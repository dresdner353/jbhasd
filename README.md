# jbhasd 

To be continued..
#    "JSON-Based Home Automation with Service Discovery"


Just a pet project to go and build some home automation devices, turn on and off appliances, read a few sensors, have some fun and learn something along the way. 

The main objective of the project is to create firmware sketches for ESP-8266 devices that enables the devices to be easily configured on a WiFI network and then automatically discovered by a server running on a networked computer or even Raspberry Pi.

There's some great existing options for home automation stacks out there and protocols such as MQTT. There are also some very powerful firmware sketches and LUA/Node-MCU scripts for the ESP-8266. However I wanted to build a lean simplified model that made it easier to manage during setup. The objective was to use a JSON status string served via URL from the device as a means of presenting the capabilities to a querying server. The discovery would be based on DNS-SD/Zeroconf. 

## Quick Summary of the Setup Steps
- The generic firmware is flashed to any ESP-8266 Device
- The device should power up in AP mode with SSID JBHASD-XXXXXXXX where XXXXXXXX is the CPU ID of the ESP8266
- You connect to the SSID where you can then select Wifi SSID and Password and apply changes
- The device then reboots and connects to your network in STA mode (WiFI client)
- You can then access the device with a JSON-based API and manage it from there
- MDNS and DNS-SD are built-in and an accompanying web server can be used as a hub for the devices providing a web portal, means of managing automation and even downloading config data to newly attached or reset devices

## Using AP Mode to Configure WiFI
When you first power up the device after flashing, it should auto launch as a open wireless AP. The SSID will be of the format "JBHASD-XXXXXXXX". 

After connecting to the SSID, you get captive-DNS dropped into a config page that lets you set two fields.. WiFI SSID and password. 

You then click/tap an apply button to save the config and reboot. The device should then enter STA mode after reboot and connect to your configured WiFI router. If you need to access the AP mode again, power-cycle the device and ground GPIO-0 within the first 5 seconds and it should re-enter AP mode. 

At this stage, all you have is a device that connects to your WiFI. It is not yet configured in terms of pin asignments for switch and sensor functions.

But you can now communicate to the device and see its status information. The device is also discoverable via DNS-SD. 

## Getting the Device JSON Status

You can use your WiFI router to tell you what IP was assigned but this project also includes a script to run a discovery of devices on your LAN.

```
python3 jbhasd/jbhad_discover.py

Discovered: JBHASD-0095EB30.local. URL: http://192.168.12.165:80/status
json len:575
{
  "name": "JBHASD-0095EB30",
  "zone": "Needs Setup",
  "wifi_ssid": "My WiFI SSID",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 0,
  "system": {
    "compile_date": "JBHASD-VERSION Jun 10 2019 23:58:14",
    "reset_reason": "Software/System restart",
    "free_heap": 19880,
    "chip_id": 9825072,
    "flash_id": 1327328,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 3660415620,
    "millis": 23894
  },
  "controls": []
}
```

The script uses zeroconf to locate services matching "_JBHASD._tcp.local." It outputs the URL of each discovered device and calls a GET on that URL showing the returned JSON state details. You will get the same JSON response with:

```
curl http://192.168.12.165:80/status
```

The section above for controls is where we would normally see details on any switches, sensors etc that are being managed by this device. Given this is a clean setup, no such detail is defined yet. Also the "configured" attribute of this device is set to 0 which confirms that no full device configuration has been pushed to this device.

## Pushing Configuration to the Device

The example below is how you would push configuration to a given device and set up it's GPIO pins to control any attached hardware or onboard features.

We first put a JSON config definition into a file device_config.json. It's contents are as follows:

```
{
   "boot_pin": 0,
    "manual_switches_enabled": 1,
    "mdns_enabled": 1,
    "ota_enabled": 1,
    "status_led_pin": 13,
    "telnet_enabled": 1,
    "wifi_password": "My WiFi Password",
    "wifi_ssid": "My WiFi SSID",
    "zone": "Sonoff Desktop Test",
    "controls": [
        {
            "enabled": 1,
            "name": "My Relay",
            "sw_led_pin": 13,
            "sw_man_pin": 0,
            "sw_mode": "toggle",
            "sw_relay_on_high": 1,
            "sw_relay_pin": 12,
            "sw_state": 0,
            "type": "switch"
        }
    ]
}
```

In the above example, the device in question is a Sonoff Basic switch. This device has a single relay for controlling mains appliances. GPIO-12 is the pin for this relay. There is also an onboard LED that is tied to GPIO-13 and an onboard button which is connected to GPIO-0. 

To push this configuration to the device, we POST to the /configure API function:

```
$ curl --header "Content-Type: application/json" --data @device_config.json http://192.168.12.165/configure
{
  "error": 0,
  "desc": "Configured Device successfully"
}
```

The response received is a JSON payload which includes an error value of 0 for success and 1 for failure. The description will give context to the sepcific failure at hand. The above is indicating that the device was successfully configured.


To summarise  top-level fields:
* boot_pin  
This configures the GPIO pin for the boot switch. This switch plays an important role when you with to reset the device config or change it's WiFI settings. When the device boots, you have 5 seconds to ground the assigned boot pin to put it into AP Mode. Otherwise it will connect in STA mode (client) after those 5 seconds elapse
* status_led_pin  
This pin defines an optional GPIO for use as a status LED. 
* manual_switches_enabled  
This field can be used to blanket-disable all manual switches configured on the device. Handy if it needs to be deployed where manual pushes on the buttons need to be avoided. This only applys to button pushes on configured switches and not the boot_pin.
* mdns_enabled  
This can be used to enable/disable MDNS and DNS-SD for the device. The default should be to leave this enabled as it will ensure the device can be discovered
* ota_enabled  
This enables Arduino OTA functionality and is a very handy way to flash updated firmware to devices. 
* telnet_enabled  
Telnet support is used for logging. If enabled, you can telned to the device IP and receive a live debug log of actitiy on the device. Telnet support will disable Serial logging once it activates. So in some cases when debugging, you may need to configure this option as disabled to ensure the device only logs to serial.
* wifi_ssid & wifi_password  
Pretty obvious what these are. They let you specify the credentials of the WiFi router you want to connect to. They can be omitted from the configuration call meaning the initial Wifi details will be preserved. 
* zone  
Zone defines a simple string to name the device location. It plays a role later on in organising devices on the web server dashboard

We're now at the controls array and what you can see is a JSON sub-object for a control named 'My Relay' of type 'switch'. The switch is in 'toggle' mode, meaning it's assigned manual pin toggles between on/off. The relay pin is set to 12, LED pin to 13 and manual pin to 0. 

So with that control configured, the device will boot and configure a switch called 'My Relay' that drives the onboard relay via GPIO-12 and match the on/off state of that relay with the onboard LED (GPIO-13). Pressing the Sonoff onboard flash button (GPIO-0) will act as a toggle-on and off button for the relay.

With the config sent up, the device validates the JSON, saves the config to EEPROM and reboots again, it will flash the status LED at a medium rate for 5 seconds giving you time to ground GPIO-0 (boot_pin) and put the device into AP Mode. If you activate that boot PIN during that initial 5 seconds, the LED flashing will go to a fast rate to indicate AP mode. 
If you leave that boot sequence alone, after 5 seconds, the initial medium flashing rate will drop to a slower rate to indicate that the WiFI connect stage has started and it will remain at that rate until it gets connected to the network. 

When it gets connected, it will issue a quick burst of flashes to indicate that the device has successfully conected to wifi and then turn off. 

Once it reboots again, the following JSON is returned when the device is probed.. 

```
$ curl 'http://192.168.12.165/status'
{
  "name": "JBHASD-0095EB30",
  "zone": "Sonoff Desktop Test",
  "wifi_ssid": "My WiFI SSID",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 1,
  "system": {
    "compile_date": "JBHASD-VERSION Jun 10 2019 23:58:14",
    "reset_reason": "Software/System restart",
    "free_heap": 19456,
    "chip_id": 9825072,
    "flash_id": 1327328,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 991115282,
    "millis": 1727191
  },
  "controls": [
    {
      "name": "My Relay",
      "type": "switch",
      "state": 0,
      "context": "init",
      "behaviour": "toggle",
      "motion_interval": 0,
      "manual_interval": 0,
      "last_activity_millis": 1134,
      "last_activity_delta_secs": 1726
    }
  ]
}
```

So now, we have a populated 'controls' array being returned that describes the available controls. This is crucial as it plays a role later on in how this device is then integrated with hub-type applications that need to discover capabilities and manipulate specific devices and controls. 

## Manipulating controls on the Device
To control the device from the network, we can POST JSON directives to the /control API function as follows:

```
curl -XPOST 'http://192.168.12.165/control' -d '
{
    "controls" : [
        {
            "name": "My Relay", 
            "state": 1
        }
    ]
}'
```

The JSON payload contains a "controls" array and has an control object per control we wish to manipulate. In this case we specified a single control called "My Relay" and set it's state to 1. This will cause the Sonoff device I'm using to turn on the rely being controlled by the "My Relay" control. 

The response I receive is simply the same JSON status you get from the /status API call and this status will now show the updated status of the targetted control. 

```
{
  "name": "JBHASD-0095EB30",
  "zone": "Sonoff Desktop Test",
  "wifi_ssid": "My WiFI SSID",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 1,
  "system": {
    "compile_date": "JBHASD-VERSION Jun 10 2019 23:58:14",
    "reset_reason": "Software/System restart",
    "free_heap": 19200,
    "chip_id": 9825072,
    "flash_id": 1327328,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 3285025550,
    "millis": 2686841
  },
  "controls": [
    {
      "name": "My Relay",
      "type": "switch",
      "state": 1,
      "context": "network",
      "behaviour": "toggle",
      "motion_interval": 0,
      "manual_interval": 0,
      "last_activity_millis": 2686840,
      "last_activity_delta_secs": 0
    }
  ]
}
```

To turn off that control, we just call the same function and set the state to 0:

```
curl -XPOST 'http://192.168.12.165/control' -d '
{
    "controls" : [
        {
            "name": "My Relay", 
            "state": 0
        }
    ]
}'
```

## Other API calls

Reboot the device:
```
curl 'http://192.168.12.165/reboot'
```

Reboot the device to AP Mode:
```
curl 'http://192.168.12.165/apmode'
```

Reset the device (wipe config):
```
curl 'http://192.168.12.165/reset'
```

Set the device for reconfiguring:
```
curl 'http://192.168.12.165/reconfigure'
```

To be continued..
