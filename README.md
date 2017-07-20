# jbhasd
JSON-Based Home Automation with Service Discovery

Just a pet project to go and build some home automation devices, turn on and off appliances, read a few sensors, have some fun and learn something along the way. 

The main objective of the project is to create firmware sketches for ESP-8266 devices that enables the devices to be easily configured on a WiFI network and then automatically discovered by a server running on a networked computer or even Raspberry Pi.

There's some great existing options for home automation stacks out there and protocols such as MQTT. There are also some very powerful firmware sketches and LUA/Node-MCU scripts for the ESP-8266. However I wanted to build a lean simplified model that made it easier to manage during setup. The objective was to use a JSON status string served via URL as a means of presenting the device capabilities to a querying server. The discovery would be bassed on DNS-SD/Zeroconf. 

The esp8266_generic folder contains an Arduino sketch you can flash to and ESP-8266 device. It has 3 profiles or pin assignmenets built in for ESP-01, Sonoff Basic and Sonoff S20 devices. All these profiles are doing is defining the pin assignments in terms of switches, LEDS, and sensors. You can easily edit the fletch to define additonal profiles based around your devices and configrations.

Flash this sketch to your ESP-8266 using your preferred method. 

When you power up the device, the LED flashes at a medium speed and you have 5 seconds to groung GPIO-0 to put it into a config mode where it then acts as an open wireless AP. The LED will start flashing at a fast rate once the device enters this AP mode.

Note: For a first-time flash, the device will detect no config present and automatically enter this AP mode. Its possible you will not see any LED flashing at this stage simply because the required profile is not yet set and the correct LED GPIO assignments are therefore not applied. If you search for wifi SSIDs, you should see "esp-8266-ddddddd-Unknown". Select and you should get directed to an initial setup screen.

The initial config is to select the desired device profile, click apply and then you are shown the config options for that profile. You configure the desired SSID, password, zone name, enable/disable OTA mode and telnet loging mode and set names for the associated switch relays and their initial power-up states. You can also set the names for any defined sensors. Any switch or sensor name left blank is your means of disabling it.

You click apply, it saves the config to eeprom and reboots. If the reboot is not interrupted with another GOIO-0 ground, the device will start in WiFI STA mode after 5 seconds, connecting to the configured WiFI and registering for self discovery. While connecting to WiFI, the LED flashes as a slower rate and then issues burst of quick flashes once it connects to WiFI. 

If you need to re-configure, just reset the mains and press the button (or ground GPIO-0) within 5 seconds to get AP mode activated to let you jump in and edit settings. 

The sketch also supports OTA updating once it gets into STA mode. This makes the task of updating firmware much easier. I've only used the Arduino IDE for this but it discovers the devices without issue and lets you select the network port for flashing. This OTA updating can also be disabled in config.

The telnet logging interface runs on telnet port 23 that acts as a debug feed from the device showing all logging messages to connected clients. This activates after the device has connected to WiFI. Up to that point, logging is performed via the serial interface. You can also disable telnet logging in AP mode. To use this you just telnet to the device IP and will see a stream of debug activity as the sketch dos its thing. 

Once the devices are connected to your WiFI, you can determine the assigned IP addresses and access their JSON URLs to see the device details. Then you can use GET or POST requests on that same URL to pass in the desired control and state you wish to turn on or off any given switch. The response each time will be the current overall state. An example of the JSON status string:

```
$ curl 'http://192.168.12.165/json'
{ "name": "esp8266-9825072-Attic", "zone": "Attic", "ota_enabled" : 1, 
"controls": [{ "name": "A", "type": "switch", "state": 1 }, 
{ "name": "B", "type": "switch", "state": 0 }, 
{ "name": "C", "type": "switch", "state": 0 }, 
{ "name": "D", "type": "switch", "state": 0 }], 
"sensors": [{ "name": "Temp", "type": "temp/humidity", "humidity": "47.29", "temp": "24.20" }, 
{ "name": "Wilma", "type": "temp/humidity", "humidity": "7.50", "temp": "67.25" }], 
"system" : { "reset_reason" : "Software/System restart", "free_heap" : 30712, 
"chip_id" : 9825072, "flash_id" : 1327328, "flash_size" : 1048576, 
"flash_real_size" : 1048576, "flash_speed" : 40000000, "cycle_count" : 248706992 } }
```
The response will list the device name, zone, OTA state, controls and sensors. Each control lists its type (only switch defined for now) and given state. The sensors list their type (only temp/humidity for now) and the humidity and temp values in Celsius. There is also a sub-struct of system details showing reset reasons, free heap etc. All this system detail was taken from the variety of calls you can make on the ESP global object. 

Then to demo the toggling of a switch using GET (note the changes to control "A" on each response:
```
$ curl 'http://192.168.12.165/json?control=A&state=0'
{ "name": "esp8266-9825072-Attic", "zone": "Attic", "ota_enabled" : 1, 
"controls": [{ "name": "A", "type": "switch", "state": 0 }, 
{ "name": "B", "type": "switch", "state": 0 }, 
{ "name": "C", "type": "switch", "state": 0 }, 
{ "name": "D", "type": "switch", "state": 0 }], 
"sensors": [{ "name": "Temp", "type": "temp/humidity", "humidity": "47.29", "temp": "24.20" }, 
{ "name": "Wilma", "type": "temp/humidity", "humidity": "72.50", "temp": "20.25" }], 
"system" : { "reset_reason" : "Software/System restart", "free_heap" : 30560, 
"chip_id" : 9825072, "flash_id" : 1327328, "flash_size" : 1048576, 
"flash_real_size" : 1048576, "flash_speed" : 40000000, "cycle_count" : 4038990944 } }

$ curl 'http://192.168.12.165/json?control=A&state=1'
{ "name": "esp8266-9825072-Attic", "zone": "Attic", "ota_enabled" : 1, 
"controls": [{ "name": "A", "type": "switch", "state": 1 }, 
{ "name": "B", "type": "switch", "state": 0 }, 
{ "name": "C", "type": "switch", "state": 0 }, 
{ "name": "D", "type": "switch", "state": 0 }], 
"sensors": [{ "name": "Temp", "type": "temp/humidity", "humidity": "47.40", "temp": "24.29" }, 
{ "name": "Wilma", "type": "temp/humidity", "humidity": "28.50", "temp": "59.25" }], 
"system" : { "reset_reason" : "Software/System restart", "free_heap" : 30560, 
"chip_id" : 9825072, "flash_id" : 1327328, "flash_size" : 1048576, 
"flash_real_size" : 1048576, "flash_speed" : 40000000, "cycle_count" : 1636317248 } }
```
The examples above are all GET-based but the ESP8266 webserver supports GET and POST simultaneously.

If you browse to the base URL on its own (minus the /json path) then you get a very primitive web page with details on sensors and simple buttons to toggle the switches on/off.

Note: Python3 script zero_discover.py will easily assist in discovering your devices after they attache to your LAN. 

Python3 script jbhasd_web_server.py is a simple web server running on port 8080 and acts a console for both seeing detected devices, controlling them manually and automatically. When you start up the script, it begins using zeroconf to discover the JBHASD devices and then probes their status URL to determine the capabilities. It will then render this list of devices on the web page with fancy CSS checkbox toggles for switches and temp/humidity details shown for sensors. The web page refreshes every 10 seconds as does the background probe/re-probe of each detected devices. Devices that clock up 30+ seconds of no response are struck off the register. So it should react to devices appearing and disapearing. There is also an internal register of devices for automation and this simply turns on/off the desired switches at the appropriate time. The web engine is using cherrypy and also using jquery for background reloading etc.

Script jbhasd_device_sim.py can be used to simulate a set of fake JBHASD devices. So if you want to test out the basic idea on its own even without an actual ESP-8266 device, download and run both the web server and device sim scripts on the same LAN. You will need python3, and handful of packages added such as zeroconf & cherrypy. The scripts can be run on the same machine or on different machines. Then on the webserver machine http://ip:8080 you should see a dashboard of detected devices using names from US states and switch names for Irish counties. The switch states will update at random as the simulator is randomly changing states which are then detected by the web server. If you kill the simulator script, you will see the webserver report probe failures and within 30 seconds purge all devices that are no longer responding. 

The web server script also has an in-memory register of zones and switch names you can use to automate on/off times for named devices. 

A link to some photos of the prototypes and enclosures I've built to date..
https://goo.gl/photos/uwRadttk9wY7vvGm6
