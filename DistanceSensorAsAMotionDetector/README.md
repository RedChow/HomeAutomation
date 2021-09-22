# Using a Distance Sensor As a Motion Detector
## Background
I recently wanted to automate turning on lights when I entered a room or an area in the house. 

But when I went to buy motion detectors or other solutions for automatically turning on lights, I found several issues with many of the products.
* **Accessible only through the company's app**: It seemed the majority of the units were only accessible through an app desiged by the product manufacturer.
	* I didn't want or need a dozen different apps for home automation. 
I'm already using Ignition Maker, which comes with a web/mobile browser module to build your own app. 
	* A device that is only accessible through their own app could mean I could no longer access and control devices.
	If I experience an Internet outage, my home automation should still work on the local area network.
* **Reliant on voice control**: Many had other options for controlling outside of their own app, but were dependent on Alexa, Siri, Google Assistant, or something similar. To me, having to audibly say, "Hey Google" or "Hey Siri" is not
automation. I shouldn't have to do anything except be enter the area for the lights to turn on.
* **Too little control**: A lot of products were plagued by false positives with no mechanism for sensitivity adjustment. Plus, some lacked options for delay on turning the lights on/off.
* **Expensive**: Others were just flat out too much money. I'm not paying more than $60 for a motion sensor. It should be a really simple product.

I finally decided on using a Raspberry Pi 4 W and a simple motion detector sensor. A Raspberry Pi 4 W along with all the necessary components and sensors costs less than $40. 
Plus, I would have more control over sensitivity. A Raspberry Pi 4 W would also allow me to place the sensor remotely and use Node-RED and MQTT to send data to a central SCADA system.
However, when I finally decided to buy a motion detector sensor, they were all sold out! Thus enter the distance sensor. It has actually turned out to work extremely well!
I have direct control on which distance to use to turn on lights and can set a delay on when to turn them off. Have direct access to the distances picked up by the sensor has
virtually eliminated false positives. In fact, after a month of using this set up, I've only had one false positive.
## Set Up
### Hardware
* [HC-SR04](https://www.adafruit.com/product/4007) - Distance sensor
* [Raspberry Pi Zero W](https://www.raspberrypi.org/products/raspberry-pi-zero-w/) - Built-in wifi makes it easy to set wherever
* [Sonoff BasicR3 WiFi Switch](https://sonoff.tech/product/wifi-diy-smart-switches/basicr3) - Sonoff BasicR3 WiFi switch that has Tasmota firmware. See [Tasmotizer](https://github.com/tasmota/tasmotizer).
* Other: 4 Female to female connectors, [Raspberry PI Zero headers](https://www.adafruit.com/product/3413), and a power supply for the Raspberry PI Zero W
### Software
* [bcm2835](https://www.airspayce.com/mikem/bcm2835/): C library for reading/writing GPIO
* [Paho MQTT C Library](https://www.eclipse.org/paho/index.php?page=clients/c/index.php): C library for working with MQTT
* MQTT broker: There are several options here. I use mosquitto. Ignition Maker edition comes with an MQTT broker that can be used to send and receive MQTT messages, but a third-party client cannot connect to subscribe to messages. Status messages from the C program are sent to mosquitto; Ignition Maker subscribes to topics related to the Sonoff switch. 
* queue.h/queue.c: data structure for keeping a moving average. Slightly modified traditional Queue data structure for not needing to manually dequeue/remove items; enqueue will always add an item to the queue and remove items as necessary.
#### controller.c
C program to control the Sonoff WiFi switch. Some of the code was lifted from example programs from Paho.
If you want to make changes or modify this program, here are a few highlights:
* There are several comments beginning with NOTE.
This is where you will need to change parameters if you want different behavior.
* Be sure to double check which pins you are using and change the program accordingly.
* double radius: Controls when moving_average is sent to SCADA. 
 "if (abs(moving_average - old_moving_average) >= radius) {" is where radius is used. Radius could be enlarged if you want a bigger change before sending the value to SCADA, or it could be lessened if you want more values coming into SCADA. 
This also controls the difference in distance in which the lights turn on. I have it currently set to 20cm, which has seemed to work very well for my purposes.
If you wanted to use a percentage, you could change Line 138 to something like "if (moving_average > 1.1\* old_moving_average || moving_average < .9\*old_moving_average) {" for sending values that change more than 10%. 15% would change 1.1 to 1.15 and .9 to .85, etc.
* "if (moving_average <= 250 && trip_the_lights) {": 
After testing, 250 is about where I would like the lights to turn on.
You can change this according to where you want the lights to turn on. 
The max distance of the sensor is 400cm, which is a little over 14ft. 
* "in_the_future = now + 5\*60;": If you want the light to turn off sooner than 5 min, change this. Resolution is by seconds, so if you want the light to turn off after 30 sec, just change to "in_the_future = now + 30;".
* Compile the program with "gcc -g -o controller ./controller.c queue.c -lbcm2835 -l paho-mqtt3c -lcjson." I usually direct any output to /dev/null, so I use "./controller > /dev/null 2> /dev/null &."


## ToDo/Future Plans
* Broaden the capabilities of the program to allow set points for SCADA, such as distance required to trigger the light, minutes to turn off the light, moving average capacity, and delay between finding distances.
* Send more statuses back to SCADA such as last communication to broker
* Have a manual mode to be set in SCADA so that the lights can be turned off/on remotely. This might could be useful for simulating activity in the house while away.
* Add a delay when communications to the MQTT broker drops. 

## Updates
* 2020-11-19: Moved finding the distance to a function outside of the main while loop.
Added a couple of boolean variables to actually use the distance sensor as a motion detector. 
I.e., when the difference in distances are greater than the variable radius, the lights will turn on.
* 2020-11-23: Added the usage of cJSON. cJSON is superb; makes dealing with JSON in C super easy.
* 2021-09-22: Really need to update this code, as there are some problems with it. 
	* The code exits when MQTT connection is lost and doesn't allow for re-connection attempts.
	* Should probably take out the sleep method (usleep(10000)) at the end of the main loop and instead check for time differences are above or equal to a preset. 
