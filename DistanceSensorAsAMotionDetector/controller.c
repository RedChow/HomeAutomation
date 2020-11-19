#include <bcm2835.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include "MQTTClient.h"
#include "queue.h"

#define ECHO RPI_V2_GPIO_P1_18
#define TRIGGER RPI_V2_GPIO_P1_16

struct lightSwitch {
	unsigned int isConnectedToBroker:1;
	unsigned int lightSwitchState:1;
};
struct lightSwitch kitchenLightSwitch;

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
	//Subscribe to the ../../RESULT topic on the Tasmota switch
	if (strstr(message->payload, "POWER")) {
		if (strstr(message->payload, "ON")) {
			kitchenLightSwitch.lightSwitchState = 1;
		}
		else {
			kitchenLightSwitch.lightSwitchState = 0;
		}
	}
	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);
	return 1;
}

void delivered(void *context, MQTTClient_deliveryToken dt) {

}
void connlost(void *context, char *cause) {
	kitchenLightSwitch.isConnectedToBroker = 0;
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

int main(int argc, char **argv) {
	if (!bcm2835_init()) {
		return 1;
	}

	MQTTClient lightSwitchClient;
	MQTTClient_connectOptions conn_opts_lightSwitch = MQTTClient_connectOptions_initializer;
	MQTTClient_deliveryToken token_lightSwitch;
	MQTTClient_deliveryToken token;

	MQTTClient_create(&lightSwitchClient, "YOUR_IP_AND_PORT", "C_Client", MQTTCLIENT_PERSISTENCE_NONE, NULL);
	conn_opts_lightSwitch.keepAliveInterval = 60;
	MQTTClient_setCallbacks(lightSwitchClient, NULL, connlost,msgarrvd , delivered);
	int rc_lightSwitch = MQTTClient_connect(lightSwitchClient, &conn_opts_lightSwitch);
	if (rc_lightSwitch != MQTTCLIENT_SUCCESS) {
		exit(EXIT_FAILURE);
	}
	//if we have made it here, we are connected
	kitchenLightSwitch.isConnectedToBroker = 1;
	kitchenLightSwitch.lightSwitchState = -1;

	MQTTClient_subscribe(lightSwitchClient, "YOUR_TOPIC_RELATED_TO_THE_SWITCH", 1);

	bcm2835_gpio_fsel(ECHO, BCM2835_GPIO_FSEL_INPT);
	bcm2835_gpio_fsel(TRIGGER, BCM2835_GPIO_FSEL_OUTP);

	//Keeping a moving average of 3 distances seems to work the best:
	//    - keeps false negatives at 0
	//	  - the time required to calculate the moving average to be within the set distance doesn't add too much of a delay, we
	//      could always change the delay at the end of the main while loop
	// Have seen distances come in as 250, 100, 250. Moving average is 200, which is above the threshold, so the 100 doesn't
	// trip the lights. For whatever reason, the sensor will pick of these random values every so often.
	Queue queue =  {.capacity = 3, .front = 0, .back = 2, .currentSize = 0, .data = {0, 0, 0}}; 
	Queue *queueP = &queue;
	const char powerTopic[] = "SOMETHING/SOMETHING_ELSE/POWER"; 
	const char statusOffTimeTopic[] = "SOMETHING/SOMETHING_ELSE/offTime";
	const char statusMovingAverageTopic[] = "SOMETHING/SOMETHING_ELSE/averageDistance";
	//At this point in time, we will not need anywhere close to the maximum length of the payload given by the MQTT spec
	char payload[200] = {0};

	time_t now = time(NULL);
	time_t in_the_future = time(NULL);
	int publishReturnCode = 1;
	MQTTClient_message pubMessage = MQTTClient_message_initializer;
	pubMessage.qos = 1;
	pubMessage.retained = 0;

	double distance = 0;
	double moving_average = 0;
	double old_moving_average = 0;
	double radius = 20;
	bool trip_the_lights = false;

	while (kitchenLightSwitch.isConnectedToBroker) {
		now = time(NULL);
		
		distance = getDistance();
		if (distance > 0) {
			enqueue(queueP, distance);
	    }
		moving_average = movingAverage(queue);
		//We only send values to SCADA that fall within this range
		if (abs(moving_average - old_moving_average) >= radius) {
			old_moving_average = moving_average;
			snprintf(payload, 11, "%3.7f", moving_average);
			pubMessage.payload = payload;
			pubMessage.payloadlen = strlen(payload);

			MQTTClient_publishMessage(lightSwitchClient, statusMovingAverageTopic, &pubMessage, &token);
			publishReturnCode = MQTTClient_waitForCompletion(lightSwitchClient, token, 1000);
			memset(payload, 0, sizeof(payload));
			trip_the_lights = true;
		}
		//If the differences between the moving averages is less than 20cm, we turn on the lights
		if (moving_average <= 250 && trip_the_lights) {
			//if the light switch is already on, there's no need to resend the command
			if(kitchenLightSwitch.lightSwitchState != 1) {
				pubMessage.payload = "1";
				pubMessage.payloadlen = 1;
				MQTTClient_publishMessage(lightSwitchClient, powerTopic, &pubMessage, &token);
				publishReturnCode = MQTTClient_waitForCompletion(lightSwitchClient, token, 1000);
				trip_the_lights = false;
			}
			//will turn lights off in five minutes
			in_the_future = now + 5*60;
			snprintf(payload, 11, "%ld", in_the_future); 
			pubMessage.payload = payload;
			pubMessage.payloadlen = strlen(payload);
			MQTTClient_publishMessage(lightSwitchClient, statusOffTimeTopic, &pubMessage, &token);
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
		//Future proofing myself in case I come back and want to send more to SCADA and forget to clear payload
		memset(payload, 0, sizeof(payload));
		delay(200);
	}
	return 0;
}
