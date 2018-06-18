# jbhasd 
#    "JSON-Based Home Automation with Service Discovery"


Just a pet project to go and build some home automation devices, turn on and off appliances, read a few sensors, have some fun and learn something along the way. 

The main objective of the project is to create firmware sketches for ESP-8266 devices that enables the devices to be easily configured on a WiFI network and then automatically discovered by a server running on a networked computer or even Raspberry Pi.

There's some great existing options for home automation stacks out there and protocols such as MQTT. There are also some very powerful firmware sketches and LUA/Node-MCU scripts for the ESP-8266. However I wanted to build a lean simplified model that made it easier to manage during setup. The objective was to use a JSON status string served via URL from the device as a means of presenting the  capabilities to a querying server. The discovery would be based on DNS-SD/Zeroconf. 

## Flashing the Firmware to ESP8266 Devices
The esp8266_generic folder contains an Arduino sketch you can flash to any ESP-8266 device. It has 4 profiles or pin assignmenets built in for ESP-01, Sonoff Basic, Sonoff S20 and H801 Wifi LED Controller devices. All these profiles are doing is defining the pin assignments in terms of switches, LEDS, and sensors. You can easily edit the fletch to define additional profiles based around your devices and configurations.

## Configuring A Device
When you power up the device, the LED flashes at a medium speed and you have 5 seconds to ground GPIO-0 to put it into a config mode where it then acts as an open wireless AP. The LED will start flashing at a fast rate once the device enters this AP mode.

Note: For a first-time flash, the device will detect no config present and automatically enter this AP mode. It's possible you will not see any LED flashing at this stage simply because the required profile is not yet set and the correct LED GPIO assignments are therefore unknown. If you search for wifi SSIDs, you should see "ESP-ddddddd-Unknown". Select and you should get directed to an initial setup screen.

The initial config step is to select the desired device profile. Selct the desired profile and click apply. The page will refresh and you are then shown the config options for that profile. For Wi-Fi, you configure the desired SSID and password. You also set a zone name which for intents and purposes is the either a room name or a common name you wants for devices in a single area. There are several additional features such as a toggle to enable/disable OTA mode and telnet loging mode. Finally, you get to set names for the associated switch relays and their initial power-up states. You can also set the names for any defined sensors. Any switch or sensor name left blank is your means of disabling it.

You click apply, it saves the config to eeprom and reboots. If the reboot is not interrupted with another GPIO-0 ground, the device will start in WiFI STA mode after 5 seconds, connecting to the configured WiFI and registering for self discovery. While connecting to WiFI, the LED flashes as a slower rate and then issues burst of quick flashes once it connects to WiFI. 

If you need to re-configure, just reset the mains and press the button (or ground GPIO-0) within 5 seconds to get AP mode activated to let you jump in and edit settings. 

## OTA Update
The sketch also supports OTA updating once it gets into STA mode. This makes the task of updating firmware much easier. I've only used the Arduino IDE for this but it discovers the devices without issue and lets you select the network port for flashing. This OTA updating can also be disabled in config.

## Telnet Logging
The telnet logging interface runs on telnet port 23 that acts as a debug feed from the device showing all logging messages to connected clients. This activates after the device has connected to WiFI. Up to that point, logging is performed via the serial interface. To use this you just telnet to the device IP and will see a stream of debug activity as the sketch does it's thing. You can also disable telnet logging in AP mode which will restrict logging to the serial interface only. 

## Communicating with JBHASD Devices
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
The response will list the device name, zone, OTA state, controls and sensors. Each control lists its type (can be 'switch' or 'led') and given state. The sensors list their type (only temp/humidity for now) and the humidity and temp values in Celsius. There is also a sub-struct of system details showing reset reasons, free heap etc. All this system detail was taken from the variety of calls you can make on the ESP global object. 

Then to demo the toggling of a switch using GET, you use the control and state fields in the GET URL. Note the changes to control "A" on each response:
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

## Using Device Discovery
Python3 script zero_discover.py is a very simple application of the zeroconf in a python script to discover these JBHASD devices on your LAN. 

## The Web Server
Python3 script jbhasd_web_server.py is a simple web server running on port 8080 and acts a console for both seeing detected devices and  controlling them manually and automatically. The web engine is using cherrypy and also using jquery for background reloading on the client browser.

When you start up the script, it begins using zeroconf to discover the JBHASD devices and then probes their status URL to determine the capabilities. In the background the script will probe/re-probe of each detected devices every 10 seconds. Devices that clock up 30+ seconds of no response are struck off the register.

You get two views for the web interface. The Zone view is the default shown when you browse to http://ip:8080 or to http://ip:8080/zone. A CSS widget panel is shown per JBHASD zone. In that panel will be all switches and sensors. If you configure multiple separate devices with the same zone name, then they will appear as a single zon panel on the web interface with a unified set of switch and sensor data shown. 

The Device view http://ip:8080/device shows each device in it's own panel. This view has more detail including a reboot switch and ap mode switch per device. There is also a URL link on each panel that will open a new browser window/tab to the raw JSON URL direct from the device. Finally, there is a Control Panel widget also shown on this page that details the number of devices being tracked, the sunset time and Lights on offset and a reboot option that will initiate the reboot of all tracked devices.

On the client browser side, the web page refreshes every 10 seconds using a background GET. So it gives you a seamless updating of the console as devices come and go or change state. 

## Lighting Automation 
The web server has an internal register of devices for automation and this simply turns on/off the desired switches at the appropriate time. You can set specific times during the day for this automation or use a keyword "sunset" which uses a calculated time as an offset from the day's given sunset time. You get to set a URL to retrieve the sunset time for your region and configre the minutes +/-offset.

## Simulating Devices
Script jbhasd_device_sim.py can be used to simulate a set of fake JBHASD devices. So if you want to test out the basic idea on its own even without an actual ESP-8266 device, download and run both the web server and device sim scripts on the same LAN. You will need python3, and handful of packages added such as zeroconf & cherrypy. The scripts can be run on the same machine or on different machines. Then on the webserver machine http://ip:8080 you should see a dashboard of detected devices using names from US states and switch names for Irish counties. The switch states will update at random as the simulator is randomly changing states which are then detected by the web server. If you kill the simulator script, you will see the webserver report probe failures and within 30 seconds purge all devices that are no longer responding. 

A link to some photos of the prototypes and enclosures I've built to date..
https://goo.gl/photos/uwRadttk9wY7vvGm6

## TODO
Nothing is perfect and one always strives for improvement :). So in no particular order, here are some enhancements I'd like to address in the future:

- Move to a JSON config file in the sketch, stored in the SPIFFS instead of EEPROM
- Add some CSS to the AP mode device web interface
- Evolve the profile model away from in-memory arrays to JSON documents that can be pushed to the devices and stored in SPIFFS
  - This would mean that the entire pin assignments could be changed without reflashing a device
- Give the python web server a JSON config file that it uses to save preferences
  - This would pave the way for adding automation rules around discovered devices instead of having to edit the script
- Implement proper long poll on the web server so that it updates instantly when any device state changes
- Sort out Analytics support on web server end:
  - Write rotating CSV files based on time and number of line limits
  - Purge mechanism for older CSV files
  - Assume an analytics system will pull and delete files from the server
  - Commit Elasticsearch scripts and related material for collecting analytics from JBHASD devices
- Examine intergation with Node Red for those that prefer to do things that way
