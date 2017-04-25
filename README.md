# jbhasd
JSON-Based Home Automation with Service Discovery

Just a pet project to go and build some home automation devices, turn on and off appliances, read a few sensors, have some fun and learn something along the way. 

The main objective of the project is to create firmware sketches for ESP-8266 devices that enables the devices to be easily configured on a WiFI network and automatically discovered by a server running on a networked computer or even Raspberry Pi.

There's some great existing options for home automation stacks out there and protocols such as MQTT. There are also some very powerful firware sketches and LUA/Node-MCU scripts for the ESP-8266. However I wanted to build a lean simplified model that made it easier to manage.

The basic principle for the ESP-8266 device would be that it registers on the WiFI network and registers on MDNS. Then the server self-discovers the device and uses a URL request to determine the capabilities of the device that it discovered. The objective being that I could introduce a new device onto the network and immediately have the server understand what the new device can do. 

So first up, JSON was going to feature in this.. when the device is probed, it responds with a JSON string defining its identity and capabilities. Then the server imparts commands to the device in the form of GET or POST requests where it simply passes the name of the control it wants to change and the desired state.

Now onto the ESP-8266 sketch itself. Here I wanted to flash the sketch onto an ESP-8266 device such as a Sonoff or any of the break-out variations you can get. The intention was to have the device inside a safe housing and have it fitted with all necessary cabling for mains supply and then feature a single button and LED indicator on the housing itself.

When you first power up the device, the LED flashes at a medium speed and you have 5 seconds to press the hardware button to put it into a config mode where it then acts as an open wireless AP. The LED will start flashing at a fast rate once the device enters this AP mode.

In AP mode, the device uses captive DNS, ensuring that once you connect to it from a mobile device or computer, you should be quickly directed to a landing page where you can set the config options. 

On that setup landing page you configure the desired SSID, password, zone name, names for the associated switch relays and their initial power-up states. You can also set the names for any defined sensors. You click apply, it saves the config to eeprom and reboots. If the reboot is not interrupted with another button press, the device will start in client mode after 5 seconds, connecting to the configured WiFI and registering for self discovery. The hardware button from then on only acts as a manual over-ride for the switch relay and will turn the LED on/off as you toggle between switch states.

If you need to re-configure, just power up and press the button within 5 seconds to get AP mode activated to let you jump in and edit settings. 

Once the device is connected to your WiFI, you can discover it via zeroconf and access the JSON URL to see the device details. Then you can use GET or POST requests on that same URL to pass in the desired control and state you wish to turn on or off any given switch. The response each time will be the current overall state. I haven't added any error responses yet. So if you pass bogus parameters to it, it will just respond back with the current status:

```
$ curl 'http://192.168.12.165/json'
{ "name": "esp8266-9825072", "zone": "Attic", "controls": [{ "name": "A", "type": "switch", "state": 1 }, 
{ "name": "B", "type": "switch", "state": 0 }, { "name": "C", "type": "switch", "state": 0 }, 
{ "name": "D", "type": "switch", "state": 0 }], "sensors": [{ "name": "Temp", "type": "temp/humidity", 
"humidity": "38.00", "temp": "21.70" }, { "name": "Wilma", "type": "temp/humidity", "humidity": "3.50", 
"temp": "75.25" }], "system" : { "reset_reason" : "Power on", "free_heap" : 32200, 
"chip_id" : 9825072, "flash_id" : 1327328, 
"flash_size" : 1048576, "flash_real_size" : 1048576, "flash_speed" : 40000000, "cycle_count" : 661931206 } }
```
The response will list the device name, zone, controls and sensors. Each control lists its type (only switch defined for now) and given state. The sensors list their type (again only temp/humidity for now) and the humidity and temp values in Celsius. There is also a sub-struct of system details showing reset reasons, free heap etc. All this system detail was taken from the variety of calls you can make on the ESP global object. 

Then to demo the toggling of a switch using GET:
```
$ curl 'http://192.168.12.165/json?control=A&state=0'
{ "name": "esp8266-9825072", "zone": "Attic", "controls": [{ "name": "A", "type": "switch", "state": 0 }, 
{ "name": "B", "type": "switch", "state": 0 }, { "name": "C", "type": "switch", "state": 0 }, 
{ "name": "D", "type": "switch", "state": 0 }], "sensors": [{ "name": "Temp", "type": "temp/humidity", 
"humidity": "38.00", "temp": "21.60" }, { "name": "Wilma", "type": "temp/humidity", 
"humidity": "86.50", "temp": "12.25" }], "system" : { "reset_reason" : "Power on", "free_heap" : 32048, 
"chip_id" : 9825072, "flash_id" : 1327328, "flash_size" : 1048576, "flash_real_size" : 1048576, 
"flash_speed" : 40000000, "cycle_count" : 716904159 } }

$ curl 'http://192.168.12.165/json?control=A&state=1'
{ "name": "esp8266-9825072", "zone": "Attic", "controls": [{ "name": "A", "type": "switch", "state": 1 }, 
{ "name": "B", "type": "switch", "state": 0 }, { "name": "C", "type": "switch", "state": 0 }, 
{ "name": "D", "type": "switch", "state": 0 }], "sensors": [{ "name": "Temp", "type": "temp/humidity", 
"humidity": "38.00", "temp": "21.39" }, { "name": "Wilma", "type": "temp/humidity", 
"humidity": "0.50", "temp": "19.25" }], "system" : { "reset_reason" : "Power on", "free_heap" : 32048, 
"chip_id" : 9825072, "flash_id" : 1327328, "flash_size" : 1048576, "flash_real_size" : 1048576, 
"flash_speed" : 40000000, "cycle_count" : 954438381 } }
```
To be true to a JSON solution, I probably should go and enhance this to allow you post a full JSON body in the request. For now, I'll stick with a more basic approach as it makes command-line control easier. The examples above are all GET-based but the ESP8266 webserver supports GET and POST simultaneously.

If you browse to the base URL on its own.. http://xxx.xxx.xxx.xxx, then you get a very primitive web page with details on sensors and simple buttons to toggle the switches on/off.

The sonoff_basic.ino file in this repo is the basic firmware I wrote that should work on any Sonoff device and easily adapt to other ESP-8266 devices. You need to only correct the GPIO pin assignments as required for switches and LEDs and edit the in-memory array entries as required.

Python3 script zero_discover.py should aid in discovering your device after it attaches to your LAN. Script jbhasd_server.py is a very basic server I wrote that turns on two uplighter lights for me as a first stab at a working deployment of the firmware. The same script also reads temp and humidity from all sensors of type "temp/humidity" and saves these to a CSV file. I'll rework this example and add more sophisticated scripts as I grow the concept further.

A link to some photos of the prototypes and enclosures I've built to date..
https://goo.gl/photos/uwRadttk9wY7vvGm6
