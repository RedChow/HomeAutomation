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
Besides, my mind is often wondering around aimlessly so having to think about and executing a voice a command
would severely disrupt whatever tangent I'm on while entering the kitchen.
* **Too little control**: A lot of products were plagued by false positives with no mechanism for sensitivity adjustment. Plus, some lacked options for delay on turning the lights on/off.
* **Expensive**: Others were just flat out too much money. I'm not paying more than $60 for a motion sensor. It should be a really simple product.

I finally decided on using a Raspberry Pi 4 W and a simple motion detector sensor. A Raspberry Pi 4 W along with all the necessary components and sensors costs less than $40. 
Plus, I would have more control over sensitivity. A Raspberry Pi 4 W would also allow me to place the sensor remotely and use Node-RED and MQTT to send data to a central SCADA system.
However, when I finally decided to buy a motion detector sensor, they were all sold out! Thus enter the distance sensor. It has actually turned out to work extremely well!
I have direct control on which distance to use to turn on lights and can set a delay on when to turn them off. Have direct access to the distances picked up by the sensor has
virtually eliminated false positives. In fact, after a month of using this set up, I've only had one false positive.
## Set Up


