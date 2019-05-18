// ESP-8266 Sketch for variations of devices
// using what I call the JBHASD "API"
// (Json-Based Home Automation with Service Discovery)
//
// Cormac Long September 2018

#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <DHT.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "HandyTaskMan.h"
#include "jbhasd_types.h"

HandyTaskMan TaskMan;

// Function: get_sw_context
// returns string for switch context enum 
// type
const char *get_sw_context(enum switch_state_context context)
{
    switch(context) {
      default:
      case SW_ST_CTXT_INIT:
        return "init";
        break;

      case SW_ST_CTXT_MANUAL:
        return "manual";
        break;

      case SW_ST_CTXT_NETWORK:
        return "network";
        break;

      case SW_ST_CTXT_MOTION:
        return "motion";
        break;
    }
}

// Function: get_sw_behaviour
// returns string for switch behaviour enum 
// type
const char *get_sw_behaviour(enum switch_behaviour behaviour)
{
    switch(behaviour) {
      default:
      case SW_BHVR_TOGGLE:
        return "toggle";
        break;

      case SW_BHVR_ON:
        return "on";
        break;

      case SW_BHVR_OFF:
        return "off";
        break;
    }
}

// Web server
ESP8266WebServer gv_web_server(WEB_PORT);
const byte DNS_PORT = 53;
IPAddress gv_ap_ip(192, 168, 1, 1);
IPAddress gv_sta_ip;
DNSServer gv_dns_server;
char gv_mdns_hostname[MAX_FIELD_LEN + MAX_FIELD_LEN];

// Global Data buffers
// In an effort to keep the RAM usage low
// It seems best to use a single large and two
// small buffers to do all string formatting for
// web pages and JSON strings
char gv_small_buffer[1024];
char gv_large_buffer[4096];
char gv_config[MAX_CONFIG_LEN];

// LOW/HIGH registers for GPIO states
// Used when turning on/off LEDs and relays
// depending on which variant applies
uint8_t gv_high_state_reg[] = { LOW, HIGH };
uint8_t gv_low_state_reg[] = { HIGH, LOW };

// Function start_serial
// Starts serial logging after
// checking GPIO pin usage
// across switches and sensors
void start_serial()
{
    if (!pin_in_use(3) &&  // Rx
        !pin_in_use(1)) {  // Tx
        gv_logging = LOGGING_SERIAL;
        Serial.begin(115200);
        delay(1000);
    }
}

// Telnet server
// Used for logging diversion from serial
// to connected clients
WiFiServer gv_telnet_server(23);
WiFiClient gv_telnet_clients[MAX_TELNET_CLIENTS];
uint8_t  gv_num_telnet_clients = 0;

// Function: vlog_message
// Wraps calls to Serial.print or connected
// telnet client
// takes form of typical vprintf-like function
// with va_list passed in args where upper
// function is managing he va_start/end
void vlog_message(char *format, va_list args )
{
    static char log_buf[LOGBUF_MAX + 1];
    uint8_t  i;
    uint8_t  prefix_len;
    uint32_t now;
    uint16_t days;
    uint16_t hours;
    uint16_t mins;
    uint16_t secs;
    uint16_t msecs;
    uint16_t remainder;
    
    if (gv_logging == LOGGING_NONE) {
        // Logging not enabled
        return;
    }

    if (gv_logging == LOGGING_NW_CLIENT &&
        gv_num_telnet_clients == 0) {
        // No logging to do if we have no
        // network clients
        return;
    }

    // Timestamp
    // Break down relative msecs
    // into days, hours. mins, secs & msecs
    now = millis();
    days = now / (1000 * 60 * 60 * 24);
    remainder = now % (1000 * 60 * 60 * 24);
    hours = remainder / (1000 * 60 * 60);
    remainder = remainder % (1000 * 60 * 60);
    mins = remainder / (1000 * 60);
    remainder = remainder % (1000 * 60);
    secs = remainder / 1000;
    msecs = remainder % 1000;

    // pre-write timestamp to log buffer
    ets_sprintf(log_buf,
                "%02u:%02u:%02u:%02u:%03u  ",
                days,
                hours,
                mins,
                secs,
                msecs);

    prefix_len = strlen(log_buf);

    // handle va arg list and write to buffer offset by 
    // existing timestamp length
    vsnprintf(log_buf + prefix_len,
              LOGBUF_MAX - prefix_len,
              format,
              args);
    log_buf[LOGBUF_MAX] = 0; // force terminate last character

    // CRLF termination
    strcat(log_buf, "\r\n");

    switch(gv_logging) {
      case LOGGING_SERIAL:
        Serial.print(log_buf);
        break;

      case LOGGING_NW_CLIENT:
        for(i = 0; i < MAX_TELNET_CLIENTS; i++) {
            if (gv_telnet_clients[i] && 
                gv_telnet_clients[i].connected()) {
                gv_telnet_clients[i].write((uint8_t*)log_buf, 
                                        strlen(log_buf));
                gv_telnet_clients[i].flush();
            }
        }
        break;
    }
}

// va_start/end wrapper for vlog_message()
// used directly in this code as a top
// level call point
void log_message(char *format, ... )
{
    va_list args;

    va_start(args, format);
    vlog_message(format, args);
    va_end(args);

}

// Function loop_task_telnet
// loop function for driving telnet
// session handling both accepting new
// sessions and flushing data from existing
// sessions
void loop_task_telnet()
{
    uint8_t i;

    if (gv_logging != LOGGING_NW_CLIENT) {
        return;
    }

    // check for new sessions
    if (gv_telnet_server.hasClient()) {
        for(i = 0; i < MAX_TELNET_CLIENTS; i++) {
            // find free/disconnected spot
            if (!gv_telnet_clients[i] || 
                !gv_telnet_clients[i].connected()) {
                if(gv_telnet_clients[i]) {
                    gv_telnet_clients[i].stop();
                    gv_num_telnet_clients--;
                }
                gv_telnet_clients[i] = gv_telnet_server.available();
                gv_num_telnet_clients++;
                ets_sprintf(gv_small_buffer,
                            "JBHASD Logging Console client %d/%d\r\n"
                            "Name:%s Zone:%s\r\n",
                            i + 1,
                            MAX_TELNET_CLIENTS,
                            gv_mdns_hostname,
                            gv_device.zone);
                gv_telnet_clients[i].write((uint8_t*)gv_small_buffer, 
                                        strlen(gv_small_buffer));
                continue;
            }
        }

        //no free/disconnected slot so reject
        WiFiClient serverClient = gv_telnet_server.available();
        ets_sprintf(gv_small_buffer,
                    "JBHASD %s Logging Console.. no available slots\r\n",
                    gv_mdns_hostname);
        serverClient.write((uint8_t*)gv_small_buffer, 
                           strlen(gv_small_buffer));
        serverClient.stop();
    }

    //check clients for data
    for (i = 0; i < MAX_TELNET_CLIENTS; i++) {
        if (gv_telnet_clients[i] && 
            gv_telnet_clients[i].connected()) {
            if(gv_telnet_clients[i].available()) {
                // throw away any data
                while(gv_telnet_clients[i].available()) {
                    gv_telnet_clients[i].read();
                }
            }
        }
    }
}

// Function start_telnet
// enables telnet server
void start_telnet()
{
    log_message("start_telnet()");

    if (!gv_device.telnet_enabled) {
        log_message("Telnet not enabled.. returning");
        return;
    }

    // start telnet server
    gv_telnet_server.begin();
    gv_telnet_server.setNoDelay(true);

    gv_logging = LOGGING_NW_CLIENT;

    // Telnet Sessions every 1 second
    TaskMan.add_task("Telnet Sessions",
                     RUN_STATE_WIFI_STA_UP,
                     1000,
                     loop_task_telnet);
}



// Function: set_switch_state
// Sets the desired switch state to the value of the state arg
void set_switch_state(struct gpio_switch *gpio_switch,
                      uint8_t state,
                      enum switch_state_context context)
{
    uint8_t relay_gpio_state, led_gpio_state;

    if (!gpio_switch) {
        // can get called with a find_switch() call
        // used for gpio_switch arg
        // So this can be NULL
        return;
    }

    log_message("set_switch_state(name=%s, state=%u, context=%d)",
                gpio_switch->name,
                state,
                context);

    // set state to 1 or 0
    // for safety because of array derefs
    // any non-0 value of state will be over-ridden to 1
    // else 0.
    if (state) {
        // on
        state = 1;
    }
    else {
        // off
        state = 0;
    }

    // Manual bypass scenario
    // trumps network or motion contexts
    if (gpio_switch->state_context == SW_ST_CTXT_MANUAL &&
        gpio_switch->manual_interval &&
        (context == SW_ST_CTXT_NETWORK ||
         context == SW_ST_CTXT_MOTION)) {
        log_message("Ignoring network/motion switch event.. currently in manual over-ride (%d secs)", 
                    gpio_switch->manual_interval);
        return;
    }

    // Motion bypass scenario
    // trumps only network
    if (gpio_switch->state_context == SW_ST_CTXT_MOTION &&
        gpio_switch->motion_interval &&
        context == SW_ST_CTXT_NETWORK) {
        log_message("Ignoring network switch event.. currently in motion over-ride (%d secs)", 
                    gpio_switch->motion_interval);
        return;
    }

    // change state as requested
    // Set the current state
    gpio_switch->current_state = state;
    gpio_switch->state_context = context;
    gpio_switch->last_activity = millis();

    // Determine the desired GPIO state to use
    // depending on whether on is HIGH or LOW
    if (gpio_switch->relay_on_high) {
        relay_gpio_state = gv_high_state_reg[state];
    }
    else {
        relay_gpio_state = gv_low_state_reg[state];
    }

    if (gpio_switch->led_on_high) {
        led_gpio_state = gv_high_state_reg[state];
    }
    else {
        led_gpio_state = gv_low_state_reg[state];
    }

    if (gpio_switch->relay_pin != NO_PIN) {
        digitalWrite(gpio_switch->relay_pin,
                     relay_gpio_state);
    }
    if (gpio_switch->led_pin != NO_PIN) {
        digitalWrite(gpio_switch->led_pin,
                     led_gpio_state);
    }
}

// Function: set_switch_motion_interval
// enables/disables motion control for a switch
// by setting an interval (seconds).. 0 is off
void set_switch_motion_interval(struct gpio_switch *gpio_switch,
                                uint32_t interval)
{
    if (!gpio_switch) {
        // can get called with a find_switch() call
        // used for gpio_switch arg
        // So this can be NULL
        return;
    }

    log_message("set_switch_motion_interval(name=%s, interval=%u)",
                gpio_switch->name,
                interval);

    // Sanity on value
    // min of 5 seconds or 0
    if (interval > 0 && interval < 5) {
        interval = 5;
    }

    gpio_switch->motion_interval = interval;
}

// Function: setup_switches
// Scans the list of configured switches
// and performs the required pin setups
void setup_switches()
{
    struct gpio_switch *gpio_switch;

    log_message("setup_switches()");

    for (gpio_switch = HTM_LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = HTM_LIST_NEXT(gpio_switch)) {

        log_message("Setting up switch:%s, state:%d",
                    gpio_switch->name,
                    gpio_switch->current_state);

        if (gpio_switch->relay_pin != NO_PIN) {
            log_message("    switch pin:%d",
                        gpio_switch->relay_pin);
            pinMode(gpio_switch->relay_pin, OUTPUT);
        }

        if (gpio_switch->led_pin != NO_PIN) {
            log_message("    LED pin:%d",
                        gpio_switch->led_pin);
            pinMode(gpio_switch->led_pin, OUTPUT);
        }

        if (gpio_switch->manual_pin != NO_PIN) {
            log_message("    Manual pin:%d",
                        gpio_switch->manual_pin);
            pinMode(gpio_switch->manual_pin, INPUT_PULLUP);
        }

        if (gpio_switch->motion_pin != NO_PIN) {
            log_message("    Motion pin:%d",
                        gpio_switch->motion_pin);
            pinMode(gpio_switch->motion_pin, INPUT_PULLUP);
        }

        // set initial state
        set_switch_state(gpio_switch,
                         gpio_switch->current_state,
                         SW_ST_CTXT_INIT);

    }
}

// Function: loop_task_check_switches
// Scans the input pins of all switches and
// invokes a toggle of the current state if it detects
// LOW state
void loop_task_check_switches()
{
    uint8_t button_state;
    uint8_t took_action = 0;
    static uint32_t last_action_timestamp = 0;
    WiFiClient wifi_client;
    struct gpio_switch *gpio_switch;
    struct gpio_rgb *gpio_rgb;
    char post_buffer[50];

    if (!gv_device.manual_switches_enabled) {
        return;
    }

    if (millis() - last_action_timestamp < 500) {
        // fast repeat switching bypassed
        // the loop calls this function every 200ms
        // that will ensure a rapid response to a switch
        // press but we don't want 10 actions per second
        // so as soon as a switch is pressed, we want 500 msecs
        // grace before we allow that again
        return;
    }

    for (gpio_switch = HTM_LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = HTM_LIST_NEXT(gpio_switch)) {

        // Only work with entries with a manual pin
        if (gpio_switch->manual_pin != NO_PIN) {
            button_state = digitalRead(gpio_switch->manual_pin);
            if (button_state == LOW) {
                log_message("Detected manual push on switch:%s pin:%d",
                            gpio_switch->name,
                            gpio_switch->manual_pin);

                switch(gpio_switch->switch_behaviour) {
                  default:
                  case SW_BHVR_TOGGLE:
                    // toggle state and treat as an action taken
                    set_switch_state(gpio_switch,
                                     (gpio_switch->current_state + 1) % 2,
                                     SW_ST_CTXT_MANUAL);
                    took_action = 1; // note any activity
                    break;

                  case SW_BHVR_ON:
                    // only allow switch to be turned on from off state
                    if (gpio_switch->current_state != 1) {
                        set_switch_state(gpio_switch,
                                         1, // On
                                         SW_ST_CTXT_MANUAL);
                        took_action = 1; // note any activity
                    }
                    break;

                  case SW_BHVR_OFF:
                    if (gpio_switch->current_state != 0) {
                        set_switch_state(gpio_switch,
                                         0, // Off
                                         SW_ST_CTXT_MANUAL);
                        took_action = 1; // note any activity
                    }
                    break;
                }
            }
            else {
                // no button press in play but check for expiry
                // of manual context or even auto-off
                if (gpio_switch->state_context == SW_ST_CTXT_MANUAL &&
                    gpio_switch->manual_interval > 0) {
                    if (millis() - gpio_switch->last_activity >= 
                        (gpio_switch->manual_interval * 1000)) {
                        log_message("Manual interval timeout (%u secs) on switch:%s",
                                    gpio_switch->manual_interval,
                                    gpio_switch->name);

                        // Can just turn off if this is set for 
                        // auto-off
                        if (gpio_switch->manual_auto_off) {
                            set_switch_state(gpio_switch,
                                             0, // Off
                                             SW_ST_CTXT_INIT);
                        }
                        else {
                            // Otherwise, we re-asset current state
                            // but let the context go to init
                            set_switch_state(gpio_switch,
                                             gpio_switch->current_state,
                                             SW_ST_CTXT_INIT);
                        }
                    }
                }
            }
        }

        // Motion pin (PIR)
        if (gpio_switch->motion_pin != NO_PIN &&
            gpio_switch->motion_interval) {
            button_state = digitalRead(gpio_switch->motion_pin);

            // Check for a trigger 
            if (button_state == HIGH) {
                log_message("Detected motion on switch:%s pin:%d",
                            gpio_switch->name,
                            gpio_switch->motion_pin);

                set_switch_state(gpio_switch,
                                 1, // On
                                 SW_ST_CTXT_MOTION);
                took_action = 1; // note any activity
            }
            else {
                // no motion detected.. see if we can turn it 
                // off
                if (gpio_switch->current_state == 1 &&
                    gpio_switch->state_context == SW_ST_CTXT_MOTION) {
                    if (millis() - gpio_switch->last_activity >= 
                        (gpio_switch->motion_interval * 1000)) {
                        log_message("Motion interval timeout (%u secs) on switch:%s",
                                    gpio_switch->motion_interval,
                                    gpio_switch->name);

                        // using INIT context to give over network 
                        // control 
                        set_switch_state(gpio_switch,
                                         0, // Off
                                         SW_ST_CTXT_INIT);
                    }
                }
            }
        }
    }
    
    for (gpio_rgb = HTM_LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = HTM_LIST_NEXT(gpio_rgb)) {

        // Only work with entries with a manual pin
        if (gpio_rgb->manual_pin != NO_PIN) {
            button_state = digitalRead(gpio_rgb->manual_pin);
            if (button_state == LOW) {
                log_message("Detected manual push on rgb:%s pin:%d",
                            gpio_rgb->name,
                            gpio_rgb->manual_pin);
                set_rgb_random_program(gpio_rgb);
                took_action = 1; // note any activity
            }
        }
    }

    if (took_action) {
        // record timestamp for fast
        // re-entry protection
        last_action_timestamp = millis();
    }
}

// Function: restore_wifi_led_state
// Restores state of WIFI LED to match
// it's assigned switch state if applicable
void restore_wifi_led_state()
{
    uint8_t found = 0;
    struct gpio_switch *gpio_switch;

    log_message("restore_wifi_led_state()");

    // Start by turning off
    if (gv_device.wifi_led_on_high) {
        digitalWrite(gv_device.wifi_led_pin,
                     gv_high_state_reg[0]);
    }
    else {
        digitalWrite(gv_device.wifi_led_pin,
                     gv_low_state_reg[0]);
    }

    // locate the switch by wifi LED pin in register
    for (gpio_switch = HTM_LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = HTM_LIST_NEXT(gpio_switch)) {
        if (gpio_switch->led_pin == gv_device.wifi_led_pin) {
            found = 1;
            log_message("found switch:%s state:%d using WIFI LED", 
                        gpio_switch->name,
                        gpio_switch->current_state);
            break;
        }
    }

    if (found) {
        // Set LED to the current state of matched
        // switch
        if (gv_device.wifi_led_on_high) {
            digitalWrite(gv_device.wifi_led_pin,
                         gv_high_state_reg[gpio_switch->current_state]);
        }
        else {
            digitalWrite(gv_device.wifi_led_pin,
                         gv_low_state_reg[gpio_switch->current_state]);
        }
    }
    else {
        log_message("no switch found assigned to wifi LED");
    }

}

// Function: setup_sensors
// Scans the in-memory array and configures the
// defined sensor pins
void setup_sensors()
{
    static uint8_t first_run = 1;
    struct gpio_sensor *gpio_sensor;
    DHT *dhtp;

    log_message("setup_sensors()");

    // Protect against multiple calls
    // can only really set these sensors up once
    // because of the pointer ref field
    // could try to get smart and call delete on set pointers
    // but its probably safer to just do this once.
    if (!first_run) {
        log_message("already setup (returning)");
        return;
    }
    first_run = 0;

    for (gpio_sensor = HTM_LIST_NEXT(gv_device.sensor_list);
         gpio_sensor != gv_device.sensor_list;
         gpio_sensor = HTM_LIST_NEXT(gpio_sensor)) {
        log_message("Setting up sensor %s",
                    gpio_sensor->name);

        switch (gpio_sensor->sensor_type) {
          case GP_SENS_TYPE_NONE:
            // do nothing
            log_message("    Unknown Type (dummy)");
            break;

          case GP_SENS_TYPE_DHT:
            log_message("    DHT Type %d on pin %d",
                        gpio_sensor->sensor_variant,
                        gpio_sensor->sensor_pin);

            if (gpio_sensor->sensor_pin != NO_PIN) {
                // Setup DHT temp/humidity sensor and record
                // class pointer in void* ref
                dhtp = new DHT(gpio_sensor->sensor_pin,
                               gpio_sensor->sensor_variant);
                gpio_sensor->ref = dhtp;
            }
            else {
                log_message("    Sensor not assigned to pin (fake)");
                // non-pin assigned DHT
                // for faking/simulation
                gpio_sensor->ref = NULL;
            }
            break;
        }
    }
}

// Function: float_get_fp
// Returns floating point part of float
// as integer. Needed due to limitations of
// formatting where it cant handle %f in ets_sprintf
uint32_t float_get_fp(float f, uint8_t precision) {

    int32_t f_int;
    uint32_t f_fp;
    double pwr_of_ten;

    // Calculate power of ten for precision
    pwr_of_ten = pow(10, precision);

    // Integer part
    f_int = (int)f;

    // decimal part
    if (f_int < 0) {
        f_fp = (int) (pwr_of_ten * -1 * f) % (int)pwr_of_ten;
    } else {
        f_fp = (int) (pwr_of_ten * f) % (int)pwr_of_ten;
    }

    return f_fp;
}

// Function read_sensors()
// Read sensor information
void read_sensors()
{
    DHT *dhtp;
    float f1, f2;
    struct gpio_sensor *gpio_sensor;

    log_message("read_sensors()");

    for (gpio_sensor = HTM_LIST_NEXT(gv_device.sensor_list);
         gpio_sensor != gv_device.sensor_list;
         gpio_sensor = HTM_LIST_NEXT(gpio_sensor)) {
        switch (gpio_sensor->sensor_type) {
          case GP_SENS_TYPE_DHT:
            dhtp = (DHT*)gpio_sensor->ref;

            if (gpio_sensor->sensor_pin != NO_PIN) {
                // Humidity
                f1 = dhtp->readHumidity();
                if (isnan(f1)) {
                    log_message("  Humidity sensor read failed");
                }
                else {
                    gpio_sensor->f1 = f1;
                }

                // Temp Celsius
                f2 = dhtp->readTemperature();
                if (isnan(f2)) {
                    log_message("Temperature sensor read failed");
                }
                else {
                    // record temp as read value offset
                    // by temp_offset in config
                    gpio_sensor->f2 = f2 +
                        gpio_sensor->temp_offset;
                }
            }
            else {
                // fake the values
                gpio_sensor->f1 = (ESP.getCycleCount() % 100) + 0.5;
                gpio_sensor->f2 = ((ESP.getCycleCount() +
                                    ESP.getFreeHeap()) % 100) + 0.25;
            }
            log_message("Sensor:%s "
                        "Humidity:%d.%02d "
                        "Temperature:%d.%02d "
                        "(temp offset:%d.%02d)",
                        gpio_sensor->name,
                        (int)gpio_sensor->f1,
                        float_get_fp(gpio_sensor->f1, 2),
                        (int)gpio_sensor->f2,
                        float_get_fp(gpio_sensor->f2, 2),
                        (int)gpio_sensor->temp_offset,
                        float_get_fp(gpio_sensor->temp_offset, 2));
            break;
        }
    }
}

// Function parse_rgb_colour
// Parses hue, red, green and blue
// values from 4-octet int
// Then applies MAX_PWM_VALUE against
// 0-255 ranges of RGB to render into the PWM
// range of the ESP-8266 (0-1023)
// Finally optional hue value applied against
// RGB values to act as a brightness affect on
// the values
void parse_rgb_colour(uint32_t colour,
                      uint16_t &red,
                      uint16_t &green,
                      uint16_t &blue)
{
    float brightness_factor;
    uint8_t brightness;

    log_message("parse_rgb_colour(0x%08X)", colour);

    // separate out brightness from most significant 
    // octet and RGB from lower 3 octets
    brightness = (colour >> 24) & 0xFF;
    red = (colour >> 16) & 0xFF;
    green = (colour >> 8) & 0xFF;
    blue = colour & 0xFF;
    log_message("Decoded RGB.. Brightness:0x%02X Red:0x%02X Green:0x%02X Blue:0x%02X",
                brightness,
                red,
                green,
                blue);

    // apply PWM.. scales from 0..255 to 0..1023
    red = red * MAX_PWM_VALUE / 255;
    green = green * MAX_PWM_VALUE / 255;
    blue = blue * MAX_PWM_VALUE / 255;

    log_message("Applied PWM.. Red:%d Green:%d Blue:%d",
                red,
                green,
                blue);

    // Apply optional brightness modification
    // value 1-255 is rendered into
    // a fraction of 255 and multiplied against the
    // RGB values to scale them accordingly
    // A brightness of 0 is equivalent to a value of 255
    // in the way we treat it as optional
    if (brightness > 0) {
        brightness_factor = float(brightness) / 255;
        red = float(red) * brightness_factor;
        green = float(green) * brightness_factor;
        blue = float(blue) * brightness_factor;

        log_message("Applied Brightness.. Red:%d Green:%d Blue:%d",
                    red,
                    green,
                    blue);
    }
}

// Function shift_rgb
// Shifts RGB values for start_red,
// start_green & start_blue one notch
// each toward the end values
// Used to apply a fading effect on values
void shift_rgb(uint16_t &start_red,
               uint16_t &start_green,
               uint16_t &start_blue,
               uint16_t end_red,
               uint16_t end_green,
               uint16_t end_blue)
{
    if (start_red < end_red) {
        start_red++;
    }
    else if (start_red > end_red) {
        start_red--;
    }

    if (start_green < end_green) {
        start_green++;
    }
    else if (start_green > end_green) {
        start_green--;
    }

    if (start_blue < end_blue) {
        start_blue++;
    }
    else if (start_blue > end_blue) {
        start_blue--;
    }
}

// Function: fade_rgb
// Takes a gpio_rgb object
// and applies a fade step
// toward a new colour setting
void fade_rgb(struct gpio_rgb *gpio_rgb)
{
    uint32_t now;

    if (gpio_rgb->fade_delay <= 0) {
        // instant switch to new setting
        log_message("Instant change to.. Red:%d Green:%d Blue:%d",
                    gpio_rgb->desired_states[0],
                    gpio_rgb->desired_states[1],
                    gpio_rgb->desired_states[2]);

        // write changes to active pins
        if (gpio_rgb->red_pin != NO_PIN){
            analogWrite(gpio_rgb->red_pin,
                        gpio_rgb->desired_states[0]);
        }
        if (gpio_rgb->green_pin != NO_PIN){
            analogWrite(gpio_rgb->green_pin,
                        gpio_rgb->desired_states[1]);
        }
        if (gpio_rgb->blue_pin != NO_PIN){
            analogWrite(gpio_rgb->blue_pin,
                        gpio_rgb->desired_states[2]);
        }

        // Update states
        gpio_rgb->current_states[0] = gpio_rgb->desired_states[0];
        gpio_rgb->current_states[1] = gpio_rgb->desired_states[1];
        gpio_rgb->current_states[2] = gpio_rgb->desired_states[2];
    }
    else {
        // delay mechanism
        // require elapsed msecs to match configured 
        // fade delay
        now = millis();
        if (gpio_rgb->fade_delay > 0 &&
            now - gpio_rgb->timestamp < gpio_rgb->fade_delay) {
            return;
        }

        // timestamp activity
        gpio_rgb->timestamp = now;

        // shift all three RGB values 1 PWM value
        // toward the desired states
        shift_rgb(gpio_rgb->current_states[0],
                  gpio_rgb->current_states[1],
                  gpio_rgb->current_states[2],
                  gpio_rgb->desired_states[0],
                  gpio_rgb->desired_states[1],
                  gpio_rgb->desired_states[2]);

        log_message("RGB Step.. Timestamp:%lu Delay:%d R:%d G:%d B:%d -> R:%d G:%d B:%d",
                    gpio_rgb->timestamp,
                    gpio_rgb->fade_delay,
                    gpio_rgb->current_states[0],
                    gpio_rgb->current_states[1],
                    gpio_rgb->current_states[2],
                    gpio_rgb->desired_states[0],
                    gpio_rgb->desired_states[1],
                    gpio_rgb->desired_states[2]);

        // write changes to pins
        // We're testing for the PIN assignment here
        // to allow for a scenario where only some
        // or one of the pins are set. This caters for
        // custom applications of dimming single colour scenarios
        // or assigning three separate dimmable LEDs to a single
        // device
        if (gpio_rgb->red_pin != NO_PIN){
            analogWrite(gpio_rgb->red_pin,
                        gpio_rgb->current_states[0]);
        }
        if (gpio_rgb->green_pin != NO_PIN){
            analogWrite(gpio_rgb->green_pin,
                        gpio_rgb->current_states[1]);
        }
        if (gpio_rgb->blue_pin != NO_PIN){
            analogWrite(gpio_rgb->blue_pin,
                        gpio_rgb->current_states[2]);
        }
    }
}

// Function loop_task_transition_rgb()
// Checks active LED devices and
// progresses to next step in program
// or applies transitions to existing step
void loop_task_transition_rgb()
{
    struct gpio_rgb *gpio_rgb;

    for (gpio_rgb = HTM_LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = HTM_LIST_NEXT(gpio_rgb)) {
        if (strlen(gpio_rgb->program) > 0) {
            // Fade color if the current and desired states 
            // are not yet aligned
            if ((gpio_rgb->desired_states[0] !=
                 gpio_rgb->current_states[0] ||
                 gpio_rgb->desired_states[1] !=
                 gpio_rgb->current_states[1] ||
                 gpio_rgb->desired_states[2] !=
                 gpio_rgb->current_states[2])) {
                fade_rgb(gpio_rgb);
            }
            else if (!gpio_rgb->single_step) {
                // Only transition to next colour if 
                // we have not determined the program is 
                // a single step
                set_rgb_state(gpio_rgb);
            }
        }
    }
}

// Function set_rgb_program
// Program string takes the format
// <colour>;<fade delay>;<pause>,<colour>;<fade delay>;<pause>,...
// So each step is a semi-colon separated trip of colour, fade and pause
// Steps are then comma-separated.
// The fade and pause args can be omitted
void set_rgb_program(struct gpio_rgb *gpio_rgb,
                     const char *program)
{
    char step_buffer[50];

    if (!gpio_rgb) {
        log_message("No led specified");
        return;
    }

    log_message("set_rgb_program(name=%s, program=%s)",
                gpio_rgb->name,
                program);

    gpio_rgb->timestamp = 0;

    // copy in program string
    // if its not a pointer to itself
    if (gpio_rgb->program != program) {
        strcpy(gpio_rgb->program, program);
    }
    gpio_rgb->program_ptr = NULL;
    gpio_rgb->step = -1;
    gpio_rgb->single_step = 0;

    // nudge into motion
    set_rgb_state(gpio_rgb);
}


// Function set_rgb_random_program
// Generates a simple random program for the given RGB
void set_rgb_random_program(struct gpio_rgb *gpio_rgb)
{
    static uint8_t variant = 0;

    log_message("set_rgb_random_program(name=%s, variant=%d)",
                gpio_rgb->name, 
                variant);

    switch(variant) {
      case 0:
        // White fixed
        strcpy(gv_small_buffer, "0xFFFFFF");
        break;

      case 1:
        // Red
        strcpy(gv_small_buffer, "0xFF0000");
        break;

      case 2:
        // Green
        strcpy(gv_small_buffer, "0x00FF00");
        break;

      case 3:
        // Blue
        strcpy(gv_small_buffer, "0x0000FF");
        break;

      case 4:
        // Random 1 second
        // no fade
        strcpy(gv_small_buffer, "random;0;1000");
        break;

      case 5:
        // Random 1 second
        // 3ms fade
        strcpy(gv_small_buffer, "random;3;1000");
        break;

      case 6:
        // Random 200ms
        // no fade
        strcpy(gv_small_buffer, "random;0;200");
        break;

      case 7:
        // Random 200ms
        // 1ms fade
        strcpy(gv_small_buffer, "random;1;200");
        break;

      case 8:
        // RGB cycle
        // 10ms fade
        strcpy(gv_small_buffer, "0xFF0000;10;0,0x00FF00;10;0,0x0000FF;10;0");
        break;

      case 9:
        strcpy(gv_small_buffer, "0x000000");
        // Off
    }

    set_rgb_program(gpio_rgb, gv_small_buffer);

    // rotate between 10 variants
    variant = (variant + 1) % 10;
}


// Function: set_rgb_state
// Sets the LED to its next/first program step
// also applies msec interval counting for pauses
// between program steps
void set_rgb_state(struct gpio_rgb *gpio_rgb)
{
    uint16_t end_red, end_green, end_blue;
    uint32_t now;
    char *p, *q; 
    char step_buffer[50];

    log_message("set_rgb_state(name=%s)",
                gpio_rgb->name);

    if (strlen(gpio_rgb->program) == 0) {
        log_message("program is empty.. nothing to do");
        return;
    }

    // Pause behaviour from previous step
    // applies only if we're actually running a program
    // So we'll be on step >=0 (not -1)
    // The current step will also have to have an assigned
    // pause period. We then just apply that interval in 
    // timestamp msec motion before allowing us move to the next
    // step
    now = millis();
    if (gpio_rgb->step >= 0 &&
        gpio_rgb->pause > 0 &&
        now - gpio_rgb->timestamp < gpio_rgb->pause) {
        return;
    }

    // Program pointer and step init/reset
    // This is performed after the above pause check so 
    // that we gonour the pause behaviour of the last step
    // before program reset
    if (gpio_rgb->program_ptr == NULL) {
        gpio_rgb->program_ptr = gpio_rgb->program;
        gpio_rgb->step = -1;
    }

    // timestamp activity
    gpio_rgb->timestamp = now;

    // Extract next full step in program
    // colour;fade_delay;pause,colour;fade_delay;pause ... 
    // comma is separator between steps
    p = strchr(gpio_rgb->program_ptr, ',');
    if (p) {
        // extract characters from this step
        strncpy(step_buffer, 
                gpio_rgb->program_ptr, 
                p - gpio_rgb->program_ptr);
        step_buffer[p - gpio_rgb->program_ptr] = '\0';

        // skip to next field
        gpio_rgb->program_ptr = p + 1;
    }
    else {
        // no more steps
        // copy what is there 
        // return pointer to start of program
        strcpy(step_buffer, gpio_rgb->program_ptr);

        // Detect single-step programs
        // we found no step separator in the above check
        // So if the program pointer is pointing
        // to the start, then the entire program is  
        // then a single step.
        // But if that single step uses the random keyword
        // we dont treat it as a single step 
        // because it changes each time its run
        if (gpio_rgb->program_ptr == gpio_rgb->program &&
            strncmp(step_buffer, "random", 5) != 0) {
            gpio_rgb->single_step = 1;
        }

        gpio_rgb->program_ptr = NULL; // will trigger reset
    }

    gpio_rgb->step++;

    // search for ; terminator between colour 
    // and fade delay
    p = strchr(step_buffer, ';');
    if (p) {
        // NULL terminator and move p on 1 char
        // gives us two strings
        *p = '\0';
        p++;

        gpio_rgb->fade_delay = atoi(p);

        // Next separator is for pause
        // same NULL trick
        q = strchr(p, ';');
        if (q) {
            *q = '\0';
            q++;
            gpio_rgb->pause = atoi(q);
        }
        else {
            // no separator, value taken as 0
            gpio_rgb->pause = 0;
        }
    }
    else {
        // no separator, value taken as 0
        gpio_rgb->fade_delay = 0;
    }

    // Extract colour value
    // sensitive to hex and decimal
    // and also keyword random
    if (!strncmp(step_buffer, "random", 5)) {
        gpio_rgb->current_colour = random(0, 0xFFFFFF);
    }
    else if (strlen(step_buffer) > 2 &&
        step_buffer[0] == '0' &&
        (step_buffer[1] == 'x' || step_buffer[1] == 'X')) {
        // hex decode
        gpio_rgb->current_colour = 
            strtoul(&step_buffer[2], NULL, 16);
    }
    else {
        // decimal unsigned int
        gpio_rgb->current_colour = 
            strtoul(step_buffer, NULL, 10);
    }

    log_message("Decoded step[%d] colour:0x%08X fade delay:%d pause:%d",
                gpio_rgb->step,
                gpio_rgb->current_colour,
                gpio_rgb->fade_delay,
                gpio_rgb->pause);


    // parse the desired state into PWM
    // values
    parse_rgb_colour(gpio_rgb->current_colour,
                     end_red,
                     end_green,
                     end_blue);

    // populate into desired state array
    gpio_rgb->desired_states[0] = end_red;
    gpio_rgb->desired_states[1] = end_green;
    gpio_rgb->desired_states[2] = end_blue;
}

// Function: setup_rgbs
// Scans the in-memory array and configures the defined led
// pins including initial values
void setup_rgbs()
{
    struct gpio_rgb *gpio_rgb;
    uint16_t rgb_count = 0;

    log_message("setup_rgbs()");

    for (gpio_rgb = HTM_LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = HTM_LIST_NEXT(gpio_rgb)) {

        rgb_count++;

        gpio_rgb->current_colour = 0;

        set_rgb_program(gpio_rgb,
                        gpio_rgb->program);

        log_message("Setting up RGB:%s, initial value:%d",
                    gpio_rgb->name,
                    gpio_rgb->current_colour);

        if (gpio_rgb->red_pin != NO_PIN) {
            log_message("    LED Red pin:%d",
                        gpio_rgb->red_pin);
            pinMode(gpio_rgb->red_pin, OUTPUT);
            analogWrite(gpio_rgb->red_pin, 0);
        }
        if (gpio_rgb->green_pin != NO_PIN) {
            log_message("    LED Green pin:%d",
                        gpio_rgb->green_pin);
            pinMode(gpio_rgb->green_pin, OUTPUT);
            analogWrite(gpio_rgb->green_pin, 0);
        }
        if (gpio_rgb->blue_pin != NO_PIN) {
            log_message("    LED Blue pin:%d",
                        gpio_rgb->blue_pin);
            pinMode(gpio_rgb->blue_pin, OUTPUT);
            analogWrite(gpio_rgb->blue_pin, 0);
        }
        if (gpio_rgb->manual_pin != NO_PIN) {
            log_message("    Manual pin:%d",
                        gpio_rgb->manual_pin);
            pinMode(gpio_rgb->manual_pin, INPUT_PULLUP);
        }
    }

    if (rgb_count > 0) {
        // LED Transtions
        // Uses 1msec delay and actual longer transitions
        // are handled internally. 
        // Also runs in both STA modes
        // and init mode ensuring LEDs start working right
        // away at boot time even during the 5-sec AP mode
        // wait
        TaskMan.add_task("PWM LED Transitions",
                         RUN_STATE_WIFI_STA_UP |
                         RUN_STATE_WIFI_STA_DOWN |
                         RUN_STATE_INIT,
                         1,
                         loop_task_transition_rgb);
    }
}

// Function set_argb_state
// Drives aRGB program by applying the 
// program list of colours to the addressable LED
// strip
void set_argb_state(struct gpio_argb *gpio_argb)
{
    uint32_t now;
    uint32_t colour;
    uint8_t red, green, blue;
    char colour_buf[12];
    char *p, *q;
    uint16_t led_count;
    uint32_t i;
    uint32_t start_index;
    uint8_t loop;
    uint8_t wipe = 0;

    if (gpio_argb->program_start == NULL) {
        log_message("program is empty.. nothing to do");
        return;
    }

    // delay early return until delay
    // msec period reached
    now = millis();
    if (now - gpio_argb->timestamp < 
        gpio_argb->pause) {
        return;
    }

    log_message("set_argb_state(name=%s)",
                gpio_argb->name);

    start_index = gpio_argb->index;

    log_message("index:%d direction:%d fill_mode:%d pause:%d",
                start_index,
                gpio_argb->direction,
                gpio_argb->fill_mode,
                gpio_argb->pause);

    // Wipe strip before draw
    switch(gpio_argb->fill_mode) {
      case 0:
        wipe = 1;
        break;

      case 2:
        // For append mode, we wipe only at the start of 
        // the program
        if (start_index == 0) {
            wipe = 1;
        }
        break;

      default:
        // no wipe
        wipe = 0;
        break;
    }

    if (wipe) {
        for (i = 0; 
             i < gpio_argb->num_leds; 
             i++) {
            gpio_argb->neopixel->setPixelColor(i,
                                               gpio_argb->neopixel->Color(0,0,0));
        }
    }

    // enter permanent loop now to populate
    // the neopixel array from the program data
    // breaks will exit depending on fill mode
    p = gpio_argb->program_start;
    led_count = 0;
    loop = 1;
    while (loop) {
        // find colour separator
        q = strchr(p, ',');
        if (q &&
            q - p < sizeof(colour_buf)) {

            // isolate colour 
            strncpy(colour_buf, 
                    p,
                    q - p);
            colour_buf[q - p] = '\0';

            // move to next in sequence
            p = q + 1;
        }
        else {
            // last colour in program
            strncpy(colour_buf, 
                    p,
                    sizeof(colour_buf) - 1);
            colour_buf[sizeof(colour_buf) - 1] = '\0';

            // Program reset scenarios
            switch(gpio_argb->fill_mode) {
              case 1:
                // reset the program for repeat
                // write 
                p = gpio_argb->program_start;
                break;

              default:
                // once
                p = NULL;
                break;
            }
        }

        log_message("Read colour from program: %s", 
                    colour_buf);

        // convert given colour to int
        if (strlen(colour_buf) > 2 &&
            colour_buf[0] == '0' &&
            (colour_buf[1] == 'x' || colour_buf[1] == 'X')) {
            // hex decode
            colour = strtoul(&colour_buf[2], NULL, 16);
        }
        else {
            // decimal unsigned int
            colour = strtoul(colour_buf, NULL, 10);
        }

        // split to RGB
        red = colour >> 16 & 0xFF;
        green = colour >> 8 & 0xFF;
        blue = colour & 0xFF;

        // Set current pixel index to colour
        // We use Neopixel Color method as this will 
        // apply the RGB, BGR, GBR variation accordingly
        log_message("Setting LED %d to Red:0x%02X Green:0x%02X Blue:0x%02X", 
                    gpio_argb->index,
                    red,
                    green,
                    blue);
        gpio_argb->neopixel->setPixelColor(gpio_argb->index, 
                                           gpio_argb->neopixel->Color(red, 
                                                                      green,
                                                                      blue));
        led_count++;

        // advance index for next colour
        gpio_argb->index = (gpio_argb->index + 1) % gpio_argb->num_leds;
        log_message("Next LED is %d", 
                    gpio_argb->index);

        log_message("LED count is %d", led_count);

        // Loop exit scenarios
        switch(gpio_argb->fill_mode) {
          case 0:
          case 2:
            if (p == NULL) {
                loop = 0;
            }
            break;

          case 1:
            if (led_count >= gpio_argb->num_leds) {
                loop = 0;
            }
            break;

          default:
            // break loop for safety
            loop = 0;
            break;
        }
    }

    gpio_argb->neopixel->show();

    // reset index for next call
    switch(gpio_argb->fill_mode) {
      default:
      case 0:
      case 1:
        // Standard single pixel movement according 
        // to set direction
        // or standstill
        if (gpio_argb->direction > 0) {
            gpio_argb->index = (start_index + 1) % gpio_argb->num_leds;
        }
        if (gpio_argb->direction < 0) {
            gpio_argb->index = (start_index - 1) % gpio_argb->num_leds;
        }
        if (gpio_argb->direction == 0) {
            gpio_argb->index = start_index;
        }
        break;

      case 2:
        // append draw
        // for forward motion, we leave it as is
        // for backward, we need to offset from start_index
        // so we determine the unsigned difference between where
        // we started and are now and then further subtract
        // this from the current position
        if (gpio_argb->direction < 0) {
            gpio_argb->index = (start_index - led_count) % gpio_argb->num_leds;
        }
        break;

    }

    log_message("Done.. LEDs written:%d index:%d",
                led_count,
                gpio_argb->index);

    // Update activity timestamp
    gpio_argb->timestamp = millis();
}

// Function set_argb_program
// Sets the desired aRGB program for the 
// addessable LED strip
void set_argb_program(struct gpio_argb *gpio_argb,
                     const char *program)
{
    char *p;
    char *direction_p = NULL;
    char *pause_p = NULL;
    char *fill_p = NULL; 
    char field_buffer[50];
    uint16_t i;

    if (!gpio_argb) {
        log_message("No argb specified");
        return;
    }

    log_message("set_argb_program(name=%s, program=%s)",
                gpio_argb->name,
                program);

    // resets on neopixel
    // turn all pixels off
    for (i = 0; 
         i < gpio_argb->num_leds; 
         i++) {
        gpio_argb->neopixel->setPixelColor(i,
                                           gpio_argb->neopixel->Color(0,0,0));
    }
    gpio_argb->neopixel->show(); 

    gpio_argb->timestamp = 0;

    // copy in program string
    // if its not a pointer to itself
    if (gpio_argb->program != program) {
        strcpy(gpio_argb->program, program);
    }
    gpio_argb->index = 0;
    gpio_argb->program_start = NULL;

    // parse program
    // <step offset>;<step delay>;<fill mode>;RRGGBB,RRGGBB,.....

    // copy over first 50 octets of text
    // should be enough to contain main front 
    // fields
    strncpy(field_buffer, 
            gpio_argb->program, 
            sizeof(field_buffer) - 1);
    field_buffer[sizeof(field_buffer) - 1] = '\0';

    // Set pointers and NULL separators
    // for key lead fields for direction, pause and fill mode
    direction_p = field_buffer;
    p = strchr(direction_p, ';');
    if (p) {
        *p = '\0';
        p++;
        pause_p = p;
        p = strchr(pause_p, ';');
        if (p) {
            *p = '\0';
            p++;
            fill_p = p;
            p = strchr(fill_p, ';');
            if (p) {
                *p = '\0';
                p++;

                // program start in main string will be same offset as 
                // p is now having traversed pass all ';' delimiters
                gpio_argb->program_start = gpio_argb->program + 
                    (p - field_buffer);
            }
            else {
                log_message("Failed to find program separator");
            }
        }
        else {
            log_message("Failed to find fill mode separator");
        }
    }
    else {
        log_message("Failed to find pause separator");
    }

    if (direction_p && 
        pause_p && 
        fill_p && 
        gpio_argb->program_start) {
        gpio_argb->direction = atoi(direction_p);
        gpio_argb->pause = atoi(pause_p);
        gpio_argb->fill_mode = atoi(fill_p);

    }
    else {
        log_message("Failed to parse program");
    }
}

// Function loop_task_transition_argb
// Drives aRGB strips from task 
// manager
void loop_task_transition_argb()
{
    struct gpio_argb *gpio_argb;

    for (gpio_argb = HTM_LIST_NEXT(gv_device.argb_list);
         gpio_argb != gv_device.argb_list;
         gpio_argb = HTM_LIST_NEXT(gpio_argb)) {
        if (gpio_argb->program_start != NULL) {
            set_argb_state(gpio_argb);
        }
    }
}

// Function: setup_argbs
void setup_argbs()
{
    struct gpio_argb *gpio_argb;
    uint8_t argb_count = 0;

    log_message("setup_argbs()");

    for (gpio_argb = HTM_LIST_NEXT(gv_device.argb_list);
         gpio_argb != gv_device.argb_list;
         gpio_argb = HTM_LIST_NEXT(gpio_argb)) {

        argb_count++;

        log_message("Setting up A-RGB:%s LEDs:%d Pin:%d Neopixel Flags:0x%08X",
                    gpio_argb->name,
                    gpio_argb->num_leds, 
                    gpio_argb->pin, 
                    gpio_argb->neopixel_flags);

        gpio_argb->neopixel = 
            new Adafruit_NeoPixel(gpio_argb->num_leds, 
                                  gpio_argb->pin, 
                                  gpio_argb->neopixel_flags);

        // Initialize all pixels to 'off'
        gpio_argb->neopixel->begin();
        gpio_argb->neopixel->show(); 

        set_argb_program(gpio_argb,
                         gpio_argb->program);

        if (gpio_argb->manual_pin != NO_PIN) {
            log_message("    Manual pin:%d",
                        gpio_argb->manual_pin);
            pinMode(gpio_argb->manual_pin, INPUT_PULLUP);
        }
    }

    if (argb_count > 0) {
        TaskMan.add_task("Neopixel LED Transitions",
                         RUN_STATE_WIFI_STA_UP,
                         1,
                         loop_task_transition_argb);
    }
}

// Function: pin_in_use
// Returns 1 if specified pin is
// found in use in any of the switches,
// sensors or the wifi status pin
uint8_t pin_in_use(uint8_t pin)
{
    struct gpio_switch *gpio_switch;
    struct gpio_sensor *gpio_sensor;
    struct gpio_rgb *gpio_rgb;

    log_message("pin_in_use(pin=%d)", pin);

    if (gv_device.wifi_led_pin == pin) {
        log_message("pin in use on wifi status led");
        return 1;
    }

    if (gv_device.boot_program_pin == pin) {
        log_message("pin in use on boot program pin");
        return 1;
    }

    for (gpio_switch = HTM_LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = HTM_LIST_NEXT(gpio_switch)) {

        if (gpio_switch->relay_pin == pin) {
            log_message("pin in use on switch %s relay ",
                        gpio_switch->name);
            return 1;
        }

        if (gpio_switch->led_pin == pin) {
            log_message("pin in use on switch %s led ",
                        gpio_switch->name);
            return 1;
        }

        if (gpio_switch->manual_pin == pin) {
            log_message("pin in use on switch %s manual pin ",
                        gpio_switch->name);
            return 1;
        }
    }

    for (gpio_sensor = HTM_LIST_NEXT(gv_device.sensor_list);
         gpio_sensor != gv_device.sensor_list;
         gpio_sensor = HTM_LIST_NEXT(gpio_sensor)) {

        if (gpio_sensor->sensor_pin == pin) {
            log_message("pin in use on sensor %s ",
                        gpio_sensor->name);
            return 1;
        }
    }

    for (gpio_rgb = HTM_LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = HTM_LIST_NEXT(gpio_rgb)) {

        if (gpio_rgb->red_pin == pin ||
            gpio_rgb->green_pin == pin ||
            gpio_rgb->blue_pin == pin) {
            log_message("pin in use on led %s",
                        gpio_rgb->name);
            return 1;
        }
    }

    // if we got to here, no matches found
    return 0;
}

// Function: get_json_status
// formats and returns a JSON string representing
// the device details, configuration status and system info
const char *get_json_status(uint8_t pretty)
{
    struct gpio_switch *gpio_switch;
    struct gpio_sensor *gpio_sensor;
    struct gpio_rgb *gpio_rgb;
    struct gpio_argb *gpio_argb;

    log_message("get_json_status(pretty=%d)", pretty);

    // refresh sensors
    read_sensors();

    DynamicJsonDocument json_status(4096);

    // top-level fields
    json_status["name"] = gv_mdns_hostname;
    json_status["zone"] = gv_device.zone;
    json_status["wifi_ssid"] = gv_device.wifi_ssid;
    json_status["ota_enabled"] = gv_device.ota_enabled;
    json_status["telnet_enabled"] = gv_device.telnet_enabled;
    json_status["mdns_enabled"] = gv_device.mdns_enabled;
    json_status["manual_switches_enabled"] = gv_device.manual_switches_enabled;
    json_status["configured"] = gv_device.configured;

    // system section
    JsonObject system  = json_status.createNestedObject("system");
    system["compile_date"] = gv_sw_compile_date;
    system["reset_reason"] = (char*)ESP.getResetReason().c_str();
    system["free_heap"] = ESP.getFreeHeap();
    system["chip_id"] = ESP.getChipId();
    system["flash_id"] = ESP.getFlashChipId();
    system["flash_size"] = ESP.getFlashChipSize();
    system["flash_real_size"] = ESP.getFlashChipRealSize();
    system["flash_speed"] = ESP.getFlashChipSpeed();
    system["cycle_count"] = ESP.getCycleCount();
    system["millis"] = millis();

    // controls section for switches & leds 
    JsonArray controls_arr = json_status.createNestedArray("controls");

    // switches
    for (gpio_switch = HTM_LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = HTM_LIST_NEXT(gpio_switch)) {

        JsonObject obj = controls_arr.createNestedObject();
        obj["name"] = gpio_switch->name;
        obj["type"] = "switch";
        obj["state"] = gpio_switch->current_state;
        obj["context"] = get_sw_context(gpio_switch->state_context);
        obj["behaviour"] = get_sw_behaviour(gpio_switch->switch_behaviour);
        obj["motion_interval"] = gpio_switch->motion_interval;
    }

    // sensors
    for (gpio_sensor = HTM_LIST_NEXT(gv_device.sensor_list);
         gpio_sensor != gv_device.sensor_list;
         gpio_sensor = HTM_LIST_NEXT(gpio_sensor)) {

        JsonObject obj = controls_arr.createNestedObject();
        obj["name"] = gpio_sensor->name;

        switch (gpio_sensor->sensor_type) {
          case GP_SENS_TYPE_NONE:
            obj["type"] = "dummy";
            break;

          case GP_SENS_TYPE_DHT:
            obj["type"] = "temp/humidity";
            obj["humidity"] = gpio_sensor->f1;
            obj["temp"] = gpio_sensor->f2;
            break;
        }
    }

    // rgb
    for (gpio_rgb = HTM_LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = HTM_LIST_NEXT(gpio_rgb)) {

        JsonObject obj = controls_arr.createNestedObject();
        obj["name"] = gpio_rgb->name;
        obj["type"] = "rgb";
        obj["program"] = gpio_rgb->program;
        ets_sprintf(gv_small_buffer,
                    "0x%08X",
                    gpio_rgb->current_colour);
        obj["current_colour"] = gv_small_buffer;
        obj["step"] = gpio_rgb->step;
    }
    
    // argb
    for (gpio_argb = HTM_LIST_NEXT(gv_device.argb_list);
         gpio_argb != gv_device.argb_list;
         gpio_argb = HTM_LIST_NEXT(gpio_argb)) {

        JsonObject obj = controls_arr.createNestedObject();
        obj["name"] = gpio_argb->name;
        obj["type"] = "argb";
        obj["program"] = gpio_argb->program;
    }

    // Format string in compact or prety format
    if (pretty){
        serializeJsonPretty(json_status, gv_large_buffer);
    }
    else {
        serializeJson(json_status, gv_large_buffer);
    }

    log_message("JSON status data: (%d bytes) \n%s", 
                strlen(gv_large_buffer), 
                gv_large_buffer);

    return gv_large_buffer;
}

struct gpio_switch* gpio_switch_alloc()
{
    struct gpio_switch *gpio_switch;

    gpio_switch = (struct gpio_switch*) malloc(sizeof(struct gpio_switch));

    return gpio_switch;
}

struct gpio_sensor* gpio_sensor_alloc()
{
    struct gpio_sensor *gpio_sensor;

    gpio_sensor = (struct gpio_sensor*) malloc(sizeof(struct gpio_sensor));

    return gpio_sensor;
}


struct gpio_rgb* gpio_rgb_alloc()
{
    struct gpio_rgb *gpio_rgb;

    gpio_rgb = (struct gpio_rgb*) malloc(sizeof(struct gpio_rgb));

    return gpio_rgb;
}

struct gpio_argb* gpio_argb_alloc()
{
    struct gpio_argb *gpio_argb;

    gpio_argb = (struct gpio_argb*) malloc(sizeof(struct gpio_argb));

    return gpio_argb;
}


// Function find_switch
// finds switch by name
struct gpio_switch* find_switch(const char *name)
{
    struct gpio_switch *gpio_switch;

    log_message("find_switch(%s)", name);

    for (gpio_switch = HTM_LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = HTM_LIST_NEXT(gpio_switch)) {

        if (!strcmp(gpio_switch->name, name)) {
            log_message("found");
            return gpio_switch;
        }
    }

    log_message("not found");
    return NULL;
}

// Function find_rgb
// Finds RGB device by name
struct gpio_rgb* find_rgb(const char *name)
{
    struct gpio_rgb *gpio_rgb;

    for (gpio_rgb = HTM_LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = HTM_LIST_NEXT(gpio_rgb)) {

        if (!strcmp(gpio_rgb->name, name)) {
            log_message("found");
            return gpio_rgb;
        }
    }

    log_message("not found");
    return NULL;
}

// Function find_argb
// Finds aRGB device by name
struct gpio_argb* find_argb(const char *name)
{
    struct gpio_argb *gpio_argb;

    for (gpio_argb = HTM_LIST_NEXT(gv_device.argb_list);
         gpio_argb != gv_device.argb_list;
         gpio_argb = HTM_LIST_NEXT(gpio_argb)) {

        if (!strcmp(gpio_argb->name, name)) {
            log_message("found");
            return gpio_argb;
        }
    }

    log_message("not found");
    return NULL;
}

// Function: save_config
// Writes config to EEPROM
void save_config()
{
    log_message("save_config()");

    log_message("config data: (%d bytes) \n%s", 
                strlen(gv_config), 
                gv_config);

    log_message("Write EEPROM data..(%d bytes)", sizeof(gv_config));
    EEPROM.begin(sizeof(gv_config));
    EEPROM.put(0, gv_config);
    EEPROM.commit();
}

// Function update_config
// Updates config with specified field string
// value or int value. Also optionall saves
// config to EEPROM if save_now set 
void update_config(char *field, 
                   const char *sval,
                   int32_t ival,
                   uint8_t save_now)
{    
    log_message("update_config()");

    log_message("Current Config:\n%s", gv_config);

    // JSON parse from config
    DynamicJsonDocument json_cfg(4096);
    DeserializationError error = deserializeJson(json_cfg, 
                                                 (const char*)gv_config);
    if (error) {
        log_message("JSON decode failed for config");

        // build fresh JSON document
        strcpy(gv_config, "{}");
        DeserializationError error = deserializeJson(json_cfg, 
                                                     (const char*)gv_config);

        if (error) {
            log_message("Failed to create json cfg document");
            return;
        }
    }

    // perform the desired update
    // after determining a string or int focus
    if (sval) {
        log_message("Updating string %s with %s", 
                    field,
                    sval);
        json_cfg[field] = sval;
    }
    else {
        log_message("Updating string %s with %d", 
                    field,
                    ival);
        json_cfg[field] = ival;
    }

    // Dump back out JSON config
    serializeJsonPretty(json_cfg, gv_config);

    log_message("Config updated to:\n%s", gv_config);

    if (save_now) {
        // Commit to disk
        save_config();
    }
}

// Function: reset_config
// wipes all config 
// puts in sensible defaults
// writes to EEPROM
void reset_config()
{
    struct gpio_switch *gpio_switch;
    struct gpio_sensor *gpio_sensor;
    struct gpio_rgb *gpio_rgb;

    log_message("reset_config()");

    // factory default settings
    // last field on most calls is 0 to 
    // delay EEPROM commit per call
    strcpy(gv_config, "{}");
    update_config("name", gv_mdns_hostname, 0, 0);
    update_config("zone", "Needs Setup", 0, 0);
    update_config("wifi_ssid", "", 0, 0);
    update_config("wifi_password", "", 0, 0);
    update_config("ota_enabled", NULL, 1, 0);
    update_config("telnet_enabled", NULL, 1, 0);
    update_config("mdns_enabled", NULL, 1, 0);
    update_config("manual_switches_enabled", NULL, 1, 0);
    update_config("boot_pin", NULL, 0, 0);
    update_config("wifi_led_pin", NULL, NO_PIN, 0);

    // Mark configured state to 0 to label 
    // ready for auto-config
    // also set last field to 1 to commit 
    // the lot to EEPROM
    update_config("configured", NULL, 0, 1);
}


// Function: load_config
// Loads config from EEPROM, checks for the marker
// octet value and resets config to in-memory array
// defaults
void load_config()
{
    struct gpio_switch *gpio_switch;
    struct gpio_sensor *gpio_sensor;
    struct gpio_rgb *gpio_rgb;
    struct gpio_argb *gpio_argb;

    log_message("load_config()");

    // Init of device 
    memset(&gv_device, 0, sizeof(gv_device));

    // Initialise lists for switches, sensors and LEDs
    gv_device.switch_list = gpio_switch_alloc();
    HTM_LIST_SELFLINK(gv_device.switch_list);

    gv_device.sensor_list = gpio_sensor_alloc();
    HTM_LIST_SELFLINK(gv_device.sensor_list);

    gv_device.rgb_list = gpio_rgb_alloc();
    HTM_LIST_SELFLINK(gv_device.rgb_list);

    gv_device.argb_list = gpio_argb_alloc();
    HTM_LIST_SELFLINK(gv_device.argb_list);

    log_message("Read EEPROM data..(%d bytes)", sizeof(gv_config));
    EEPROM.begin(sizeof(gv_config));
    EEPROM.get(0, gv_config);

    // Safe terminate last char of string
    // just in case
    gv_config[sizeof(gv_config) - 1] = 0;

    log_message("config data: (%d bytes) \n%s", 
                strlen(gv_config), 
                gv_config);

    // Extra Sanity on config data
    // we should have what seem to tokens for zone, wifi 
    // and password
    if (!strstr(gv_config, "\"zone\"") ||
        !strstr(gv_config, "\"wifi_ssid\"") ||
        !strstr(gv_config, "\"wifi_password\"")) {
        log_message("Config likely corrupt.. resetting");
        reset_config();
        return;
    }

    // JSON parse from config
    // Assuming 4k is fine for the overheads
    // we might encounter
    DynamicJsonDocument json_cfg(4096);
    DeserializationError error = deserializeJson(json_cfg, 
                                                 (const char*)gv_config);
    if (error) {
        log_message("JSON decode failed for config.. resetting");
        reset_config();
        return;
    }

    // Standard top-level string and int fields

    // Wifi SSID is the main man here.. if empty, we
    // reset
    strcpy(gv_device.wifi_ssid, json_cfg["wifi_ssid"]);
    if (strlen(gv_device.wifi_ssid) == 0) {
        log_message("Empty WiFI SSID.. resetting");
        reset_config();
        return;
    }

    strcpy(gv_device.zone, json_cfg["zone"]);
    strcpy(gv_device.wifi_password, json_cfg["wifi_password"]);
    gv_device.ota_enabled = json_cfg["ota_enabled"];
    gv_device.telnet_enabled = json_cfg["telnet_enabled"];
    gv_device.mdns_enabled = json_cfg["mdns_enabled"];
    gv_device.manual_switches_enabled = json_cfg["manual_switches_enabled"];
    gv_device.boot_program_pin = json_cfg["boot_pin"];
    gv_device.wifi_led_pin = json_cfg["wifi_led_pin"];
    gv_device.wifi_led_on_high = json_cfg["wifi_led_on_high"];
    gv_device.force_apmode_onboot = json_cfg["force_apmode_onboot"];
    gv_device.configured = json_cfg["configured"];

    JsonArray controls = json_cfg["controls"];
    if (controls.isNull()) {
        log_message("Failed to parse controls array from json cfg");
        return;
    }

    // Loop through each control
    // each should have a name and type as standard
    for (JsonObject control : controls) {
        const char* control_name = control["name"];
        const char* control_type = control["type"];
        const uint8_t enabled = control["enabled"];

        if (control_name && control_type) {
            log_message("Control:%s, Type:%s Enabled:%d",
                        control_name, 
                        control_type,
                        enabled);

            if (!enabled) {
                log_message("disabled.. ignoring");
                continue;
            }

            if (!strcmp(control_type, "switch")) {
                // switch
                const char* sw_mode = control["sw_mode"];

                gpio_switch = gpio_switch_alloc();
                HTM_LIST_INSERT(gv_device.switch_list, gpio_switch);

                strcpy(gpio_switch->name, control_name);
                gpio_switch->relay_pin = control["sw_relay_pin"];
                gpio_switch->relay_on_high = control["sw_relay_on_high"];
                gpio_switch->led_pin = control["sw_led_pin"];
                gpio_switch->led_on_high = control["sw_led_on_high"];
                gpio_switch->manual_pin = control["sw_man_pin"];
                gpio_switch->manual_interval = control["sw_man_interval"];
                gpio_switch->manual_auto_off = control["sw_man_auto_off"];
                gpio_switch->current_state = control["sw_state"];

                gpio_switch->switch_behaviour = SW_BHVR_TOGGLE;
                if (!strcmp(sw_mode, "on")) {
                    gpio_switch->switch_behaviour = SW_BHVR_ON;
                }
                else if (!strcmp(sw_mode, "off")) {
                    gpio_switch->switch_behaviour = SW_BHVR_OFF;
                }

                // Motion pin
                // left optional. if pin comes in as 0, assumed
                // not applicable
                gpio_switch->motion_pin = control["sw_motion_pin"];
                if (gpio_switch->motion_pin == 0) {
                    gpio_switch->motion_pin = NO_PIN;
                    gpio_switch->motion_interval = 0;
                }
                else {
                    gpio_switch->motion_interval = control["sw_motion_interval"];
                }
            }

            if (!strcmp(control_type, "temp/humidity")) {
                gpio_sensor = gpio_sensor_alloc();
                HTM_LIST_INSERT(gv_device.sensor_list, gpio_sensor);

                const char *th_variant = control["th_variant"];

                // Default
                gpio_sensor->sensor_variant = DHT11;

                if (!strcmp(th_variant, "DHT11")) {
                    gpio_sensor->sensor_variant = DHT11;
                }
                else if (!strcmp(th_variant, "DHT21")) {
                    gpio_sensor->sensor_variant = DHT21;
                }
                else if (!strcmp(th_variant, "DHT22")) {
                    gpio_sensor->sensor_variant = DHT22;
                }

                strcpy(gpio_sensor->name, control_name);
                gpio_sensor->sensor_type = GP_SENS_TYPE_DHT;
                gpio_sensor->sensor_pin = control["th_pin"];
                gpio_sensor->temp_offset = control["th_temp_offset"];
            }

            if (!strcmp(control_type, "rgb")) {
                gpio_rgb = gpio_rgb_alloc();
                HTM_LIST_INSERT(gv_device.rgb_list, gpio_rgb);

                strcpy(gpio_rgb->name, control_name);
                gpio_rgb->red_pin = control["red_pin"];
                gpio_rgb->green_pin = control["green_pin"];
                gpio_rgb->blue_pin = control["blue_pin"];
                gpio_rgb->manual_pin = control["manual_pin"];
                strcpy(gpio_rgb->program, control["program"]);
            }

            if (!strcmp(control_type, "argb")) {
                gpio_argb = gpio_argb_alloc();
                HTM_LIST_INSERT(gv_device.argb_list, gpio_argb);

                strcpy(gpio_argb->name, control_name);
                gpio_argb->pin = control["pin"];
                gpio_argb->manual_pin = control["manual_pin"];
                gpio_argb->num_leds = control["num_leds"];
                gpio_argb->neopixel_flags = control["neopixel_flags"];
                strcpy(gpio_argb->program, control["program"]);
            }
        }
    }


}

// Function: ap_handle_root
// On the initial call, this will display the pre-built
// web form we create from the in-memory array and config
// showing profiel, zone, SSID, password and all named switches
// and sensors
// But the same handler will be invoked on a GET/POST from
// the form. So the code will test for the "zone" arg and act on
// that to drive population of the config record, save and reboot
// ir profile is changed on the POST, it merely applies the chosen
// profile and re-displays the form with updated field layouts
void ap_handle_root() 
{
    uint8_t i;
    char *str_ptr;
    char *selected_str;
    static uint32_t last_scanned = 0;

    static uint8_t num_networks;
    uint32_t now;

    log_message("ap_handle_root()");

    // check for post args

    if (gv_web_server.hasArg("reset") &&
        // Reset will trump others if set to 1
        strcmp(gv_web_server.arg("reset").c_str(), "1") == 0) {
        log_message("Reset via AP Mode");
        reset_config();
        gv_reboot_requested = 1;
    }
    else {
        // normal wifi setup
        if (gv_web_server.hasArg("ssid")) {
            // actual normal config updates
            gv_reboot_requested = 1;

            update_config("wifi_ssid", 
                          gv_web_server.arg("ssid").c_str(),
                          0,
                          0);

            update_config("wifi_password", 
                          gv_web_server.arg("password").c_str(),
                          0,
                          1);
        }
    }

    if (gv_reboot_requested) {
        gv_web_server.send(200, "text/html", "Applying settings and rebooting");
    }
    else {
        // Scan wifi network
        // initially at startup
        // and no more than every 30 seconds
        // handles refreshes of this page so 
        // that we will not rescan each time
        // and stall the AP mode
        now = millis();
        if (now - last_scanned > 30000 || 
            last_scanned == 0) {
            log_message("scanning wifi networks");
            num_networks = WiFi.scanNetworks();
            log_message("found %d SSIDs", num_networks);
            last_scanned = now;
        }

        // Build combo list of discovered 
        // networks and try to set current SSID
        // as selected entry
        str_ptr = gv_small_buffer;
        gv_small_buffer[0] = '\0';

        for (i = 0; i < num_networks; i++) {

            // try to mark curent SSID selected
            if (!strcmp(gv_device.wifi_ssid, WiFi.SSID(i).c_str())) {
                selected_str = "selected";
            }
            else {
                selected_str = "";
            }

            str_ptr += ets_sprintf(str_ptr,
                                   "<option value=\"%s\" %s>%s</option>",
                                   WiFi.SSID(i).c_str(),
                                   selected_str,
                                   WiFi.SSID(i).c_str());
        }

        ets_sprintf(gv_large_buffer,
                    "<head>"
                    "<title>JBHASD Device Setup</title>"
                    "<meta charset=\"utf-8\">"
                    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                    "</head>"
                    "<body>"
                    "<h2>%s Setup</h2>"
                    "<form action=\"/\" method=\"post\">"
                    "<div>"
                    "    <label>WIFI SSID:</label>"
                    "    <select name=\"ssid\">"
                    "    %s"
                    "    </select>"
                    "</div>"
                    "<div>"
                    "    <label>WIFI Password:</label>"
                    "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"password\">"
                    "</div>"
                    "<div>"
                    "    <label>Reset Config:</label>"
                    "    <select name=\"reset\">"
                    "       <option value=\"0\" selected>No</option>"
                    "       <option value=\"1\" >Yes</option>"
                    "    </select>"
                    "</div>"
                    "<div>"
                    "<button class=\"btn btn-primary\">Apply Settings</button>"
                    "</div>"
                    "<br><br>"
                    "<br><br>"
                    "<div>"
                    "    <label>JSON Config</label>"
                    "</div>"
                    "<div>"
                    "<pre>"
                    "%s"
                    "</pre>"
                    "</div>"
                    "</form>"
                    "</body>",
            gv_mdns_hostname,
            gv_small_buffer,
            gv_device.wifi_password,
            MAX_FIELD_LEN,
            gv_config);

        // Just return the pre-formatted web page we built at
        // setup
        gv_web_server.send(200, "text/html", gv_large_buffer);
    }
}

// Function: toggle_wifi_led
// Toggles the WiFI boot LED on/off with
// the specified delay
// Drives the visual aspect of the boot sequence to
// indicate the config mode in play
void toggle_wifi_led(uint16_t delay_msecs)
{
    static uint8_t state = 0;

    // toggle
    state = (state + 1) % 2;

    // ambiguous use of state here for LOW/HIGH
    // as we don't know the low/high nature of 
    // the LED
    // But given its a toggle, it doesn't matter
    // It will work as a toggle when repeatedly called
    digitalWrite(gv_device.wifi_led_pin,
                 state);

    if (delay > 0) {
        delay(delay_msecs);
    }
}

// Function: start_ota()
// Enables OTA service for formware
// flashing OTA
void start_ota()
{
    static uint8_t already_setup = 0;

    // Only do this once
    // lost wifi conections will re-run start_wifi_sta_mode() so
    // we dont want this function called over and over
    if (already_setup) {
        log_message("OTA already started");
        return;
    }

    if (!gv_device.ota_enabled) {
        log_message("OTA mode not enabled.. returning");
        return;
    }

    // mark as setup, preventing multiple calls
    already_setup = 1;

    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);

    // Set hostname
    ArduinoOTA.setHostname(gv_mdns_hostname);

    // No authentication by default
    // ArduinoOTA.setPassword((const char *)"123");

    ArduinoOTA.onStart([]() {
                       log_message("OTA Start");

                       // Change mode to lock in OTA behaviour
                       TaskMan.set_run_state(RUN_STATE_WIFI_OTA);
                       });

    ArduinoOTA.onEnd([]() {
                     log_message("OTA End");
                     });

    ArduinoOTA.onProgress([](uint32_t progress,
                             uint32_t total) {
                          log_message("OTA Progress: %d/%d (%02u%%)", 
                                      progress, 
                                      total, 
                                      (progress / (total / 100)));
                          });

    ArduinoOTA.onError([](ota_error_t error) {
                       log_message("Error[%u]:", error);
                       if (error == OTA_AUTH_ERROR) log_message("Auth Failed");
                       else if (error == OTA_BEGIN_ERROR) log_message("Begin Failed");
                       else if (error == OTA_CONNECT_ERROR) log_message("Connect Failed");
                       else if (error == OTA_RECEIVE_ERROR) log_message("Receive Failed");
                       else if (error == OTA_END_ERROR) log_message("End Failed");
                       });

    ArduinoOTA.begin();

    // OTA (STA) every 1s
    TaskMan.add_task("OTA",
                     RUN_STATE_WIFI_STA_UP | RUN_STATE_WIFI_OTA,
                     1000,
                     loop_task_ota);

    log_message("OTA service started");
}


// Function: start_ap_mode
// Sets up the device in AP mode
// The function formats the config form used in
// AP mode. It uses the in-memory array of switches to drive
// this along with config data
void start_ap_mode()
{
    log_message("start_ap_mode()");
    TaskMan.set_run_state(RUN_STATE_WIFI_AP);


    // Activate AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(gv_ap_ip,
                      gv_ap_ip,
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(gv_mdns_hostname);

    // Captive DNS to try and force the client to the
    // landing page
    gv_dns_server.start(DNS_PORT, "*", gv_ap_ip);

    log_message("AP IP:%d.%d.%d.%d",
                gv_ap_ip[0],
                gv_ap_ip[1],
                gv_ap_ip[2],
                gv_ap_ip[3]);


    gv_web_server.on("/", ap_handle_root);
    gv_web_server.onNotFound(ap_handle_root);
    gv_web_server.begin();
    log_message("HTTP server started for AP mode");
}


// Function: sta_handle_json
// This is the API handler that takes in POST/GET args for
// "control" and "state" and sets the desired switch to the
// specified state.
// "control" and "program" are used to set LED controls to their
// programmed behaviour
// Then it prints out the JSON status string listing device name
// and details along with the configured controls and their states
// Additional commands it supports:
// "reboot" arg will reboot the device
// "reset" arg wipes config and restarts
// "apmode" will perform a once-off boot into AP mode
void sta_handle_json() {
    uint32_t state;
    uint8_t pretty = 0;

    log_message("sta_handle_json()");

    // Check for switch control name and state
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("state")) {
        set_switch_state(find_switch(gv_web_server.arg("control").c_str()),
                         atoi(gv_web_server.arg("state").c_str()),
                         SW_ST_CTXT_NETWORK); // specifying name only
    }

    // Check for switch control name and motion interval
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("motion_interval")) {
        set_switch_motion_interval(find_switch(gv_web_server.arg("control").c_str()),
                         atoi(gv_web_server.arg("motion_interval").c_str()));
    }

    // RGB/aRGB Program
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("program")) {
        set_rgb_program(find_rgb(gv_web_server.arg("control").c_str()), 
                        gv_web_server.arg("program").c_str());
        set_argb_program(find_argb(gv_web_server.arg("control").c_str()), 
                        gv_web_server.arg("program").c_str());
    }

    if (gv_web_server.hasArg("pretty")) {
        pretty = atoi(gv_web_server.arg("pretty").c_str());
    }

    // reboot if directed
    if (gv_web_server.hasArg("reboot")) {
        log_message("Received reboot command");
        gv_reboot_requested = 1;
    } 

    // jump to ap mode if directed
    // This done by making a save to config
    // with a force apmode flag
    if (gv_web_server.hasArg("apmode")) {
        log_message("Received apmode command");
        gv_device.force_apmode_onboot = 1;
        update_config("force_apmode_onboot", NULL, 1, 1);
        gv_reboot_requested = 1;
    }

    // factory reset if directed
    if (gv_web_server.hasArg("reset")) {
        log_message("Received reset command");
        reset_config();
        gv_reboot_requested = 1;
    }

    // mark for reconfigure
    // just a case of setting configured 
    // field to 0. No need to even save or 
    // reboot
    // Device will work fine until detected and 
    // reconfigured
    if (gv_web_server.hasArg("reconfig")) {
        log_message("Received reconfigure command");
        gv_device.configured = 0;
    }

    // config update
    // Copy over specified config
    // flag as configured
    // Also set name to actual device name
    if (gv_web_server.hasArg("config")) {
        log_message("Received configure command");
        strncpy(gv_config, 
                gv_web_server.arg("config").c_str(),
                MAX_CONFIG_LEN);
        gv_config[MAX_CONFIG_LEN - 1] = '\0';
        update_config("name", gv_mdns_hostname, 0, 0);
        update_config("configured", NULL, 1, 1);
        gv_reboot_requested = 1;
    }

    // Return current status as standard
    gv_web_server.send(200, "text/html", get_json_status(pretty));

}

// Function: start_wifi_sta_mode
// Configures the device as a WiFI client
void start_wifi_sta_mode()
{
    log_message("start_wifi_sta_mode(ssid:%s password:%s)", 
                gv_device.wifi_ssid,
                gv_device.wifi_password);

    // set state to track wifi down
    // will drive main loop to act accordingly
    TaskMan.set_run_state(RUN_STATE_WIFI_STA_DOWN);

    // WIFI
    // Turn off first as it better 
    // handles recovery after WIFI router
    // outages
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(gv_mdns_hostname);
    WiFi.begin(gv_device.wifi_ssid,
               gv_device.wifi_password);
}


// Function: start_sta_mode_services
// Run after we confirm WiFI up
// records IP and starts MDNS & DNS-SD
void start_sta_mode_services()
{
    log_message("start_sta_mode_services()");

    gv_sta_ip = WiFi.localIP();
    log_message("Connected.. IP:%d.%d.%d.%d",
                gv_sta_ip[0],
                gv_sta_ip[1],
                gv_sta_ip[2],
                gv_sta_ip[3]);

    if (gv_device.mdns_enabled) {
        // MDNS & DNS-SD using "JBHASD"
        log_message("Activating MDNS for hostname:%s", gv_mdns_hostname);
        if (!MDNS.begin(gv_mdns_hostname)) {
            log_message("Error setting up MDNS responder!");
        }
        else {
            log_message("Activating DNS-SD");
            MDNS.addService("JBHASD", "tcp", WEB_PORT);
        }
    }
    else {
        log_message("MDNS disabled!");
    }

    // Web server
    gv_web_server.on("/json", sta_handle_json);
    gv_web_server.onNotFound(sta_handle_json);
    gv_web_server.begin();
    log_message("HTTP server started for client mode");

    start_ota();
    start_telnet();
}


// Function: loop_task_check_boot_switch
// Checks for a pressed state on the boot program
// pin to drive a switch to AP mode
void loop_task_check_boot_switch()
{
    static uint8_t pin_wait_timer = 25;
    uint8_t button_state;

    // Can toggle LED with no 
    // delay as the main loop tasks
    // apply the timing
    toggle_wifi_led(0);

    // decrement pin wait timer on each call
    // 25 calls against a 200msec call interval
    // is roughly 5 seconds
    if (pin_wait_timer > 0) {
        log_message("Boot wait #%d", pin_wait_timer);
        button_state = digitalRead(gv_device.boot_program_pin);
        if (button_state == LOW) {
            log_message("Detected pin down.. going to AP mode");
            start_ap_mode();
            return;
        }
        pin_wait_timer--;
    }
    else {
        log_message("Passed boot wait stage.. going to STA mode");
        start_wifi_sta_mode();
    }
}



// task loop state machine 
// function wrappers
// Its based on void functions (no args, no returns)
// So object-based loop task for web server and 
// several others are manipulated in simple wrappers
// Some of the wrappers are more substantial

void loop_task_check_wifi_down(void)
{
    log_message("loop_task_check_wifi_down()");
    if (WiFi.status() != WL_CONNECTED) {
        log_message("WiFI is down");
        TaskMan.set_run_state(RUN_STATE_WIFI_STA_DOWN);

        log_message("Connecting to Wifi SSID:%s, Password:%s",
                    gv_device.wifi_ssid,
                    gv_device.wifi_password);

        start_wifi_sta_mode();
    }
}

void loop_task_check_wifi_up(void)
{
    uint8_t i;
    uint8_t status;
    static uint16_t check_count = 0;

    // This function gets called every 10 secs
    // So 360 calls is about 1 hour
    uint16_t max_checks_before_reboot = 360; 

    // Restart WiFI every 60 seconds if we continue
    // to remain disconnected
    // that equates to 6 calls
    uint16_t max_checks_before_wifi_restart = 6; 

    log_message("loop_task_check_wifi_up()");

    check_count++;

    status = WiFi.status();
    log_message("WiFI Status: %d", status);

    if (status == WL_CONNECTED) {
        log_message("WiFI is up");
        TaskMan.set_run_state(RUN_STATE_WIFI_STA_UP);

        // quick LED burst
        for (i = 1; i <= 50; i++) {
            toggle_wifi_led(50);
        }

        // wifi LED back to correct state
        restore_wifi_led_state();

        // reset connect count so that
        // we give a reconnect scenario the same max attempts
        // before reboot
        check_count = 0;

        // start additional services for sta
        // mode
        start_sta_mode_services();
    }
    else {
        log_message("WiFI is down");

        // check for max checks and restart
        if (check_count > max_checks_before_reboot) {
            log_message("Exceeded max checks of %d.. rebooting",
                        max_checks_before_reboot);
            gv_reboot_requested = 1;
        }
        else {
            // if not rebooting, then go for simple Wifi Restarts
            // every N calls
            if (check_count % max_checks_before_wifi_restart == 0) {
                log_message("Exceeded %d check interval.. restarting WiFI",
                            max_checks_before_wifi_restart);
                start_wifi_sta_mode();
            }
        }
    }
}

void loop_task_webserver(void)
{
    gv_web_server.handleClient();
}

void loop_task_dns(void)
{
    gv_dns_server.processNextRequest();
}

void loop_task_ota(void)
{
    ArduinoOTA.handle();
}

void loop_task_wifi_led(void)
{
    // toggle LED with no delay
    // main loop driving the timing for this
    toggle_wifi_led(0); 
}


void loop_task_reboot(void)
{
    if (gv_reboot_requested) {
        log_message("Calling ESP.restart()");
        ESP.restart();
    }
}

void loop_task_ap_reboot(void)
{
    log_message("Rebooting from AP Mode (timeout)");
    gv_reboot_requested = 1;
}

void loop_task_log_stats(void)
{
    TaskMan.log_stats();
}

// Function: setup
// Standard setup initialisation callback function
// Does an initial config load and switch setup.
// Then activates the boot GPIO pin for enabling ap
// mode and also the LED indicator
// It gives about 5 secs then for the pin to be pressed
// to force us into AP mode, falling back on a normal
// startup. If the config however is found to be reset
// (wifi SSID blank), then it forces itself into AP mode
void setup()
{
    // Get the config at this stage
    // as start_serial needs config setup
    load_config();
    start_serial();

    TaskMan.set_logger(vlog_message);
    TaskMan.set_run_state(RUN_STATE_INIT);

    // Reboot Check every 5s
    TaskMan.add_task("Reboot Check",
                     RUN_STATE_ALL,
                     5000,
                     loop_task_reboot);

    // AP auto-reboot after 5 mins
    TaskMan.add_task("AP Reboot Timer",
                     RUN_STATE_WIFI_AP,
                     300000,
                     loop_task_ap_reboot);

    // Web Server (AP) every 50 ms
    TaskMan.add_task("Webserver",
                     RUN_STATE_WIFI_AP |
                     RUN_STATE_WIFI_STA_UP,
                     50,
                     loop_task_webserver);

    // DNS Server every 50ms
    TaskMan.add_task("DNS",
                     RUN_STATE_WIFI_AP,
                     50,
                     loop_task_dns);

    // Init Mode button push every 200ms
    TaskMan.add_task("Boot AP Switch",
                     RUN_STATE_INIT,
                     200,
                     loop_task_check_boot_switch);

    // AP WiFI LED every 100 ms (fast)
    TaskMan.add_task("AP Status LED",
                     RUN_STATE_WIFI_AP,
                     100,
                     loop_task_wifi_led);

    // STA WiFI LED every 1s (slow)
    TaskMan.add_task("STA Status LED",
                     RUN_STATE_WIFI_STA_DOWN,
                     1000,
                     loop_task_wifi_led);

    // WiFI Check (While Down) Every 10s
    TaskMan.add_task("WiFI Status Up Check",
                     RUN_STATE_WIFI_STA_DOWN,
                     10000,
                     loop_task_check_wifi_up);

    // Manual Switches every 200ms
    TaskMan.add_task("Switch Checks",
                     RUN_STATE_WIFI_STA_DOWN | RUN_STATE_WIFI_STA_UP,
                     200,
                     loop_task_check_switches);

    // WiFI Check (While Up) every 10s
    TaskMan.add_task("WiFI Status Down Check",
                     RUN_STATE_WIFI_STA_UP,
                     10000,
                     loop_task_check_wifi_down);

    // Stats every 30s
    TaskMan.add_task("Stats",
                     RUN_STATE_ALL,
                     30000,
                     loop_task_log_stats);

    // Watchdog timer reconfigure
    // for longer period (8 seconds)
    // Helps stop spontaneous watchdog
    // timers resetting the device
    ESP.wdtDisable();
    ESP.wdtEnable(WDTO_8S);

    // Disable WiFI storing of settings in 
    // flash. We do this ourselves
    WiFi.persistent(false);

    log_message("Device boot: ChipId:%u FreeHeap:%u ResetReason:%s",
                ESP.getChipId(),
                ESP.getFreeHeap(),
                ESP.getResetReason().c_str());

    // Set MDNS hostname based on prefix and chip ID
    ets_sprintf(gv_mdns_hostname,
                "JBHASD-%08X",
                ESP.getChipId());

    // Set up status LED
    pinMode(gv_device.wifi_led_pin, OUTPUT);

    // forced AP mode from config
    // Before we launch AP mode
    // we first unset this option and save
    // config.. that ensures that if power cycled, 
    // the device will boot as normal and not 
    // remain in AP mode
    if (gv_device.force_apmode_onboot == 1) {
        log_message("Detected forced AP Mode");
        update_config("force_apmode_onboot", NULL, 0, 1);
        start_ap_mode();
        return;
    }

    // If we have no zone set
    // then we go straight for AP mode
    if (strlen(gv_device.wifi_ssid) == 0) {
        log_message("No WiFI SSID set.. going directly to AP mode");
        start_ap_mode();
        return;
    }

    // Init Boot program pin for detecting manual
    // jump to AP mode at boot stage
    pinMode(gv_device.boot_program_pin, INPUT_PULLUP);

    // Activate switches, leds and sensors
    setup_switches();
    setup_rgbs();
    setup_argbs();
    setup_sensors();

    log_message("Setup stage complete");
}


// Function: loop
void loop()
{
    // Keep the sw watchdog happy
    ESP.wdtFeed();

    // Tasks
    TaskMan.nudge();
}
