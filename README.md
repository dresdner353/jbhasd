# jbhasd 

#    "JSON-Based Home Automation with Service Discovery"


Just a pet project to go and build some home automation devices, turn on and off appliances, read a few sensors, have some fun and learn something along the way. 

There's some great existing options for home automation stacks out there and protocols such as MQTT. There are also some very powerful swissknife firmware sketches and LUA/Node-MCU scripts for the ESP-8266. 

However, none as far as I can tell offer the simplified discovery model I wanted to see. You follow the steps to integrate your sketch with monitoring tools and this whole plethora of steps and hard-coding of IP addresses takes place.

My objective was to deploy a device by putting it onto the local WiFI and then have it auto-discovered and instantly understood by a server. 

After some thought I settled on getting the device to enable mDNS and DNS-SD to allow the device to be discovered based on a sub-system keyword. Then a simple web serving of a JSON status string from the discovered device would be all the server needed to know the device details.. basically it's name, zone and what controls it has attached. The objective was to use that discovered JSON status to equip the server to render dashboards and other timing functions from the "hub" server.

So the combination of the use of JSON as API protocol along with DNS-SD ended up giving me the geeky name JBHASD. 

## Quick Summary of the Setup Steps
- The generic firmware is flashed to any ESP-8266 Device
- The device should power up in AP mode with SSID JBHASD-XXXXXXXX where XXXXXXXX is the CPU ID of the ESP8266
- You connect to the SSID and get brought to a web page where you can then select Wifi SSID and Password and apply changes
- The device then reboots and connects to your network in STA mode (WiFI client)
- You can then access the device with a JSON-based API and manage it from there
- MDNS and DNS-SD are built-in and an accompanying web server can be used as a hub for the devices providing a web portal, means of managing automation and even downloading config data to newly attached devices

## Using AP Mode to Configure WiFI
When you first power up the device after flashing, it should auto launch as a open wireless AP. The SSID will be of the format "JBHASD-XXXXXXXX". 

After connecting to the SSID, you get captive-DNS dropped into a config page that lets you set two fields.. WiFI SSID and password. 

You then click/tap the apply button to save the config and reboot. The device should then enter STA mode after reboot and connect to your configured WiFI router. If you need to access the AP mode again, power-cycle the device and ground GPIO-0 within the first 5 seconds and it should re-enter AP mode. 

At this stage, all you have is a device that connects to your WiFI. It is not yet configured in terms of pin asignments for switch and sensor functions.

But you can now communicate to the device and see its status information. The device is also discoverable via DNS-SD. 

## Getting the Device JSON Status

You can use your WiFI router to tell you what IP was assigned but this project also includes a script to run a discovery of devices on your LAN. Discovery is key here. So let's show that in action:

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

You can also reference the device by it's MDNS hostname:
```
curl http://JBHASD-0095EB30.local/status
```
The MDNS named approach will take longer as the IP resolution depends on a network broadcast for the given device to respond back with it's IP address.

The section above for controls is where we would normally see details on any switches, sensors etc that are being managed by this device. Given this is a clean setup, no such detail is defined yet. Also the "configured" attribute of this device is set to 0 which confirms that no full device configuration has been pushed to this device.

## Pushing Configuration to the Device

The example below is how you would POST to the /configure API function to upload the desired configuration. 

```
curl -XPOST 'http://192.168.12.165/configure' -d '
{
   "boot_pin": 0,
    "status_led_pin": 13,
    "zone": "Sonoff Desktop Test",
    "controls": [
        {
            "enabled": 1,
            "name": "My Relay",
            "led_pin": 13,
            "manual_pin": 0,
            "mode": "toggle",
            "relay_pin": 12,
            "type": "switch"
        }
    ]
}
'
```
In the above example, the device in question is a Sonoff Basic switch. This device has a single relay for controlling mains appliances. GPIO-12 is the pin for this relay. There is also an onboard LED that is tied to GPIO-13 and an onboard button which is connected to GPIO-0. 

The response to this call is:
```
{
  "error": 0,
  "desc": "Configured Device successfully"
}
```

The is a JSON payload which includes an error value of 0 for success and 1 for failure. The description will give context to the specific failure at hand. The above is indicating that the device was successfully configured.

With the config sent up, the device validates the JSON, saves the config to EEPROM and reboots again. It will now flash the status LED at a medium rate for 5 seconds giving you time to ground GPIO-0 (boot_pin) and put the device into AP Mode. If you activate that boot PIN during that initial 5 seconds, the LED flashing will go to a fast rate to indicate AP mode. 
If you leave that boot sequence alone, after 5 seconds, the initial medium flashing rate will drop to a slower rate to indicate that the WiFI connect stage has started and it will remain at that slow rate until it gets connected to the network. 

When it gets connected, it will issue a quick burst of flashes to indicate that the device has successfully conected to wifi and then turn off the status LED. 

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

## See Also..

[Configuration Guide](./CONFIG_GUIDE.md)  
[API Guide](./API.md)   
[Webserver Guide](./WEBSERVER.md)
