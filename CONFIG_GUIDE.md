# jbhasd Configuration Guide

This guide details all configuration options with examples.

## Top-level Configuration

The JSON configuration is made up of a series of top-level fields and then a controls array that defines the controls. The top-level config fields are as follows:

* wifi_ssid & wifi_password (string, mandatory)  
Pretty obvious what these are. They let you specify the credentials of the WiFi router you want to connect to. They can also be omitted from the configuration call meaning the existing WiFi details will be preserved. 

* zone  (string, optional)  
Zone defines a simple string to name the device location. It plays a role later on in organising devices on the web server dashboard. Controls from devices sharing the same zone are shown in a common panel widget. If omitted in configuration, the devuce assumes a default "Unknown" value.

* boot_pin (integer, mandatory)  
This configures the GPIO input pin for the boot switch. This switch plays an important role when you wish to reset the device config or change it's WiFI settings. When the device boots, you have 5 seconds to ground the assigned boot pin to put it into AP Mode. Otherwise it will connect in STA mode (client) after those 5 seconds elapse. The boot pin function ceases after the STA mode begins and thus the pin itself can be shared as an input pin with other controls if desired.

* status_led_pin (integer, optional)  
This pin defines an optional GPIO for use as a status LED. The initial 5-second boot sequence flashes the LED at a medium rate. If the boot_pin is grounded within that 5 seconds, AP mode is invoked and the LED flashes at a fast rate. If the boot sequence times out, the device enters STA mode and flashes at a slow rate until it connects to WiFi. Once connected, the status LED issues a fast burst of flashes and turns off. During normal usage, if the device detects WiFi down, the status LED once again starts to flash at a slow rate while it remains disconnected from WiFi and issues the short burst of flashes once it regains a connection. To disable the status LED, omit the field or by set to 255

* status_led_on_high (integer 0/1, optional, default 0)  
This option if set to 1 will use a HIGH state on the LED pin to turn it on. The default is to turn on the LED with LOW state. That default applies if the field is omitted.

* manual_switches_enabled (integer 0/1, optional, default 1)  
This field can be used to blanket-disable all manual switches configured on the device. Handy if it needs to be deployed where manual pushes on the buttons need to be avoided. This only applys to button pushes on configured switches and not the boot_pin. The same setting also disables motion control via PIR sensors. An alternative to using this option is to set the targetted switch to use PIN value 255 for the input (or omit that field) which essentially disables manual function on the switch.

* mdns_enabled (integer 0/1, optional, default 1)  
This can be used to enable/disable MDNS and DNS-SD for the device. The default is enabled as it will ensure the device can be discovered

* ota_enabled (integer 0/1, optional, default 1)  
This enables Arduino OTA functionality and is a very handy way to flash updated firmware to devices. The feature is enabled by default.

* telnet_enabled (integer 0/1, optional, default 1)  
Telnet support is used for logging. If enabled, you can telnet to the device IP and receive a live debug log of actitiy on the device. Telnet support will disable serial logging once it activates. So in some cases when debugging, you may need to configure this option as disabled to ensure the device only logs to serial.

* idle_period_wifi (integer <secs>, optional, default 0)  
Defines an optional idle period in seconds between /status API calls that triggers a restart of WiFi. This is used to provide a level of LAN health check by montitoring the continual calling of the /status API function. If this period expires, the WiFi is restarted as a means of forcefully re-establishing network connectivity. Set to 0 or omit the field to disable the feature.

* idle_period_reboot (integer <secs>, optional, default 0)  
Defines an optional idle period in seconds between /status API calls that triggers a reboot of the device. This is similar to the "idle_period_wifi" setting but will trigger a full reboot of the device if it fires. The recommendation is to set this value to a much larger value that that used for "idle_period_wifi". The net effect then after a prolonged absence /status calls will be several restarts of WiFi and ultimately the device reboots when the larger "idle_period_reboot" value is reached. Omit the field or set to 0 to disable the feature.

* configured (integer 0/1, internal)  
This field is automatically added to the configuration after the configure function is called. There is no need to use this field when configuring a device and in fact if you set it to 0, it will be over-ridden to 1 during the configure call.

* force_apmode_onboot (integer 0/1, internal)  
This field is used to temporarily force AP mode behaviour at the boot stage. It allows an active device to be told to reboot into AP Mode using the /apmode API call. Once that call is received, configuration is updated with force_apmode_onboot to 1 and rebooted. On reboot, it loads config, detects the setting, unsets the feature and re-saves config. Then it enters AP Mode. So the behaviour is a once-off boot into AP Mode. The field should not be set in normal configuration calls


## Controls Array

The controls array is used to define each attached control on the device. The following fields represent common fields on each control object:

* name (string, mandatory)  
The name of the specific control. If this field is omitted or set to empty string, the control is ignored.

* type (string, mandatory)  
This field defines the type of control being specified. The possible values are:

  - "switch"  
  This is a switch control used to drive the HIGH/LOW state of an output pin and turn on/off some attached peripheral.

  - "temp/humidity"  
  This is a temperature/humidity sensor that is used to track temp and humidity and make available for environmental monitoring

  - "rgb"  
  This is an RGB/PWM output device that is typically used to control single or strips of RGB LEDs. 

  - "argb"  
  This is an addressible RBG strip which can be programmed to run specific patterns


### Switch-specific fields

* relay_pin (integer, optional, default 255)
This is output relay pin for the switch. Actions in relation to turning the switch on and off will manipulate the HIGH/LOW state on this pin. If this field is omitted or set to 255, then the switch acts as a dummy switch.

* relay_pin_on_high (integer 0/1, optional, default 1)  
This field set the HIGH/LOW orientation of the output pin. The default of 1 means that the "on" state is HIGH and "off" state is LOW. Setting this field to 0 reverses this context. 

* led_pin (integer, optional, default 255)  
This defines a LED status pin for the switch to act as a visual indicator of the on/off state. If omitted or set to 255, the status LED is disabled.

* led_on_high (integer 0/1, optional, default 0)  
Defines the HIGH/LOW orientation of the LED pin. Defaults to 0.. on=LOW

* manual_pin (integer, optional, default 255)  
Defines an optional input pin to use as a manual on-device control for the switch. If omitted, this function is disabled

* manual_interval (integer, optional, default 0)  
This defines an optional interval in seconds that acts as a lock-out interval for the switch. Once the switch is manually activated (on or off), this interval is taken into acount and used to prevent a network on/off request from changing the switch state. The main purpose is for scenarios where an automated monitoring is configured to put the switch into an on/off state but a local manual over-ride applies if the switch is ativated manually. This function is disabled if omitted or set to 0.

* manual_auto_off (integer 0/1, optional, default 0)  
This option if set to 1, will cause the switch to turn off after the "manual_interval" seconds have elapsed. This may be used to create a simple timed "on" period for the given switch. If omitted, the switch will remain on indefinitely until it is manually turned off or a network API call turns off the switch.

* init_state (integer 0/1, optional, default 0)  
This is the initial state of the switch on boot. If omitted or set to 0, the default is that the switch is off when the device boots. Setting this to 1 will ensure that the control is on at boot time.

* manual_mode (string "toggle", "on" or "off", optional, default "toggle")  
This field defines the manual operation mode for the switch. The default is "toggle" which will simply toggle the switch between on and off states on each press of the manual input pin. If set to "on" a single press will always put the switch into an on-state, whereas a value of "off" will always act by putting the switch into the off state.

* motion_pin (integer, optional, default 255)  
Defines an optional input pin to use as a motion trigger for the switch. This works with PIR sensors. If omitted, this function is disabled. 

* motion_interval (integer, optional, default 0)  
This feature defines a timeout period in seconds for detected motion. If set to 0, it disables motion detection. Otherwise wen motion is detected, the switch is turned on and automatically off after the "motion_interval" expires. Network events to turn off the switch are ignored if the motion interval has not yet expired. 



### Temp/Humidity Sensor fields

* variant (string "DHT11", "DHT21", "DHT22", optional)  
This field defines the DHT sensor variant. By default it is assumed to be DHT11 if th field is omitted. 

* pin (integer, optional, default 255)  
This pin defines the GPIO pin for the DHT temp/humidity sensor. If omitted or set to 255, the sensor is configured as a dummy sensor with randomised values for temperature and humidity.

* temp_offset (float, optional, default 0)  
This field defines a negative/positive real number offset for the temperature sensor and can be used to correct inaccurate readings from sensors. Assumes a value of 0 if the field is omitted.


### RGB strip fields

* red_pin (integer, optional, default 255)  
Defines the GPIO pin for the red pin on the RGB strip. Can be omitted or set to 255 to disable.

* green_pin (integer, optional, default 255)  
Defines the GPIO pin for the green pin on the RGB strip. Can be omitted or set to 255 to disable.

* blue_pin (integer, optional, default 255)  
Defines the GPIO pin for the blue pin on the RGB strip. Can be omitted or set to 255 to disable.

* manual_pin (integer, optional, default 255)  
Defines an optional input pin to use as a manual on-device control for the RGB strip. When pressed it cycles the RGB program between a set of 10 presets. If omitted, this function is disabled.

* program (string, optional)  
This field specifies a colour transition program for the RGB strip. If omitted or set to "", the LED strip will be turned off. The program uses a comma-separated sequence of colour codes including transition and pause intervals.

The program takes the form : 
```
  <32-bit colour code>;<transition delay msecs>;<pause msecs>,<colour code>;<transition delay msecs>;<pause msecs>,..
```
The colour code can be a decimal or hex value (prefixed with 0x) and represents the RGB values of the colour to render. That RGB value occupies the lower three octets of the integer. The upper octet acts as a brightness control between 0-255 (0x00-0xFF) and can scale the brightness value of the rendered colour. If that upper octet is set to 0x00, it will render at full brightness.

The colour field can also be set to text "random" which will generate random values each time that color is selected in the program.

The transition delay is a period in milliseconds between each PWM change on the the given RGB. If set to 0, the colour change is instant and as this value is increased, you can create a graduated fade from the previous colour to the new colour. The larger this transition value becomes, the longer the transtion takes.

The pause field is used to place a stall in the program execution before the next colour is selected.

Examples:
```
"0xFF0000;10;0,0x00FF00;5;1000,0x0000FF;50;5000"
```
   ..starts with red using a 10ms transition delay and no pause. Then it fades to green with 5 msec delay, 1 second pause and finally to blue with the 50ms delay between transitons and a 5 second pause. The program then repeats on loop.


### Addressible RGB strip fields

* pin (integer, optional, default 255)  
Defines the GPIO pin for the aRGB pin on the RGB strip

* manual_pin (integer, optional, default 255)  
Defines an optional input pin to use as a manual on-device control for the aRGB strip. Currently does nothing (to be implemented at a future date)

* num_leds (integer, optional, default 0)  
Defines the number of addressible LEDs on the strip

* neopixel_flags (integer, optional, default 0)  
Defines flags to be passed to the Neopixel library and used in relation to activating various modes for supporting the aRGB strip variant. You need to consult Neopixel documentation to get details on values to use for the given strip variant at play.

* program (string, optional)  
This field specifies a display pattern for the aRGB strip along with offset, delay and fill mode settings. 

The program takes the form: 
```
<direction 0/1/-1>;<pause msecs>;<draw mode>;<colour>,<colour>,<colour>,,,,,
```
The first three fields are semi-colon separated followed by a final semi-colon and the a comma-separated list of colour values in decimal or hex (0x-prefixed). 

Direction defines the way the starting offset is managed bwtween executions of the program. If set to 0, it implies the first pixel drawn is always at the the start of the aRGB strip. If set to 1 or higher, the starting offset is incremented by 1 on each re-run. If direction is set to -1 or lower, the starting index is decremented by 1 on each run. These values modulo rotate on the strip offset positon and wrap around as they reach either end of the strip.

The pause period in msecs defines the delay between executions of the program

The draw mode defines the strip draw behaviour to be one of the following:
- 0 (wipe before each execution)   
The strip is populated with one execution of the colour sequence and wiped before each execution of the program. When used in conjunction with a direction value != 0, the effect is that the colour sequence appears to travel from one end of the strip to the other.

- 1 (fill)   
The full strip is instantly populated with repeated draws of the colour sequence. The colours appear in one go and if combined with a direction value != 0, then the sull strip pattern will appear to rotate on the strip.

- 2 (append)   
The program is written sequentially to the strip on each run of the program. When used with a pause value, it can create a gradual fill of the strip before it reaches the end and wipes the strip.


## Config Examples

The following are examples of configuration taken from the authors own home instalation. Each will be described in full.

### Sonoff S20 
```
{
    "boot_pin": 0,
    "controls": [
        {
            "enabled": 1,
            "manual_interval": 3600,
            "manual_pin": 0,
            "name": "Window Lights",
            "relay_pin": 12,
            "type": "switch"
        },
        {
            "enabled": 1,
            "led_pin": 13,
            "name": "Green LED",
            "type": "switch"
        }
    ],
    "profile": "Sonoff S20",
    "status_led_pin": 13,
    "wifi_password": "XXXXXXXX",
    "wifi_ssid": "WWWWWWWW",
    "zone": "Livingroom"
}
```

This is Sonoff S20 mains plug in the Livingroom zone with the onboard relay (GPIO12) tied to a control called "Window Lights". GPIO0 is wired to the onboard buton and serves as the boot pin and manual pin for the relay. The S20 has a blue LED that is tied into the relay and will turn on and off with the relay. There is also a green LED wired to GPIO13 and in this example, we have set it up as the status LED and as its own switch "Green LED" with no manual pin. So it is possible to turn on/off that green LED from the network.

Note: The profile field as shown above is not part of the config specification and wil be ignored by the software. In this regard, it or any other custom field would act like comments or notes.


### ESP 01
```
{
    "boot_pin": 0,
    "controls": [
        {
            "enabled": 1,
            "name": "Temp",
            "pin": 2,
            "temp_offset": -2.4,
            "type": "temp/humidity",
            "variant": "DHT21"
        }
    ],
    "profile": "ESP-01 + Temp Sensor",
    "wifi_password": "XXXXXXXX",
    "wifi_ssid": "WWWWWWWW",
    "zone": "Girls Room"
}
```
This is an ESP-01 that is configured with a single DHT21 temp/humidity sensor wired to GPIO2. Boot pin is wired to GPIO0 and no status LED has been defined. Temperature offset is also using -2.4 which will report temp in celcius but reduced by -2.4C


### Sonoff S20 with disabled switch
```
{
    "boot_pin": 0,
    "controls": [
        {
            "enabled": 1,
            "manual_interval": 3600,
            "manual_pin": 0,
            "name": "Uplighter",
            "relay_pin": 12,
            "type": "switch"
        },
        {
            "enabled": 0,
            "led_pin": 13,
            "name": "Green LED",
            "type": "switch"
        }
    ],
    "profile": "Sonoff S20",
    "status_led_pin": 13,
    "wifi_password": "XXXXXXXX",
    "wifi_ssid": "WWWWWWWW",
    "zone": "Playroom"
}
```
This is another S20 with the relay switch configured as "Uplighter" and the "Green LED" disabled (enabled: 0). This config when loaded will skip the configuration stage of the Green LED switch. This is handy if you wish to retain configuration but disable specific controls.

### Sonoff S20 with Temp Sensor
```
{
    "boot_pin": 0,
    "controls": [
        {
            "enabled": 1,
            "manual_pin": 0,
            "name": "KRK Monitors",
            "relay_pin": 12,
            "type": "switch"
        },
        {
            "enabled": 1,
            "name": "Temp",
            "pin": 1,
            "temp_offset": 0,
            "type": "temp/humidity",
            "variant": "DHT21"
        }
    ],
    "profile": "Sonoff S20 + Temp Sensor",
    "status_led_pin": 13,
    "wifi_password": "XXXXXXXX",
    "wifi_ssid": "WWWWWWWW",
    "zone": "Office"
}
```
The S20 does not have normal breakout GPIO pins. However the Tx/Rx GPIO pins used to serial program can be repurposed for external controls. In this case, the housing was modified to feed a cable with TRRS 2.5mm female jack from the units 4 break-out pins to a plug-in sensor. This gives us 3.3v DC support and 2 GPIO pins (1 & 3) for use with external sensors. Pin 1(Tx) is configured for a DHT21.


### Sonoff S20 with Motion PIR
```
{
    "boot_pin": 0,
    "controls": [
        {
            "enabled": 1,
            "manual_auto_off": 1,
            "manual_interval": 3600,
            "manual_pin": 0,
            "motion_interval": 60,
            "motion_pin": 3,
            "name": "Desk Lamp",
            "relay_pin": 12,
            "type": "switch"
        },
        {
            "enabled": 1,
            "name": "PIR Temp",
            "pin": 1,
            "type": "temp/humidity",
            "variant": "DHT21"
        }
    ],
    "profile": "Sonoff S20 + Temp & Motion Sensors",
    "status_led_pin": 13,
    "wifi_password": "XXXXXXXX",
    "wifi_ssid": "WWWWWWWW",
    "zone": "Office"
}
```
This is an enhanced S20 again with the 4 onboard pins wired to an external unit containing a 3.3v PIR sensor wired to GPIO3 and DHT21 wired to GPIO1. The PIR is tied to the motion pin of the "Desk Lamp" switch. The motion interval is set to 60 seconds and will turn on the "Desk Lamp" for 60 seconds after motion is detected. If the onboard button is pushed (GPIO0), this will turn on the same switch for 1 hour and turn off automatically (manual_auto_off: 1). 


### Huacanxing H801 LED strip controller
```
{
    "boot_pin": 0,
    "controls": [
        {
            "enabled": 0,
            "led_pin": 5,
            "name": "Red LED",
            "type": "switch"
        },
        {
            "blue_pin": 12,
            "enabled": 1,
            "green_pin": 13,
            "manual_pin": 0,
            "name": "Deer",
            "program": "random;1;500",
            "red_pin": 15,
            "type": "rgb"
        },
        {
            "blue_pin": 14,
            "enabled": 1,
            "name": "W1",
            "program": "0x00",
            "type": "rgb"
        },
        {
            "blue_pin": 4,
            "enabled": 1,
            "name": "W2",
            "program": "0x00",
            "type": "rgb"
        }
    ],
    "profile": "H801 RBG Controller",
    "status_led_pin": 1,
    "wifi_password": "XXXXXXXX",
    "wifi_ssid": "WWWWWWWW",
    "zone": "Garden"
}
```
The H801 is a wonderful example of an ESP8266 application. The device takes in 5-24V DC and can drive 5 PWM channels from 5-12V for LED strips. The device has no onboard programming button but has the usual 4 solder points for a pin header to program and a further 2 points for pins GPIO0 and GND. So you can solder your own headers and fit a programming button. 

The above example was for a Christmas wire-frame Deer in the Garden zone that I lit up with random colours. I modelled the controls as a single control that configured the red, green and blue pins and then created a further 2 RBG controls with only the blue pins defined to use up the other remaining PWM outputs. I was able to use these W1 and W2 controls to drive single colour strips.

the default boot program for the Deer was random colours, 1msec fade delay and 500ms pause between colours. That provides a pretty fast transition from one colour to the next.
