# jbhasd 
#    "JSON-Based Home Automation with Service Discovery"


Just a pet project to go and build some home automation devices, turn on and off appliances, read a few sensors, have some fun and learn something along the way. 

The main objective of the project is to create firmware sketches for ESP-8266 devices that enables the devices to be easily configured on a WiFI network and then automatically discovered by a server running on a networked computer or even Raspberry Pi.

There's some great existing options for home automation stacks out there and protocols such as MQTT. There are also some very powerful firmware sketches and LUA/Node-MCU scripts for the ESP-8266. However I wanted to build a lean simplified model that made it easier to manage during setup. The objective was to use a JSON status string served via URL from the device as a means of presenting the  capabilities to a querying server. The discovery would be based on DNS-SD/Zeroconf. 

## Quick Summary of the Setup Steps
- The generic firmware is flashed to any ESP-8266 Device
- The device should power up in AP mode with SSID JBHASD-XXXXXXXX where XXXXXXXX is the CPU ID of the ESP8266
- You connect to the SSID where you can then set Zone, Wifi SSID and Password and apply changes
- The device then reboots and connects to your network in STA mode (WiFI client)
- You can then access the device with a JSON GET-based API and manage it from there
- MDNS and DNS-SD ae built-in and an accompanying web server can be used as a hub for the devices providing a web portal, means of managing automation and even downloading config data to newlty attached or reset devices

## Using AP Mode to Configuring WiFI
When you first power up the device after flashing, it should auto launch as a open wireless AP. The SSID will be of the format "JBHASD-XXXXXXXX". 

After connecting to the SSID, you should be dropped into a config page that lets you set three fields.. zone, WiFI SSID and password. 

You then click apply, it saves the config to eeprom and reboots. The device should then reboot, enter STA mode and connect to your configured WiFI router as a client. If you need to access the AP mode again, power-cycle the device and ground GPIO-0 within the first 5 seconds and it should re-enter AP mode. 

At this stage, all you have is a device that connects to your WiFI. It is not yet configured in terms of pin asignments for switches and sensors.

But you can now communicate to the device and see its status information

## Getting the Device JSON Status

So for this example, we'll assume a device was configured and successfully connected to the WiFI network. Also the IP was determined to be 192.168.12.145. So a simple GET on the URL will return a JSON status string detailling the particulars of this device.

```
$ curl 'http://192.168.12.145?pretty=1'
{
  "name": "JBHASD-00072D6D",
  "zone": "ESP-01",
  "wifi_ssid": "XXX",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 0,
  "system": {
    "compile_date": "Sep 17 2018 21:34:16",
    "reset_reason": "Software/System restart",
    "free_heap": 22816,
    "chip_id": 470381,
    "flash_id": 1327343,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 2062378582,
    "millis": 5140614
  },
  "controls": []
}
```

The section above for controls is where we would normally see details on any switches, sensors etc that are being managed by this device. Given this is a clean setup, no such detail is defined yet. Also the "configured" attribute of this device is set to 0 which confirms that no full device configuration has been pushed to this device.

The pretty=1 args to this call could be omitted or passed with a 0 value and it will then return the JSON status as a single string without the pretty formatting. Use whateveer you prefer. 

## Pushing Configuration to the Device

The example below is how you would cofiguration to a given device and setup its GPIO pins to control any attached hardware or onboard features.

```
curl -G  "http://192.168.12.165/json" --data-urlencode 'config={ "zone" : "Prototype 1", 
"wifi_ssid" : "XXX", "wifi_password" : "XXX", "ota_enabled" : 1, "telnet_enabled" : 1, 
"mdns_enabled" : 1, "manual_switches_enabled" : 1, "boot_pin" : 0, "wifi_led_pin" : 13, 
"force_apmode_onboot" : 0, "controls" : [ { "name" : "Relay", "type" : "switch", 
"sw_mode" : "toggle", "sw_state" : 0, "sw_relay_pin" : 12, "sw_led_pin" : 13, 
"sw_man_pin" : 0 }, { "name" : "Temp", "type" : "temp/humidity", "th_variant" : "DHT21", 
"th_temp_offset" : 0, "th_pin" : 14 } ] }'
```
In the above example, the device in question is a Sonoff Basic switch. This device has a single relay for controlling mains appliances. It's GPIO-12 is the pin for this relay. There is also an onboard LED that is tied to GPIO-13 and an onboard button which is connected to GPIO-0. That variant of the Sonoff also has a spare accesible GPIO-14 pin via the header solder points on the board. 

The config arg to the device passed a JSON config record that specifies the devices zone, wifi_ssid and password. So while you have already set this via AP mode, this config step lets you re-assign it if desired. Then following this are a series of enabled options for OTA, telnet, MDNS and manual switches. More on these later.

The "boot_pin" specifies the assigned pin used for putting the device into AP mode when it is first booted. This is the same GPIO-0 pin as used for flashing. So it typically matches at least one onboard switch on most devices or at least something that can be attached to available pinouts on the board. The "wifi_led_pin" represents the LED used for displaying WiFI connect status. That same LED provides visual feedback when the device is being booted and entering AP mode. 

Ignore "force_apmode_onboot" for now. 

We're now at the controls array and what you can see is a JSON sub-object for a control named 'Relay' or type 'switch'. The switch is in 'toggle' mode, meaning it's assigned manual pin toggles between on/off. The relay pin is set to 12, LED pin to 13 and manual pin to 0. 

So with that control configured, the device will boot and configure a switch called Relay that drives the onboard relay via GPIO-12 and match the on/off state of that relay with the onboard LED (GPIO-13). Pressing the onboard flash button (GPIO-0) will act as a toggle-on and off button for the relay.

Next up is the configuration for a DHT21 temperature sensor that is using the additonal GPIO-14. This sets the sensor name to Temp, its type to temp/humidity and then the desired pin and sensor variant required. There is also a temperature offset if the sensor accuracy needs adjustment. 

With those settings applied, when this device boots again, it will flash the GPIO-13 LED at a medium rate for 5 seconds giving you time to ground GPIO-0 (boot_pin) and put the device into AP Mode. If you activate that boot PIN during that initial 5 seconds, the LED flashing will go to a fast rate to indicate AP mode. If you leave that boot sequence alone, after 5 seconds, the initial medium flashing rate will drop to a slower rate to indicate that the WiFI connect stage has started and it will issue a quick burst of flashes to indicate that the device has successfully conected to wifi. 

Once it reboots, the following JSON is now returned when the device is probed.. 

```
$ curl "http://192.168.12.165/?pretty=1"
{
  "name": "JBHASD-0095EB30",
  "zone": "Prototype 1",
  "wifi_ssid": "XXX",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 1,
  "system": {
    "compile_date": "Sep 17 2018 21:34:58",
    "reset_reason": "Software/System restart",
    "free_heap": 22672,
    "chip_id": 9825072,
    "flash_id": 1327328,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 2442867768,
    "millis": 7373141
  },
  "controls": [
    {
      "name": "Relay",
      "type": "switch",
      "state": 0,
      "context": "init",
      "behaviour": "toggle"
    },
    {
      "name": "Temp",
      "type": "temp/humidity",
      "humidity": 62.8,
      "temp": 21.8
    }
  ]
}
```

So now, we have a populated 'controls' array being return and this is crucial as it plays a role in how this device is then integrated with hub-type applications that need to manipulate the device. 

## Manipulating controls on the Device
In the above example, we set up a Sonoff Basic device with its relay put to use and also leveraged the spare GPIO pin for a temperature device.

Now we can instruct the relay to turn on.. 

```
$ curl "http://192.168.12.165?pretty=1&control=Relay&state=1"
{
  "name": "JBHASD-0095EB30",
  "zone": "Prototype 1",
  "wifi_ssid": "XXX",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 1,
  "system": {
    "compile_date": "Sep 17 2018 21:34:58",
    "reset_reason": "Software/System restart",
    "free_heap": 22656,
    "chip_id": 9825072,
    "flash_id": 1327328,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 1194599308,
    "millis": 7894409
  },
  "controls": [
    {
      "name": "Relay",
      "type": "switch",
      "state": 1,
      "context": "network",
      "behaviour": "toggle"
    },
    {
      "name": "Temp",
      "type": "temp/humidity",
      "humidity": 62.9,
      "temp": 22
    }
  ]
}
```

The use of control=<name> informs the device which control we are manipulating and the the state=1 is used to put that selected control into a state of 1. That turns on the relay.
  
You will also notice that we get back the updated JSON status immediately and the "Relay" control shows its new state of 1. 


TO BE CONTINUED
