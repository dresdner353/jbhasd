# jbhasd
JSON-Based Home Automation with Service Discovery

Just a pet project to go and build some home automation devices, turn on and off appliances, read a few sensors, have some fun and learn something along the way. 

The main objective of the project is to create firmware sketches for ESP-8266 devices that enables the devices to be easily configured on a WiFI network and automatically discovered by a server running on a networked computer or even Raspberry Pi.

There's some great existing options for home automation stacks out there and protocols such as MQTT. There are also some very powerful firware sketches and LUA/Node-MCU scripts for the ESP-8266. However I wanted to build a lean simplified model that made it easier to manage.

The basic principle for the ESP-8266 device would be that it registers on the WiFI network and registers on MDNS. Then the server self-discovers the device and uses a URL request to determine the capabilities of the device that it discovered. The objective being that I could introduce a new device onto the network and immediately have the server understand what the new device can do. 

So first up, JSON was going to feature in this.. when the device is probed, it responds with a JSON string defining its identity and capabilities. Then the server imparts commands to the device in the form of GET or POST requests where it simply passes the name of the control it wants to change and the desired state. At this early stage, its just switches I'm supporting, but I'll be getting around to adding support for temp sensors and other controls as I further develop the solution.

Now onto the ESP-8266 sketch itself. Here I wanted to flash the sketch onto an ESP-8266 device such as a Sonoff or any of the various break-out variations you can get. The intention was to have the device inside a safe housing and have it fitted with the necessary mains supply and a single button and LED indicator. 

When you first power up the device, the LED flashes at a medium speed and you have 5 seconds to press the hardware button to put it into programming mode where it acts as an open wireless AP. The LED will start flashing at a fast rate once the device enters AP mode,

In AP mode, the device uses captive DNS, ensuring that once you connect to it from a mobile device or computer, you should be quickly directed to a landing page where you can set the config options. 

On that setup landing page you configure the desired SSID, password, zone name, names for the associated switch relays and their initial power-up states. You click apply, it saves the config to eeprom, reboots and if left alone will start in client mode after 5 seconds, connecting to the configured Wifi and registering for self discovery.

If you need to re-configure, just power up and press the button within 5 seconds to get AP mode activated to let you jump in and edit settings. 

Once the device is connected to your WiFI, you can discover it via zeroconf and access the JSON URL to see the device details. Then you can use GET or POST to pass in the desired control and state you wish to set. The response each time will be the current overall state. I haven't added any error responses yet. So if you pass bogus paramaters to it, it will just respond back with the current status:

```
pi@raspberrypi:~ $ curl 'http://192.168.12.196/json'
{ "name": "esp8266-9840833", "zone": "Playroom", "controls": [{ "name": "Uplighter", "type": "switch", "state": 1 }] }
pi@raspberrypi:~ $ curl 'http://192.168.12.196/json?control=Uplighter&state=0'
{ "name": "esp8266-9840833", "zone": "Playroom", "controls": [{ "name": "Uplighter", "type": "switch", "state": 0 }] }
pi@raspberrypi:~ $ curl 'http://192.168.12.196/json?control=Uplighter&state=1'
{ "name": "esp8266-9840833", "zone": "Playroom", "controls": [{ "name": "Uplighter", "type": "switch", "state": 1 }] }
pi@raspberrypi:~ $
```

The sonoff_basic.ino file in this repo is the basic firmware I wrote that should work on any Sonoff device and easily adapt to other ESP-8266 devices. You need to only correct the GPIO pin assignments as required for switches and LEDs.

Python3 script zero_discover.py should aid in discovering your device after it attaches to your LAN. Script jbhasd_server.py is a very basic server I wrote that turns on two uplighter lights for me as a first stab at a working deployment of the firmware. I'll add more sophisticated scripts as I grow the concept further.
