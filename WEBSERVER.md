# JBHASD Webserver
Python script jbhasd_web_server.py provides a webserver that can be used to provide a dashboard to monitor and control devices. 

It provides several features:
* Optional HTTP DIGEST authentication
* Sunset/Sunrise geo-awareness 
* Discovery of devices on the network
* 10-second probing of discovered devices to retain state of each device
* Rendering of the cached state into two dashboards.. Zone and Device
* Management of timers:
  - Turn on/off switches at desired times
  - Active/deactivate motion control for PIR-enabled switches at desired times
  - Apply timed programs to RGB and aRGB strips as desired times
* Device Config management
* External API for use with 3rd party automation such as IFTTT webhooks


For what started as a simple dashboard, it grew a lot of legs but it helps the solution to be quite useful in a home setting for both automating and managing the devices from a web page.

I'll go through everything but the best place to start is a demo that gets the sript up and running use a simulator to fake device. This helps demonstrate the environment.

# Demo With Simulated Devices
Prerequisite installed components:

OS package for Python3
gcc (may be used by pip3 when installing packages)

Python3 packages:
python-dateutil
zeroconf
cherrypy

Then to run the web server:
```
cd <your work dir>
git clone https://github.com/dresdner353/jbhasd.git
python3 jbhasd/jbhasd_web_server.py
```

.. if it works, point your browser as localhost:8080 or your machines IP:8080. If its working, 
you should see a page with a gray gradient background with a timestamp top-right. The first run of this script will also write a default config file to ~/.jbhasd_web_server

Then start the simulator on a separate terminal:
```
python3 jbhasd/jbhasd_device_sim.py
```

You can run the simulator on the same machine as the webserver or on a separate machine on the same network. This will start registering a fake device per US state with randomly added switches and sensors to each device. The controls are named after Irish counties and rivers.

The script will then advertise the fake devices on MDNS and DNS-SD. The webserver script will detect these simulated devices via zeroconf and start probing the /status API function. The web console page should refresh with widget panels being added for each discovered device. Each simulated device uses a webserver on port >= 9000 .. the ports are incremented as each new device is created. 

The console of each running script provides logging detail that should help understand 
what is then happening. 

# Webserver Architecture

The web server script is split into several separate threads that each perform a given function: 

* Config  
Loads config data from ~/.jbhasd_web_server.

* Device Discovery  
Discovers JBHASD devices on the network

* Device Status Probe  
Probes discovered devices every 10 seconds to track the status of each control and sensor

* Webserver  
Manages the dashoard rendering of all discovered devices and provides an API for external integration

We'll describe each of these now in more detail.

# Webserver Config  
On startup, the script tries to load JSON config data from ~/.jbhasd_web_server. If this file does not exist, it will create a defautl configuration and save it to that file.

Once up and running, the script monitors for changes to this file and tries to re-load the config. If it detects an issue parsing the JSON config, the reload will be ignored and the script will continue with old config until the file is again updated and can be parsed.

# Config File Format
The config detail is managed in a single JSON object stored in ~/.jbhasd_web_server. 

The model appears as follows:
```
{
    "dashboard" : {
        "box_width" : 210,
        "col_division_offset" : 25,
        "initial_num_columns" : 1
    },
    "discovery" : {
        "device_probe_interval" : 10,
        "device_purge_timeout" : 30,
        "zeroconf_refresh_interval" : 60
    },
    "sunset" : {
        "offset" : 1800,
        "url" : "http://api.sunrise-sunset.org/json?lat=53.349809&lng=-6.2624431&formatted=0"
    },
    "timezone" : "Europe/Dublin",
    "switch_timers" : [],
    "rgb_timers" : []
    "web" : {
        "port" : 8080,
        "users" : {
            "myusername" : "secret"
        }
    },
    "device_profiles" : {}
    "devices" : {}
}
```

The "dashboard" section defines the default width of each widget box. Additional fields are present for column division offsets and initial number of columns. This is in relation to how the dashboard lays itself out on screen by reacting to the detected browser page resolution. 

The "discovery" section controls how frequently zeroconf is used to detect new devices and how often each detected device is probed. The purge timeout is the max non-response time accrued before we delete the device from the disovered device list.

The "sunset" section uses the API from sunset.sunrise.org to determine the sunrise/sunset times, apply an offset and determine daily times for sunrise and sunset. This helps where device timers wish to reference keywords "sunset" or "sunrise" rather than absolute times. The defaults use the long/lat settings for Dublin, Ireland but can be customised to get accurate readings for your location.

The "timezone" field is used as part of time calculations to determine correct local times for timers. I put the setting here as the local Linux environment on a Raspberry Pi can be untrustworthy. Hence it was just easier to force the desired value here.

The "switch_timers" is a list of timer objects used to automate switches on the network. The "rgb_timers" list is the same approach but for RGB and aRGB strips.

The "device_profiles" & "devices" sections are dictionaries of device configuration templates and instances used to auto-provision devices on the network. 

The "web" section controls the listening port for the webserver and an optional dictionary of usernames and passwords. If that dictionary is left empty, HTTP DIGEST auth is disabled.


# Device Discovery  
The script uses zeroconf to discover the devices by their common "JBHASD" type attribute. 
It then establishes the device URL by combining IP and advertised port to form http://ip:port. 
This URL is added to a global set of discovered URLs. The thread for refresh then sleeps for "zeroconf_refresh_interval" seconds before it re-runs a zeroconf search.

# Device Status Probe  
Every 10 seconds (device_probe_interval), the script iterates the set of discovered device URLs and attempts to fetch that URL and capture the JSON status of the device. This is the capability discovery at work. If device contact is lost >= 30 seconds, the URL is purged from the set of discovered URLs.  

# Switch Timers

Below are examples of switch timers:
```
"switch_timers" : [
        {
            "off" : "01:00",
            "on" : "sunset",
            "control" : "Uplighter",
            "zone" : "Livingroom"
        },
        {
            "off" : "16:00",
            "on" : "12:00",
            "control" : "Uplighter",
            "zone" : "Playroom"
        },
        {
            "control" : "Desk Lamp",
            "zone" : "Office",
            "motion_on" : "sunset",
            "motion_off" : "sunrise",
            "motion_interval" : 300
        }

]
```
Each object defines the control name and zone that you wish to manage. Then the off and on times in hh:mm 24h format are local time and define when the device is turned on or off. 

Keywords "sunset" and "sunrise" are set to the values determined from API calls to sunrise-sunset.org. Both values are offset according to the configured "offset" value in seconds. For sunset, this offset is subtracted and then added to sunrise. So the effective values are an earlier sunset and delayed sunrise based on the offset in seconds.

For motion-enabled switches, you can set a "motion_on" and "motion_off" time and a desired "motion_interval". This will auto-set the controls "motion_interval" value when the time falls within the timer range and to 0 when the value falls outside of the range. Helps to control when motion control behaviour applies.


# Auto-Configuring of Devices
If a probed device returns a JSON status with top-level field "configured" set to 0, a configure device function is called to lookup the device by name and configure it accordingly. 

The device name is looked up in the "devices" section of config to obtain its configuration. If this is not found, we ignore the device. If the config is retrieved, we then retrieve referenced device profile and use both records to create a configuration dump to send to the device.

Example:

```
"device_profiles" : {
    "Sonoff S20" : {
        "boot_pin" : 0,
        "status_led_pin" : 13,
        "controls" : [
        {
            "name" : "Socket",
            "type" : "switch",
            "enabled" : 1,
            "relay_pin" : 12,
            "manual_pin" : 0
        },
        {
            "name" : "Green LED",
            "type" : "switch",
            "enabled" : 1,
            "led_pin" : 13
        }
        ]
    },
    .......
    .......
}

"devices" : {
    "JBHASD-009E91F8" : {
        "profile" : "Sonoff S20",
        "zone" : "Livingroom",
        "manual_switches_enabled" : 0,
        "controls" : [
            {
                "name" : "Socket",
                "enabled" : 1,
                "custom_name" : "Xbox"
            },
            {
                "name" : "Green LED",
                "enabled" : 0
            }
        ]
    },
    .....
    .....
    .....
}
```

The above example defines a profile for a Sonoff S20 with the boot pin, LED status and relay etc all defined acording to the device.

The devices dictionary entry for "JBHASD-009E91F8" then references the device profile and defines over-rides for the zone and other fields. The example above also disables the Green LED switch and renames the "Socket" switch to Xbox. 

When the unconfigured device is discovered and matched to teh above, the full config is created from the template and customisations and then pushed to the device.

# Zone Dashboard
When the main console page is accessed (IP:8080/zone) the script iterates the dictionary of discovered devices and generates a list of zones. For each zone, it then rescans the cached device status detail and organises all controls and sensors into a set per zone. The end result is that we render a widget on the web page per doscovered zone. Each zone widget hence shows all only for that given zone. 

So this is our dashboard. The approach to organising controls/sensors per zone is probably the most logical way to do this as we deploy with zone names like Livingroom, Kitchen and then place as many devices as required in a given zone. The control naming can then be as specific as required to put sense on the whole thing once rendered as a single widget per zone.

The dashboard is all generated code, CSS-based and templated. There is also jquery and Javascript code being generated to make the click and refresh magic do its thing.

If you click on a dashboard switch to toggle state, javascript code reacts to an onclick() event and issues a background GET passing the device name, switch name and desired state to the webserver. When the webserver detects the presence of these paramaters, it looks up the device to get its URL and then issues a POST request and JSON payload to the actual device to perform the desired on/off action. It captures the JSON response from the device and updates its register of JSON details. 

The rendered web page also uses a timed background refresh that refreshes the page seamlessly every "device_probe_interval" seconds. It uses the same jquery javascript approach to this so content just updates itself with no old-style page reload.

# Device Dashboard
The device console page (ip:8080/device) shows panel widgets for each device rather than grouping device controls into zones.

This variation is more verbose in that it provides and includes toggles that can be used to reboot individual devices or put them into AP Modes, access raw device JSON status or even issue reconfigure/reboot/apmode commands to devices. 

# Webserver API
The webserver is operating a basic API that can be used to direct it to turn on/off given switches or send programs to RGB and aRGB strips. While devices can be programmed directly, the purpose of the webserver API is to provide a single "hub" point to access rather than direct to device. It also serves the purpose of processing click actions from the dashboards.

Note: This API is GET-based for now but will be updated to a POST-based alternative in due course.

## Rebooting Devices
##### API: IP:port/api/?device=DDD&reboot=1
If the device value is set to "all", then all devices will be rebooted. Otherwise just the specified device name will be looked up and it's device API caled to invoke a reboot.

## Reconfiguring Devices 
##### API: IP:port/api/?device=DDD&reconfig=1
If the device is set to "all" then all devices are reconfigured. Otherwise just the specified name is looked up and its reconfigure API function is called.

## AP Mode
##### API IP:port/api/?device=DDD&apmode=1
This call can put all (device=all) or just a single named device into AP mode.

## Switch Control
##### API: IP:port/api/?device=DDD&zone=ZZZ&control=CCC&state=SSS
Specifies the device, zone and control to manipulate and the desired state to apply

## RGB/aRGB Control
##### API: IP:port/api/?device=DDD&zone=ZZZ&control=CCC&program=PPPPP
Same concept as controlling switches but uses a desired program instead to pass to the underlying device and change it RGB/aRGB program
