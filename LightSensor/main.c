/*
 * The struct lightSensor has four members:
 * 		isConnectedToBroker: keeps track of whether we are connected to the MQTT broker
 * 		isOn: keeps track of whether the lights are on/off
 * 		manualSwitch: SCADA can send commands to override automatic turning on/off
 * 		oneShot: aids in resetting manualSwitch to -1 after a cycle of lux reading less than THRESHOLD to more than THRESHOLD
 * 
 * manualSwitch = -1 means the program is automatic mode
 * manualSwitch = 0 means SCADA has sent off command and overrides automatic setting
 * manualSwitch = 1 means SCADA has sent on command and overrides automatic setting
 * 
 * Adjust the bounds for lux as you see fit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>

#include "DEV_Config.h"
#include <time.h>
#include "TSL2591.h"
#include "MQTTClient.h"
#include <cjson/cJSON.h>

#define THRESHOLD 10.0

struct lightSensor {
	bool isConnectedToBroker;
	bool isOn;
	int manualSwitch;
	bool oneShot;
};
struct lightSensor frontLightSensor = {.isConnectedToBroker = false, .isOn= false, .manualSwitch = -1, .oneShot = false};

/********************************************************************
 * Functions related to MQTT
 * Definitions are after main
 *******************************************************************/
void delivered(void *context, MQTTClient_deliveryToken dt);
int messageArrived(void *context, char *topicName, int topicLen, MQTTClient_message *message);
void connectionLost(void *context, char *cause);

void  Handler(int signo) {
    printf("\r\nHandler:exit\r\n");
    DEV_ModuleExit();
    exit(0);
}

int main(void)
{
	char *subscription_topics[] = {"tasmota_4b58fb/stat/RESULT", "tasmota_4b58fb/SCADA/#"};
	int qos_topics[] = {0, 0};
	MQTTClient lightSensorClient;
	MQTTClient_connectOptions conn_opts_lightSensor = MQTTClient_connectOptions_initializer;
	MQTTClient_deliveryToken token;
	MQTTClient_create(&lightSensorClient, "CHANGE_TO_YOUR_BROKER", "LightSensor_Client", MQTTCLIENT_PERSISTENCE_NONE, NULL);
	conn_opts_lightSensor.keepAliveInterval = 60;
	MQTTClient_setCallbacks(lightSensorClient, NULL, connectionLost, messageArrived, delivered);
	int rc = -1;
	if ((rc = MQTTClient_connect(lightSensorClient, &conn_opts_lightSensor)) != MQTTCLIENT_SUCCESS) {
		exit(EXIT_FAILURE);
	}
	//if we made here, we have connected
	frontLightSensor.isConnectedToBroker = true;
	MQTTClient_subscribeMany(lightSensorClient, 2, subscription_topics, qos_topics);

	MQTTClient_message publish_message = MQTTClient_message_initializer;
	publish_message.qos = 1;
	publish_message.retained = 0;

    // Exception handling:ctrl + c
    signal(SIGINT, Handler);
    DEV_ModuleInit();
    
    TSL2591_Init();
    // TSL2591_SET_InterruptThreshold(0xff00, 0x0010);
	cJSON *scada_data = cJSON_CreateObject();
	cJSON_AddItemToObject(scada_data, "lux", cJSON_CreateNumber(0));
	cJSON_AddItemToObject(scada_data, "infrared", cJSON_CreateNumber(0));
	cJSON_AddItemToObject(scada_data, "visible_light", cJSON_CreateNumber(0));
	cJSON_AddItemToObject(scada_data, "full_spectrum", cJSON_CreateNumber(0));
	double lux_reading = 0;
	double infrared_reading = 0;
	double visible_light_reading = 0;
	double full_spectrum_reading = 0;
	bool turnOnLights = false;
    while (frontLightSensor.isConnectedToBroker) {
		lux_reading = TSL2591_Read_Lux();
        TSL2591_SET_LuxInterrupt(50,200);
		infrared_reading = TSL2591_Read_Infrared();
		visible_light_reading = TSL2591_Read_Visible();
		full_spectrum_reading = TSL2591_Read_FullSpectrum();

		cJSON_ReplaceItemInObject(scada_data, "lux", cJSON_CreateNumber(lux_reading));
		cJSON_ReplaceItemInObject(scada_data, "infrared", cJSON_CreateNumber(infrared_reading));
		cJSON_ReplaceItemInObject(scada_data, "visible_light", cJSON_CreateNumber(visible_light_reading));
		cJSON_ReplaceItemInObject(scada_data, "full_spectrum", cJSON_CreateNumber(full_spectrum_reading));
		char *message_string = cJSON_Print(scada_data);
		publish_message.payload = message_string;
		publish_message.payloadlen = strlen(message_string);
		MQTTClient_publishMessage(lightSensorClient, "tasmota_4b58fb/SCADA/sensorinfo", &publish_message, &token);
		rc = MQTTClient_waitForCompletion(lightSensorClient, token, 1000);
		/*
		 * When lux > THRESHOLD the sun is rising; lux < THRESHOLD means sun is setting.
		 * lux > THRESHOLD we reset SCADA overrides and put back into automatic handling of turning on/off lights.
		 * (This is just the way I wanted the program to run; didn't want to have to manually switch modes in SCADA.)
		 * The oneShot member of the struct lightSensor will become true when the sun sets.
		 * When the sun rises, and the first time lux passes THRESHOLD, oneShot will be set to false and manualSwitch will be set to -1.
		 * oneShot will stay false until the sun sets.
		 */
		if (lux_reading >= THRESHOLD) {
			if (frontLightSensor.oneShot) {
				frontLightSensor.manualSwitch = -1;
				frontLightSensor.oneShot = false;
			}
		}
		else {
			frontLightSensor.oneShot = true;
		}
		switch (frontLightSensor.manualSwitch) {
			case -1:
				if (lux_reading < THRESHOLD) {
					turnOnLights = true;
				}
				else {
					turnOnLights = false;
				}
				break;
			case 0:
				turnOnLights = false;
				break;
			case 1:
				turnOnLights = true;
				break;
			default:
				turnOnLights = false;
		}
		if (turnOnLights) {
			//These two if statements could be combined into one, but I have plans to add in more in between the two if statements.
			//If ithe lights are not on, send the command; otherwise, don't send any command
			if (!frontLightSensor.isOn) {
				publish_message.payload = "1";
				publish_message.payloadlen = 1;
				MQTTClient_publishMessage(lightSensorClient, "tasmota_4b58fb/cmnd/POWER", &publish_message, &token);
				MQTTClient_waitForCompletion(lightSensorClient, token, 1000);
			}
		}
		else {
			//if the lights are on, turn them off; otherwise don't send any command
			if (frontLightSensor.isOn) {
				publish_message.payload = "0";
				publish_message.payloadlen = 1;
				MQTTClient_publishMessage(lightSensorClient, "tasmota_4b58fb/cmnd/POWER", &publish_message, &token);
				MQTTClient_waitForCompletion(lightSensorClient, token, 1000);
			}
		}
		//free the memory allocated for message_string
		free(message_string);
		delay(1000*60*1);
    }
	//System Exit
	DEV_ModuleExit();
	return 0;
}

void delivered(void *context, MQTTClient_deliveryToken dt) {
	//nothing to see here
}

int messageArrived(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
	cJSON *incoming_message = cJSON_Parse(message->payload);
	if (cJSON_HasObjectItem(incoming_message, "manualSwitch")) {
		const cJSON *manual_switch = cJSON_GetObjectItem(incoming_message, "manualSwitch");
		if (cJSON_IsNumber(manual_switch)) {
			frontLightSensor.manualSwitch = manual_switch->valueint;
		}
	}
	else if (cJSON_HasObjectItem(incoming_message, "POWER")) {
		const cJSON *power = cJSON_GetObjectItem(incoming_message, "POWER");
		if (cJSON_IsString(power) && power->valuestring != NULL) {
			if (strstr(power->valuestring, "ON")) {
				frontLightSensor.isOn = true;
			}
			else if (strstr(power->valuestring, "OFF")) {
				frontLightSensor.isOn = false;
			}
		}
	}
	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);
	cJSON_Delete(incoming_message);
	return 1;
}

void connectionLost(void *context, char *cause) {
	frontLightSensor.isConnectedToBroker = false;
}
