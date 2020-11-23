#include <bcm2835.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <cjson/cJSON.h>

#include "MQTTClient.h"
#include "queue.h"

//NOTE: Change the following two lines to the pins you are using
#define ECHO RPI_V2_GPIO_P1_18
#define TRIGGER RPI_V2_GPIO_P1_16

struct lightSwitch {
	unsigned int isConnectedToBroker:1;
	unsigned int lightSwitchState:1;
};
struct lightSwitch kitchenLightSwitch;

/*
 * Functions related to MQTT; definition appear after main
 */
void delivered(void *context, MQTTClient_deliveryToken dt);
void connectionLost(void *context, char *cause);
int messageArrived(void *context, char *topicName, int topicLen, MQTTClient_message *message);

/*
 * Distance function
 */
double getDistance(void);

int main(int argc, char **argv) {
	if (!bcm2835_init()) {
		return 1;
	}

	MQTTClient lightSwitchClient;
	MQTTClient_connectOptions conn_opts_lightSwitch = MQTTClient_connectOptions_initializer;
	MQTTClient_deliveryToken token_lightSwitch;
	MQTTClient_deliveryToken token;

	//NOTE: CHANGEME = IP and port of your MQTT broker; e.g. 192.168.1.66:1883
	MQTTClient_create(&lightSwitchClient, "CHANGEME", "C_Client", MQTTCLIENT_PERSISTENCE_NONE, NULL);
	conn_opts_lightSwitch.keepAliveInterval = 60;
	MQTTClient_setCallbacks(lightSwitchClient, NULL, connectionLost, messageArrived, delivered);
	int rc_lightSwitch = MQTTClient_connect(lightSwitchClient, &conn_opts_lightSwitch);
	if (rc_lightSwitch != MQTTCLIENT_SUCCESS) {
		exit(EXIT_FAILURE);
	}
	//if we have made it here, we are connected
	kitchenLightSwitch.isConnectedToBroker = 1;
	kitchenLightSwitch.lightSwitchState = -1;

	MQTTClient_subscribe(lightSwitchClient, "tasmota_dc43bc/stat/RESULT", 1);

	bcm2835_gpio_fsel(ECHO, BCM2835_GPIO_FSEL_INPT);
	bcm2835_gpio_fsel(TRIGGER, BCM2835_GPIO_FSEL_OUTP);

	//Keeping a moving average of 3 distances seems to work the best:
	//    - keeps false negatives at 0
	//	  - the time required to calculate the moving average to be within the set distance doesn't add too much of a delay, we
	//      could always change the delay at the end of the main while loop
	// Have seen distances come in as 400, 200, 400. Moving average is 333.33, which is above the threshold, so the 200 doesn't
	// trip the lights. For whatever reason, the sensor will pick of these random values every so often.
	//NOTE: .back is ALWAYS one less than capacity. Be sure to change .data to the appropriate number of zero for capacity.
	Queue queue =  {.capacity = 4, .front = 0, .back = 3, .currentSize = 0, .data = {0, 0, 0, 0}}; 
	Queue *queueP = &queue;
	//NOTE: Change the following topics
	char powerTopic[] = "tasmota_dc43bc/cmnd/POWER";
	char statusOffTimeTopic[] = "tasmota_dc43bc/status/offTime";
	char statusMovingAverageTopic [] = "tasmota_dc43bc/status/averageDistance";
	//At this point in time, we will not need anywhere close to the maximum length of the payload given by the MQTT spec
	char payload[200] = {0};

	time_t now = time(NULL);
	time_t in_the_future = time(NULL);
	int publishReturnCode = 1;
	MQTTClient_message pubMessage = MQTTClient_message_initializer;
	pubMessage.qos = 1;
	pubMessage.retained = 0;
	
	/*
	 * NOTE: I have this configured as {"average_distance": moving_average, "off_time": LONGINT}
	 * You can add/remove items to the MQTT payload by using cJSON_AddItemToObject.
	 */

	cJSON *scada_data = cJSON_CreateObject();
	cJSON_AddItemToObject(scada_data, "average_distance", cJSON_CreateNumber(0));
	cJSON_AddItemToObject(scada_data, "off_time", cJSON_CreateNumber(0));

	double distance = 0;
	double moving_average = 0;
	double old_moving_average = 0;
	double radius = 20;
	bool trip_the_lights = false;
	bool send_scada_data = false;

	while (kitchenLightSwitch.isConnectedToBroker) {
		now = time(NULL);
		send_scada_data = false;
		distance = getDistance();
		enqueue(queueP, distance);
		moving_average = movingAverage(queue);
		
		//We only send values to SCADA that fall within this range, or if the time_off updates
		if (abs(moving_average - old_moving_average) >= radius) {
			old_moving_average = moving_average;
			trip_the_lights = true;
			send_scada_data = true;
		}
		//If the differences between the moving averages is less than 20cm, we turn on the lights
		if (moving_average <= 250 && trip_the_lights) {
			//NOTE: This sets the time to turn off the lights in 5 minutes
			in_the_future = now + 5*60;
			send_scada_data = true;
			//if the light switch is already on, there's no need to resend the command
			if(kitchenLightSwitch.lightSwitchState != 1) {
				pubMessage.payload = "1";
				pubMessage.payloadlen = 1;
				MQTTClient_publishMessage(lightSwitchClient, powerTopic, &pubMessage, &token);
				publishReturnCode = MQTTClient_waitForCompletion(lightSwitchClient, token, 1000);
				trip_the_lights = false;
			}
		}
		if (in_the_future <= now) {
			//if the light switch is off, there's no need to resend the command
			if (kitchenLightSwitch.lightSwitchState != 0) {
				pubMessage.payload = "0";
				pubMessage.payloadlen = 1;
				MQTTClient_publishMessage(lightSwitchClient, powerTopic, &pubMessage, &token);
				publishReturnCode = MQTTClient_waitForCompletion(lightSwitchClient, token, 1000); 
				trip_the_lights = false;
			}
		}
		if (send_scada_data) {
			cJSON_ReplaceItemInObject(scada_data, "average_distance", cJSON_CreateNumber(moving_average));
			cJSON_ReplaceItemInObject(scada_data, "off_time", cJSON_CreateNumber(in_the_future));
			char *message_string = cJSON_Print(scada_data);
			pubMessage.payload = message_string;
			pubMessage.payloadlen = strlen(message_string);
			//NOTE: Change the topic here to what you need
			MQTTClient_publishMessage(lightSwitchClient, "CHANGEME", &pubMessage, &token);
			MQTTClient_waitForCompletion(lightSwitchClient, token, 1000);
			free(message_string);
		}
		//NOTE: This operates pretty fast; change the usleep parameter to sleep longer/shorter
		usleep(100000);
	}
	return 0;
}
 

void delivered(void *context, MQTTClient_deliveryToken dt) {
	//nothing to do here
}

void connectionLost(void *context, char *cause) {
	kitchenLightSwitch.isConnectedToBroker = 0;
}

int messageArrived(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
	cJSON *incoming_message = cJSON_Parse(message->payload);
	if (cJSON_HasObjectItem(incoming_message, "POWER")) {
		const cJSON *power = cJSON_GetObjectItem(incoming_message, "POWER");
		if (cJSON_IsString(power) && power->valuestring != NULL) {
			if (strstr(power->valuestring, "ON")) {
				kitchenLightSwitch.lightSwitchState = 1;
			}
			else if (strstr(power->valuestring, "OFF")) {
				kitchenLightSwitch.lightSwitchState = 0;
			}
		}
	}
	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);
	cJSON_Delete(incoming_message);
	return 1;
}

double getDistance(void) {
	struct timeval start, end, loopCheck;
	double delta = 0;
	double deltaTime = 0;
	double distance = 0;
	bcm2835_gpio_write(TRIGGER, HIGH);
	usleep(10);
	bcm2835_gpio_write(TRIGGER, LOW);
	//while loops always need an exit: the current time is measured against
	//when the loop started; if it takes too long exit out
	//This will be fine as we'll check for positive distance below.
	gettimeofday(&loopCheck, NULL);
	while(!bcm2835_gpio_lev(ECHO)) {
		gettimeofday(&start, NULL);
		if (start.tv_usec - loopCheck.tv_usec > 20000) {
			return 400;
		}
	}
	gettimeofday(&loopCheck, NULL);
	while(bcm2835_gpio_lev(ECHO)) {
		gettimeofday(&end, NULL);
		if (end.tv_usec - loopCheck.tv_usec > 40000) {
			return 400;
		}
	}
	
	deltaTime = end.tv_usec - start.tv_usec;
	delta = deltaTime/1000000;
	distance = (delta*36000)/2;
	if (distance > 400 || distance < 0) {
		return 400;
	}
	return distance;
}
