# jbhasd API Guide

## Status Function
This call is a GET or POST on the device URL and returns its current status and control details in JSON format. No actual parameter content is passed via URL fields or POST content. 

Example:
```
$ curl http://192.168.12.251/status
```
Response:
```
{
  "name": "JBHASD-00A30CCA",
  "zone": "Office",
  "wifi_ssid": "My Wifi SSID",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 1,
  "system": {
    "compile_date": "JBHASD-VERSION Jun 20 2019 14:30:19",
    "reset_reason": "Software/System restart",
    "free_heap": 19368,
    "chip_id": 10685642,
    "flash_id": 1327198,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 3316946957,
    "millis": 5204859
  },
  "controls": [
    {
      "name": "Desk Lamp",
      "type": "switch",
      "state": 0,
      "context": "init",
      "last_activity_millis": 5184584,
      "last_activity_delta_secs": 20,
      "motion_interval": 10,
      "manual_interval": 15,
      "manual_auto_off": 1
    },
    {
      "name": "Green LED",
      "type": "switch",
      "state": 0,
      "context": "init",
      "last_activity_millis": 6759,
      "last_activity_delta_secs": 5198
    },
    {
      "name": "PIR Temp",
      "type": "temp/humidity",
      "humidity": 53.4,
      "temp": 21.5
    }
  ]
}
```

## Reboot Function (/reboot)
The reboot function sets a trigger to force the device to reboot. Will work as a GET or POST call
```
curl 'http://192.168.12.251/reboot'
```

## AP Mode function (/apmode)
This forces a once-off reboot into AP mode. Very handy when you want AP Mode but are too lazy to go and manually restart the device. 
```
curl 'http://192.168.12.251/apmode'
```

## Reset function (/reset)
This call forces config to be over-written by a default config. The device will then reboot and enter AP mode.
```
curl 'http://192.168.12.251/reset'
```

## Reconfigure function (/reconfigure)
This function instructs the device to set it's configured property from 1 to 0. This is intended for use where you want an existing canned configuration to be pushed to the device via a network resource that is monitoring the devices. The call does not cause the device to reboot, update its stored configuration or act any differently other than advertise the configured property as 0. In fact if the device is rebooted, this configured property will revert to 1 if the device was previously in a configured state.
```
curl 'http://192.168.12.251/reconfigure'
```

## Configure function (/configure)
This function is where you push full config data to the device. 

For comprehensive details on this function see [Configuration Guide](./CONFIG_GUIDE.md)  

## Control Function (/control)
To control a device from the network, we can POST JSON directives to the /control API function and manipulate the onboard controls. 

We can start with an example of a Sonoff S20 with PIR sensor and temp/humidity control:

```
curl http://192.168.12.251/status
```
Response:
```
{
  "name": "JBHASD-00A30CCA",
  "zone": "Office",
  "wifi_ssid": "My Wifi SSID",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 1,
  "system": {
    "compile_date": "JBHASD-VERSION Jun 20 2019 14:30:19",
    "reset_reason": "Software/System restart",
    "free_heap": 19360,
    "chip_id": 10685642,
    "flash_id": 1327198,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 2388457467,
    "millis": 132564
  },
  "controls": [
    {
      "name": "Desk Lamp",
      "type": "switch",
      "state": 0,
      "context": "init",
      "last_activity_millis": 65,
      "last_activity_delta_secs": 132,
      "motion_interval": 0,
      "manual_interval": 3600,
      "manual_auto_off": 1
    },
    {
      "name": "Green LED",
      "type": "switch",
      "state": 0,
      "context": "init",
      "last_activity_millis": 65,
      "last_activity_delta_secs": 132
    },
    {
      "name": "PIR Temp",
      "type": "temp/humidity",
      "humidity": 55.1,
      "temp": 21.6
    }
  ]
}
```
The above status call shows us three controls on this device. Two switches and one temp/humidity sensor. 

### Turn On/Off Switches
To turn on the Desk Lamp control, we would send a JSON payload to the /control function. In that JSON payload would be a controls array with a single object specifying the name "Desk Lamp" and desired state of 1 (on).

```
curl -XPOST 'http://192.168.12.251/control' -d '
{
    "controls" : [
        {
            "name": "Desk Lamp", 
            "state": 1
        }
    ]
}
'
```
Response:
```
{
  "name": "JBHASD-00A30CCA",
  "zone": "Office",
  "wifi_ssid": "My Wifi SSID",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 1,
  "system": {
    "compile_date": "JBHASD-VERSION Jun 20 2019 14:30:19",
    "reset_reason": "Software/System restart",
    "free_heap": 19112,
    "chip_id": 10685642,
    "flash_id": 1327198,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 868132762,
    "millis": 274621
  },
  "controls": [
    {
      "name": "Desk Lamp",
      "type": "switch",
      "state": 1,
      "context": "network",
      "last_activity_millis": 274614,
      "last_activity_delta_secs": 0,
      "motion_interval": 0,
      "manual_interval": 3600,
      "manual_auto_off": 1
    },
    {
      "name": "Green LED",
      "type": "switch",
      "state": 0,
      "context": "init",
      "last_activity_millis": 65,
      "last_activity_delta_secs": 274
    },
    {
      "name": "PIR Temp",
      "type": "temp/humidity",
      "humidity": 55.5,
      "temp": 21.6
    }
  ]
}
```

The response JSON shown above lists the Desk Lamp control to be in the on state as the state field is now set to 1. 
Turning off this control is a simple case of sending the same payload but with the state set to 0:

```
curl -XPOST 'http://192.168.12.251/control' -d '
{
    "controls" : [
        {
            "name": "Desk Lamp", 
            "state": 0
        }
    ]
}
'
```
Response:
```
{
  "name": "JBHASD-00A30CCA",
  "zone": "Office",
  "wifi_ssid": "My Wifi SSID",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 1,
  "system": {
    "compile_date": "JBHASD-VERSION Jun 20 2019 14:30:19",
    "reset_reason": "Software/System restart",
    "free_heap": 18944,
    "chip_id": 10685642,
    "flash_id": 1327198,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 2362206508,
    "millis": 400671
  },
  "controls": [
    {
      "name": "Desk Lamp",
      "type": "switch",
      "state": 0,
      "context": "network",
      "last_activity_millis": 400664,
      "last_activity_delta_secs": 0,
      "motion_interval": 0,
      "manual_interval": 3600,
      "manual_auto_off": 1
    },
    {
      "name": "Green LED",
      "type": "switch",
      "state": 0,
      "context": "init",
      "last_activity_millis": 65,
      "last_activity_delta_secs": 400
    },
    {
      "name": "PIR Temp",
      "type": "temp/humidity",
      "humidity": 55.9,
      "temp": 21.7
    }
  ]
}
```

Note: The context field for the Desk Lamp is now showing "network" to indicate that it was put in this state by a network API call. The Green LED switch remains in "init" state as that was the state at boot time.

Note: The last_activity_millis and last_activity_delta_secs provide time references for the most recent switch activity. last_activity_millis is the value from the internal millis() counter and last_activity_delta_secs is the number of seconds elapsed which is based on (millis() - last_activity_millis) / 1000. From an analytic perspective, this provides a monitoring component a means of measuring activity on a switch.

The use of a JSON payload allows us to apply multiple changes in a single API call:

```
curl -XPOST 'http://192.168.12.251/control' -d '
{
    "controls" : [
        {
            "name": "Desk Lamp", 
            "state": 0
        },
        {
            "name": "Green LED", 
            "state": 1
        }
    ]
}
'
```
Response:
```
{
  "name": "JBHASD-00A30CCA",
  "zone": "Office",
  "wifi_ssid": "My Wifi SSID",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 1,
  "system": {
    "compile_date": "JBHASD-VERSION Jun 20 2019 14:30:19",
    "reset_reason": "Software/System restart",
    "free_heap": 18920,
    "chip_id": 10685642,
    "flash_id": 1327198,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 3124417881,
    "millis": 517573
  },
  "controls": [
    {
      "name": "Desk Lamp",
      "type": "switch",
      "state": 0,
      "context": "network",
      "last_activity_millis": 517566,
      "last_activity_delta_secs": 0,
      "motion_interval": 0,
      "manual_interval": 3600,
      "manual_auto_off": 1
    },
    {
      "name": "Green LED",
      "type": "switch",
      "state": 1,
      "context": "network",
      "last_activity_millis": 517566,
      "last_activity_delta_secs": 0
    },
    {
      "name": "PIR Temp",
      "type": "temp/humidity",
      "humidity": 56.2,
      "temp": 21.7
    }
  ]
}
```

The returned status then shows the Green LED in state 1 and Desk Lamp in state 0. Both context fields also set to "network". 

### Setting Motion Interval For PIR-enabled Switches
If the given switch has a configired PIR sensor (motion_pin), the "motion_interval" field will appear in it's status and it is possible to dynamically enable/disable that PIR based on setting this motion_interval field. 

In the above examples, the "Desk Lamp" has a PIR sensor and it's motion_interval field is set to 0. So this PIR is esentially disabled. We are ignoring it's signalling. 

To enable this sensor, we set the motion_interval to a desired number of seconds > 0.

```
curl -XPOST 'http://192.168.12.251/control' -d '
{
    "controls" : [
        {
            "name": "Desk Lamp", 
            "motion_interval": 10
        }
    ]
}
'
```
Response:
```
{
  "name": "JBHASD-00A30CCA",
  "zone": "Office",
  "wifi_ssid": "My Wifi SSID",
  "ota_enabled": 1,
  "telnet_enabled": 1,
  "mdns_enabled": 1,
  "manual_switches_enabled": 1,
  "configured": 1,
  "system": {
    "compile_date": "JBHASD-VERSION Jun 20 2019 14:30:19",
    "reset_reason": "Software/System restart",
    "free_heap": 19048,
    "chip_id": 10685642,
    "flash_id": 1327198,
    "flash_size": 1048576,
    "flash_real_size": 1048576,
    "flash_speed": 40000000,
    "cycle_count": 3367144558,
    "millis": 627981
  },
  "controls": [
    {
      "name": "Desk Lamp",
      "type": "switch",
      "state": 0,
      "context": "network",
      "last_activity_millis": 517566,
      "last_activity_delta_secs": 110,
      "motion_interval": 10,
      "manual_interval": 3600,
      "manual_auto_off": 1
    },
    {
      "name": "Green LED",
      "type": "switch",
      "state": 1,
      "context": "network",
      "last_activity_millis": 517566,
      "last_activity_delta_secs": 110
    },
    {
      "name": "PIR Temp",
      "type": "temp/humidity",
      "humidity": 56.3,
      "temp": 21.7
    }
  ]
}
```

If motion now trigers the switch, we will see this reflected in the state and context of the switch control:

```
...
...
    {
      "name": "Desk Lamp",
      "type": "switch",
      "state": 1,
      "context": "motion",
      "last_activity_millis": 741512,
      "last_activity_delta_secs": 3,
      "motion_interval": 10,
      "manual_interval": 3600,
      "manual_auto_off": 1
    },
...
...
```
The above shows the context set to "motion" and state of 1. So this control is turned on and due to a motion event. The time that happened was 741512 msecs according to the internal msec clock. That's not a whole lot of use to us but relatively speaking, that was determined to be 3 seconds ago (last_activity_delta_secs). 

Note: The Ardiono millis() range is an unsigned 32-bit int. That equates to about 49 days rotation. Unsigned arithmetic will also work if this time wraps around. So the delta calculation here will be accurate for intervals that are safely less than 49 days. The shorter the interval, the longer the accuracy of the delta calculation. 

Once that motion interval of 10 seconds expires and the switch is turned off, the state will update to 0 and context to "init":

```
...
...
    {
      "name": "Desk Lamp",
      "type": "switch",
      "state": 0,
      "context": "init",
      "last_activity_millis": 957153,
      "last_activity_delta_secs": 8,
      "motion_interval": 10,
      "manual_interval": 3600,
      "manual_auto_off": 1
    },
...
...
```
Motion can again be disabled by seting the interval to 0:
```
curl -XPOST 'http://192.168.12.251/control' -d '
{
    "controls" : [
        {
            "name": "Desk Lamp", 
            "motion_interval": 0
        }
    ]
}
'
```

### Setting Manual Interval For Switches
If the switch has a manual_pin defined in configuration, the status detail will include the settings for "manual_interval" and "manual_auto_off". In the same way as the above example, setting the property will return an updated JSON status showing that updated property.

To manipulate the manual interval, we POST and specify the desired interval for the given control:
```
curl -XPOST 'http://192.168.12.251/control' -d '
{
    "controls" : [
        {
            "name": "Desk Lamp", 
            "manual_interval": 30
        }
    ]
}
'
```
The above example sets an interval of 30 seconds after the last push of the manual button. What this does is block network calls to turn the "Desk Lamp" on or off for up to 30 seconds after the last manual update. So it doesn't matter if the control is on or off.. as long as it's in a manual context, it will block network attempts to manipulate it until that period has expired. This was designed to allow a control to be turned on or off and bypass any automated control that may bve in place.  

When a control is manually turned on or off, it's context will show the "manual" value:
```
    {
      "name": "Desk Lamp",
      "type": "switch",
      "state": 1,
      "context": "manual",
      "last_activity_millis": 1716166,
      "last_activity_delta_secs": 3,
      "motion_interval": 0,
      "manual_interval": 30,
      "manual_auto_off": 0
    },
```

When the manual interval expires, the control context is reset to "init" but the state is left in the current 1 or 0 value:

```
    {
      "name": "Desk Lamp",
      "type": "switch",
      "state": 1,
      "context": "init",
      "last_activity_millis": 1746166,
      "last_activity_delta_secs": 139,
      "motion_interval": 0,
      "manual_interval": 30,
      "manual_auto_off": 0
    },
```
This behaviour is essentially indicating to a monitoring resource that the enforced manual context no longer applies. Consequently, a network API call to turn on or the control will now succeed.


The manual interval can be further enhanced with a manual auto off setting. This simply creates a behaviour where the switch will automatically turn off after the manual interval has expired. It could be used for applications in temporary lighting or any kind of energy saving scenario.

This example configures auto off and a manual interval at the same time but can equally be enabled/disabled on it's own:

```
curl -XPOST 'http://192.168.12.251/control' -d '
{
    "controls" : [
        {
            "name": "Desk Lamp", 
            "manual_auto_off": 1,
            "manual_interval": 3600
        }
    ]
}
'
```

### Programming RGB Strips
RGB strips can by reprogrammed on demand by setting a new value for the program

```
curl -XPOST 'http://192.168.12.122/control' -d '
{
    "controls" : [
        {
            "name": "Deer", 
            "program": "0x0000FF;1;0,0x000000;1;0"
        }
    ]
}
'
```
The resulting JSON status response will show the updated program:

```
    {
      "name": "Deer",
      "type": "rgb",
      "program": "0x0000FF;1;0,0x000000;1;0",
      "current_colour": "0x000000FF",
      "step": 0
    },
```

That program will start with a transition to blue and fade to black and repeat in loop

Another example:
```
"program": "0xFFFFFF;0;2000,random;5;1000,0x000000;0;2000,0x00FF00;1;5000"
```
This program will start with full white shown for 2 seconds, then a gradual fade to a random colour, turn off for 2 seconds and then fast fade to green and repeat

### Programming Addressable RGB Strips
RGB strips can by reprogrammed on demand by setting a new value for the program. This is the same principle as used with programming of RGB strips except the program syntax is different.

```
curl -XPOST 'http://192.168.12.186/control' -d '
{
    "controls" : [
        {
            "name": "Front Door", 
            "program": "1;50;0;0x00,0x0A,0x14,0x1E,0x28,0x32,0x3C,0x46,0x50,0x5A,0x64,0x6E,0x78,0x82,0x8C,0x96,0xA0,0xAA,0xB4,0xBE,0xC8,0xD2,0xDC,0xE6,0xF0,0xFA"
        }
    ]
}
'
```
The resulting JSON status response will show the updated program:

```
    {
      "name": "Front Door",
      "type": "argb",
      "program": "1;50;0;0x00,0x0A,0x14,0x1E,0x28,0x32,0x3C,0x46,0x50,0x5A,0x64,0x6E,0x78,0x82,0x8C,0x96,0xA0,0xAA,0xB4,0xBE,0xC8,0xD2,0xDC,0xE6,0xF0,0xFA"
    }
```
The program format is 
```
<direction 0/1/-1>;<pause msecs>;<draw mode>;<colour>,<colour>,<colour>,,,,,
```

This program uses direction 1 which is forward direction.. incrementing the starting pixel from 0 after each redraw. The pause is 50 msecs between render of the sequence. The draw mode is 0 (wipe before execution).

So the end result will be that we draw this sequence of colours from the first pixel onward, pause for 5 msecs, clear the strip and draw the same sequence again but from one position forward. The effect is that the colour sequence will appear to travel along the strip and wrap around when it passes the end. 

The colour sequence is based on RGB in 24-bit form. All the values you see are < 0xFF. So this is a sequence of blue shades from black up to increasing levels of blue. What you get is a very slick gradiant of 26 shades of blue.

Another example:

```
curl -XPOST 'http://192.168.12.186/control' -d '
{
    "controls" : [
        {
            "name": "Front Door", 
            "program": "1;200;2;0xFF0000,0x00FF00,0x0000FF"
        }
    ]
}
'
```

This draws a red, green & blue sequence in mode 2 (append) with a 200 msec delay between draws. This will show the strip continually fill up with the red, green & blue sequence every 200 msecs until the stip is totally filled and then it is wiped and fills up again.

And an example using the fill mode:

```
curl -XPOST 'http://192.168.12.186/control' -d '
{
    "controls" : [
        {
            "name": "Front Door", 
            "program": "1;200;1;0xFF0000,0x00FF00"
        }
    ]
}
'
```

This program instantly fills up the strip with a sequence of Red and Green. In conjunction wuth the direction of 1, the effect is that one draw displays a red-green-red-green..... and the next green-red-green-red. So we get a rotating effect on the green-red pattern.
