// ESP-8266 Sketch for variations of devices
// using what I call the JBHASD "API"
// (Json-Based Home Automation with Service Discovery)
//
// Cormac Long July 2017

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
#include "jbhasd_types.h"
#include "jbhasd_profiles.h"

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

char gv_push_ip[MAX_FIELD_LEN];
int gv_push_port;

// Output buffers
// In an effort to keep the RAM usage low
// It seems best to use a single large and two
// small buffers to do all string formatting for
// web pages and JSON strings
char gv_large_buffer[4096];
char gv_small_buffer_1[2048];
char gv_small_buffer_2[2048];

// Not sure how standard LOW,HIGH norms are
// but these registers allow us customise as required.
// Each array will be indexed with a state value of 0 or 1
// and used to return the appropriate LOW or HIGH value to use
// to act on that boolean input
// So index 1 of gv_switch_state_reg returns HIGH as the state
// needed to turn on the switch
// Similarly index 1 of gv_led_state_reg returns LOW as the state
// needed to turn on the LED
int gv_switch_state_reg[] = { LOW, HIGH };
int gv_led_state_reg[] = { HIGH, LOW };

// Telnet Support
// Wrapper layer around logging to serial or
// to connected network client
// could be enhanced later on

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
WiFiServer telnet_server(23);
WiFiClient telnet_clients[MAX_TELNET_CLIENTS];

// Function: log_message
// Wraps calls to Serial.print or connected
// network client
void log_message(char *format, ... )
{
    static char log_buf[LOGBUF_MAX + 1];
    int i;
    va_list args;

    va_start(args, format);
    vsnprintf(log_buf,
              LOGBUF_MAX,
              format,
              args);
    log_buf[LOGBUF_MAX] = 0; // force terminate last character
    va_end(args);

    // CRLF termination
    strcat(log_buf, "\r\n");

    switch(gv_logging) {
      case LOGGING_SERIAL:
        Serial.print(log_buf);
        break;

      case LOGGING_NW_CLIENT:
        for(i = 0; i < MAX_TELNET_CLIENTS; i++) {
            if (telnet_clients[i] && 
                telnet_clients[i].connected()) {
                telnet_clients[i].write((uint8_t*)log_buf, strlen(log_buf));
                telnet_clients[i].flush();
            }
        }
        break;
    }
}


// Function start_telnet
// enables telnet server
void start_telnet()
{
    log_message("start_telnet()");

    if (!gv_config.telnet_enabled) {
        log_message("Telnet not enabled.. returning");
        return;
    }

    // start telnet server
    telnet_server.begin();
    telnet_server.setNoDelay(true);

    gv_logging = LOGGING_NW_CLIENT;
}

// Function handle_telnet_sessions
// loop function for driving telnet
// session handling both accepting new
// sessions and flushing data from existing
// sessions
void handle_telnet_sessions()
{
    uint8_t i;

    if (gv_logging != LOGGING_NW_CLIENT) {
        return;
    }

    // check for new sessions
    if (telnet_server.hasClient()) {
        for(i = 0; i < MAX_TELNET_CLIENTS; i++) {
            // find free/disconnected spot
            if (!telnet_clients[i] || 
                !telnet_clients[i].connected()) {
                if(telnet_clients[i]) {
                    telnet_clients[i].stop();
                }
            telnet_clients[i] = telnet_server.available();
            continue;
        }
    }

    //no free/disconnected spot so reject
    WiFiClient serverClient = telnet_server.available();
    serverClient.stop();
    }

    //check clients for data
    for (i = 0; i < MAX_TELNET_CLIENTS; i++) {
        if (telnet_clients[i] && 
            telnet_clients[i].connected()) {
            if(telnet_clients[i].available()) {
                // throw away any data
                while(telnet_clients[i].available()) {
                    telnet_clients[i].read();
                }
            }
        }
    }
}


// Function: set_switch_state
// Sets the desired switch state to the value of the state arg
// The switch is referenced by name or index value
// for the in-memory array
void set_switch_state(const char *name,
                      int index,
                      unsigned int state,
                      enum switch_state_context context)
{
    int i = 0;
    int found = 0;

    log_message("set_switch_state(name=%s, index=%d, state=%u, context=%d)",
                name,
                index,
                state,
                context);

    // Trust index if provided
    // skips linear search
    // essentially ignores the name arg
    if (index >= 0) {
        i = index;
        found = 1;
    }
    else {
        // Check for a value for name
        // Run the risk of matching against an empty named switch otherwise
        if (!name or strlen(name) == 0) {
            log_message("no name specified for switch.. ignoring");
            return;
        }
        // locate the switch by name in register
        while (gv_profile->switch_register[i].name && !found) {
            if (!strcmp(gv_profile->switch_register[i].name, name)) {
                found = 1;
                log_message("found switch in register");
            }
            else {
                i++;
            }
        }
    }

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

    // change state as requested
    if (found) {
        // Set the current state
        gv_profile->switch_register[i].current_state = state;
        gv_profile->switch_register[i].state_context = context;

        if (gv_profile->switch_register[i].relay_pin != NO_PIN) {
            digitalWrite(gv_profile->switch_register[i].relay_pin,
                         gv_switch_state_reg[state]);
        }
        if (gv_profile->switch_register[i].led_pin != NO_PIN) {
            digitalWrite(gv_profile->switch_register[i].led_pin,
                         gv_led_state_reg[state]);
        }
    }
    else {
        log_message("switch not found in register");
    }
}

// Function: setup_switches
// Scans the in-memory array and configures the defined switch
// pins including initial states
void setup_switches()
{
    int i;

    log_message("setup_switches()");

    // loop until we reach the terminator where
    // name is NULL
    i = 0;
    while (gv_profile->switch_register[i].name) {

        // Over-ride hard-coded name and initial state
        // with values set in config
        gv_profile->switch_register[i].name = gv_config.switch_names[i];
        gv_profile->switch_register[i].initial_state = gv_config.switch_initial_states[i];
        gv_profile->switch_register[i].switch_behaviour = 
            (enum switch_behaviour)gv_config.switch_behaviours[i];

        // Only service switches with set names
        // Allows for the config to disable hard-coded
        // defaults
        if (strlen(gv_profile->switch_register[i].name) > 0) {
            log_message("Setting up switch:%s, initial state:%d",
                          gv_profile->switch_register[i].name,
                          gv_profile->switch_register[i].initial_state);

            if (gv_profile->switch_register[i].relay_pin != NO_PIN) {
                log_message("    switch pin:%d",
                              gv_profile->switch_register[i].relay_pin);
                pinMode(gv_profile->switch_register[i].relay_pin, OUTPUT);
            }

            if (gv_profile->switch_register[i].led_pin != NO_PIN) {
                log_message("    LED pin:%d",
                              gv_profile->switch_register[i].led_pin);
                pinMode(gv_profile->switch_register[i].led_pin, OUTPUT);
            }

            if (gv_profile->switch_register[i].manual_pin != NO_PIN) {
                log_message("    Manual pin:%d",
                              gv_profile->switch_register[i].manual_pin);
                pinMode(gv_profile->switch_register[i].manual_pin, INPUT_PULLUP);
            }

            // set initial state
            set_switch_state(gv_profile->switch_register[i].name,
                             i,
                             gv_profile->switch_register[i].initial_state,
                             SW_ST_CTXT_INIT);
        }
        i++; // to the next entry in register
    }
}

// Function: check_manual_switches
// Scans the input pins of all switches and
// invokes a toggle of the current state if it detects
// LOW state
void check_manual_switches()
{
    int i;
    int button_state;
    int took_action = 0;
    static unsigned long last_action_timestamp = 0;
    WiFiClient wifi_client;
    int rc;

    //disabled to keep the serial activity quiet
    //log_message("check_manual_switches()");

    if (!gv_config.manual_switches_enabled) {
        //log_message("manual switches disabled.. returning");
        return;
    }

    if (millis() - last_action_timestamp < 500) {
        // fast repeat switching bypassed
        // the loop calls this function every 100ms
        // that will ensure a rapid response to a switch
        // press but we don't want 10 actions per second
        // so as soon as a switch is pressed, we want 500 msecs
        // grace before we allow that again
        return;
    }

    // loop until we reach the terminator where
    // name is NULL
    i = 0;
    while (gv_profile->switch_register[i].name) {

        // Only work with entries with a set switchname
        // and manual pin
        // Excludes non-relevant or config-disabled entries
        if (strlen(gv_profile->switch_register[i].name) > 0 &&
            gv_profile->switch_register[i].manual_pin != NO_PIN) {
            //log_message("Check Manual pin:%d", gv_profile->switch_register[i].manual_pin);
            button_state = digitalRead(gv_profile->switch_register[i].manual_pin);
            if (button_state == LOW) {
                log_message("Detected manual push on switch:%s pin:%d",
                            gv_profile->switch_register[i].name,
                            gv_profile->switch_register[i].manual_pin);

                switch(gv_profile->switch_register[i].switch_behaviour) {
                  default:
                  case SW_BHVR_TOGGLE:
                    // toggle state and treat as an action taken
                    set_switch_state(gv_profile->switch_register[i].name,
                                     i,
                                     (gv_profile->switch_register[i].current_state + 1) % 2,
                                     SW_ST_CTXT_MANUAL);
                    took_action = 1; // note any activity
                    break;

                  case SW_BHVR_ON:
                    // only allow switch to be turned on from off state
                    if (gv_profile->switch_register[i].current_state != 1) {
                        set_switch_state(gv_profile->switch_register[i].name,
                                         i,
                                         1, // On
                                         SW_ST_CTXT_MANUAL);
                        took_action = 1; // note any activity
                    }
                    break;

                  case SW_BHVR_OFF:
                    if (gv_profile->switch_register[i].current_state != 0) {
                        set_switch_state(gv_profile->switch_register[i].name,
                                         i,
                                         0, // Off
                                         SW_ST_CTXT_MANUAL);
                        took_action = 1; // note any activity
                    }
                    break;
                }
            }
        }
        i++; // to the next entry in register
    }

    i = 0;
    while (gv_profile->led_register[i].name) {

        // Only work with entries with a set name
        // and manual pin
        // Excludes non-relevant or config-disabled entries
        if (strlen(gv_profile->led_register[i].name) > 0 &&
            gv_profile->led_register[i].manual_pin != NO_PIN) {
            //log_message("Check Manual pin:%d", gv_profile->led_register[i].manual_pin);
            button_state = digitalRead(gv_profile->led_register[i].manual_pin);
            if (button_state == LOW) {
                log_message("Detected manual push on led:%s pin:%d",
                            gv_profile->led_register[i].name,
                            gv_profile->led_register[i].manual_pin);

                // Set to random value
                // FIXME this is technically broken for now
                // will refactor into something better
                set_led_state(&(gv_profile->led_register[i]));
                took_action = 1; // note any activity
            }
        }
        i++; // to the next entry in register
    }

    if (took_action) {
        // record timestamp for fast
        // re-entry protection
        last_action_timestamp = millis();

        // Send update push if WiFI up
        if (gv_mode == MODE_WIFI_STA_UP && 
            strlen(gv_push_ip) > 0) {

            // straight connection
            // Tried the http object
            // for GET and PUSH and just got
            // grief from the python web server

            log_message("pushing update request to host:%s port:%d", gv_push_ip,
                                                                       gv_push_port);

            ets_sprintf(gv_small_buffer_1,
                       "update=%s",
                       gv_mdns_hostname);

            if (!wifi_client.connect(gv_push_ip,
                                     gv_push_port)) {
                log_message("connection failed");
            }
            else {
                wifi_client.println("POST / HTTP/1.1");
                wifi_client.println("Host: server_name");
                wifi_client.println("Accept: */*");
                wifi_client.println("Content-Type: application/x-www-form-urlencoded");
                wifi_client.print("Content-Length: ");
                wifi_client.println(strlen(gv_small_buffer_1));
                wifi_client.println();
                wifi_client.print(gv_small_buffer_1);
                //delay(500);
                if (wifi_client.connected()) {
                    wifi_client.stop();
                }
            }
        }
    }
}

// Function: setup_sensors
// Scans the in-memory array and configures the
// defined sensor pins
void setup_sensors()
{
    static int already_setup = 0;
    int i;
    DHT *dhtp;

    log_message("setup_sensors()");

    // Protect against multiple calls
    // can only really set these sensors up once
    // because of the pointer ref field
    // could try to get smart and call delete on set pointers
    // but its probably safer to just do this once.
    if (already_setup) {
        log_message("already setup (returning)");
        return;
    }
    already_setup = 1;

    // loop until we reach the terminator where
    // name is NULL
    i = 0;
    while (gv_profile->sensor_register[i].name) {
        gv_profile->sensor_register[i].name = gv_config.sensor_names[i];
        if (strlen(gv_profile->sensor_register[i].name) > 0) {
            log_message("Setting up sensor %s",
                          gv_profile->sensor_register[i].name);

            switch (gv_profile->sensor_register[i].sensor_type) {
              case GP_SENS_TYPE_NONE:
                // do nothing
                break;

              case GP_SENS_TYPE_DHT:
                log_message("DHT Type %d on pin %d",
                              gv_profile->sensor_register[i].sensor_variant,
                              gv_profile->sensor_register[i].sensor_pin);

                if (gv_profile->sensor_register[i].sensor_pin != NO_PIN) {
                    // Setup DHT temp/humidity sensor and record
                    // class pointer in void* ref
                    dhtp = new DHT(gv_profile->sensor_register[i].sensor_pin,
                                   gv_profile->sensor_register[i].sensor_variant);
                    gv_profile->sensor_register[i].ref = dhtp;
                }
                else {
                    log_message("Sensor not assigned to pin (fake)");
                    // non-pin assigned DHT
                    // for faking/simulation
                    gv_profile->sensor_register[i].ref = NULL;
                }
                break;
            }
        }

        i++; // to the next entry in register
    }
}

// Function: float_get_fp
// Returns floating point part of float
// as integer. Needed due to limitations of
// formatting where it cant handle %f in ets_sprintf
int float_get_fp(float f, int precision) {

   int f_int;
   unsigned int f_fp;
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
    int i;
    DHT *dhtp;
    float f1, f2;

    log_message("read_sensors(temp_offset=%s)",
                gv_config.temp_offset);

    i = 0;
    while(gv_profile->sensor_register[i].name) {
        if (strlen(gv_profile->sensor_register[i].name) > 0) {
            switch (gv_profile->sensor_register[i].sensor_type) {
              case GP_SENS_TYPE_DHT:
                dhtp = (DHT*)gv_profile->sensor_register[i].ref;

                if (gv_profile->sensor_register[i].sensor_pin != NO_PIN) {
                    // Humidity
                    f1 = dhtp->readHumidity();
                    if (isnan(f1)) {
                        log_message("Humidity sensor read failed for %s",
                                    gv_profile->sensor_register[i].name);
                    }
                    else {
                        log_message("Humidity read from sensor %d.%02d",
                                    (int)f1,
                                    float_get_fp(f1, 2));
                        gv_profile->sensor_register[i].f1 = f1;
                    }

                    // Temp Celsius
                    f2 = dhtp->readTemperature();
                    if (isnan(f2)) {
                        log_message("Temperature sensor read failed for %s",
                                    gv_profile->sensor_register[i].name);
                    }
                    else {
                        log_message("Temperature read from sensor %d.%02d",
                                    (int)f2,
                                    float_get_fp(f2, 2));
                        // record temp as read value offset
                        // by temp_offset in config
                        gv_profile->sensor_register[i].f2 = f2 +
                            atof(gv_config.temp_offset);
                    }
                }
                else {
                    // fake the values
                    gv_profile->sensor_register[i].f1 = (ESP.getCycleCount() % 100) + 0.5;
                    gv_profile->sensor_register[i].f2 = ((ESP.getCycleCount() +
                                                 ESP.getFreeHeap()) % 100) + 0.25;
                }
                log_message("Sensor: %s Humidity: %d.%02d Temperature: %d.%02d (temp offset:%s)",
                            gv_profile->sensor_register[i].name,
                            (int)gv_profile->sensor_register[i].f1,
                            float_get_fp(gv_profile->sensor_register[i].f1, 2),
                            (int)gv_profile->sensor_register[i].f2,
                            float_get_fp(gv_profile->sensor_register[i].f2, 2),
                            gv_config.temp_offset);
                break;
            }
        }
        i++;
    }
}

// Function parse_colour
// Parses hue, red, green and blue
// values from 4-octet int
// Then applies MAX_PWM_VALUE against
// 0-255 ranges of RGB to render into the PWM
// range of the ESP-8266 (0-1023)
// Finally optional hue value applied against
// RGB values to act as a brightness affect on
// the values
void parse_colour(unsigned int colour,
                  int &red,
                  int &green,
                  int &blue)
{
    float hue_factor;
    int hue;

    // separate out hue from most significant octet and
    // RGB from lower 3 octets
    hue = (colour >> 24) & 0xFF;
    red = (colour >> 16) & 0xFF;
    green = (colour >> 8) & 0xFF;
    blue = colour & 0xFF;
    log_message("Decoded RGB.. Value:0x%08X Hue:0x%02X Red:0x%02X Green:0x%02X Blue:0x%02X",
                colour,
                hue,
                red,
                green,
                blue);

    // apply PWM
    red = red * MAX_PWM_VALUE / 255;
    green = green * MAX_PWM_VALUE / 255;
    blue = blue * MAX_PWM_VALUE / 255;

    log_message("Applied PWM.. Red:%d Green:%d Blue:%d",
                red,
                green,
                blue);

    // Apply optional hue modification
    // Hue value 1-255 is rendered into
    // a fraction of 255 and multiplied against the
    // RGB values to scale them accordingly
    if (hue > 0) {
        hue_factor = float(hue) / 255;
        red = float(red) * hue_factor;
        green = float(green) * hue_factor;
        blue = float(blue) * hue_factor;

        log_message("Applied Hue.. Red:%d Green:%d Blue:%d",
                    red,
                    green,
                    blue);
    }
}

// Function shift_rgb_led
// Shifts RGB values for start_red,
// start_green & start_blue one notch
// each toward the end values
// Used to apply a fading effect on values
void shift_rgb_led(unsigned short &start_red,
                   unsigned short &start_green,
                   unsigned short &start_blue,
                   unsigned short end_red,
                   unsigned short end_green,
                   unsigned short end_blue)
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
// Takes a gpio_led object
// and applies a fade step
// toward a new colour setting
void fade_rgb(struct gpio_led *led)
{
    unsigned long now;

    if (led->steps[led->step_index].fade_delay <= 0) {
        // instant switch to new setting
        log_message("Instant change to.. Red:%d Green:%d Blue:%d",
                    led->desired_states[0],
                    led->desired_states[1],
                    led->desired_states[2]);

        // write changes to active pins
        if (led->red_pin != NO_PIN){
            analogWrite(led->red_pin,
                        led->desired_states[0]);
        }
        if (led->green_pin != NO_PIN){
            analogWrite(led->green_pin,
                        led->desired_states[1]);
        }
        if (led->blue_pin != NO_PIN){
            analogWrite(led->blue_pin,
                        led->desired_states[2]);
        }

        // Update states
        led->current_states[0] = led->desired_states[0];
        led->current_states[1] = led->desired_states[1];
        led->current_states[2] = led->desired_states[2];
    }
    else {
        // delay mechanism
        // require elapsed msecs to match configured 
        // fade delay
        now = millis();
        if (led->steps[led->step_index].fade_delay > 0 &&
            now - led->timestamp < led->steps[led->step_index].fade_delay) {
            return;
        }

        // timestamp activity
        led->timestamp = now;

        // shift all three RGB values 1 PWM value
        // toward the desired states
        shift_rgb_led(led->current_states[0],
                      led->current_states[1],
                      led->current_states[2],
                      led->desired_states[0],
                      led->desired_states[1],
                      led->desired_states[2]);

        log_message("RGB Step.. Timestamp:%lu Delay:%d R:%d G:%d B:%d -> R:%d G:%d B:%d",
                    led->timestamp,
                    led->steps[led->step_index].fade_delay,
                    led->current_states[0],
                    led->current_states[1],
                    led->current_states[2],
                    led->desired_states[0],
                    led->desired_states[1],
                    led->desired_states[2]);

        // write changes to pins
        // We're testing for the PIN assignment here
        // to allow for a scenario where only some
        // or one of the pins are set. This caters for
        // custom applications of dimming single colour scenarios
        // or assigning three separate dimmable LEDs to a single
        // device
        if (led->red_pin != NO_PIN){
            analogWrite(led->red_pin,
                        led->current_states[0]);
        }
        if (led->green_pin != NO_PIN){
            analogWrite(led->green_pin,
                        led->current_states[1]);
        }
        if (led->blue_pin != NO_PIN){
            analogWrite(led->blue_pin,
                        led->current_states[2]);
        }
    }
}

// Function transition_leds()
// Checks active LED devices and
// progresses to next step in program
// or applies transitions to existing step
void transition_leds()
{
    int i;

    i = 0;
    // iterate LEDs with programs set
    while (gv_profile->led_register[i].name) { 
        if (gv_profile->led_register[i].num_steps > 0) {
            // Fade color if the current and desired states 
            // are not yet aligned
            if (strlen(gv_profile->led_register[i].name) > 0 &&
                (gv_profile->led_register[i].desired_states[0] !=
                 gv_profile->led_register[i].current_states[0] ||
                 gv_profile->led_register[i].desired_states[1] !=
                 gv_profile->led_register[i].current_states[1] ||
                 gv_profile->led_register[i].desired_states[2] !=
                 gv_profile->led_register[i].current_states[2])) {
                fade_rgb(&gv_profile->led_register[i]);
            }
            else if (gv_profile->led_register[i].num_steps > 1) {
                // apply next step in program 
                // we dont need to call this for 1 step programs
                set_led_state(&(gv_profile->led_register[i]));
            }
        }
        i++;
    }
}

// Function set_led_program
// parses LED string program into 
// internal array
// Program string takes the format
// <colour>;<fade delay>;<pause>,<colour>;<fade delay>;<pause>,...
// So each step is a semi-colon separated trip of colour, fade and pause
// Steps are then comma-separated.
// The fade and pause args can be omitted
void set_led_program(const char *name,
                     int index,
                     const char *program)
{
    int i = 0;
    int found = 0;
    const char *p, *q; 
    char *r, *s;
    char step_buffer[50];
    struct gpio_led *led;

    log_message("set_led_program(name=%s, index=%d, program=%s)",
                name,
                index,
                program);

    // Trust index if provided
    // skips linear search
    // essentially ignores the name arg
    if (index >= 0) {
        i = index;
        found = 1;
    }
    else {
        // Check for a value for name
        // Run the risk of matching against an empty named led otherwise
        if (!name or strlen(name) == 0) {
            log_message("no name specified for led.. ignoring");
            return;
        }
        // locate the led by name in register
        while (gv_profile->led_register[i].name && !found) {
            if (!strcmp(gv_profile->led_register[i].name, name)) {
                found = 1;
                log_message("found led in register");
            }
            else {
                i++;
            }
        }
    }

    if (found) {
        // simplify the pointer access
        led = &(gv_profile->led_register[i]);

        // reset LED program
        // step index is put to -1 as
        // set_led_state() is intending to advance this
        // index. Also -1 state is handy for detecting first run 
        // scenario
        led->num_steps = 0;
        led->step_index = -1;
        led->timestamp = 0;

        // parse program string
        p = program;

        // loop while we have a pointer and non-NULL data and 
        // we've not exceeded the max steps
        while (p && *p && 
               led->num_steps < MAX_LED_STEPS) {
            // set start of this step
            q = p;

            // search for terminator boundary for next step
            p = strchr(p, ',');
            if (p) {
                // extract characters from this step
                strncpy(step_buffer, q, p - q);
                step_buffer[p - q] = '\0';
                // skip to next field
                p++;
            }
            else {
                // no more steps, rest of string is the step
                strcpy(step_buffer, q);
                p = NULL; // stops any further parsing
            }

            log_message("Extracted step:%s", step_buffer);

            // search for ; terminator between colour 
            // and fade delay
            r = strchr(step_buffer, ';');
            if (r) {
                // NULL terminator and move r on 1 char
                // gives us two strings
                *r = '\0';
                r++;

                led->steps[led->num_steps].fade_delay = atoi(r);

                // Next separator is for pause
                // sme NULL trick
                s = strchr(r, ';');
                if (s) {
                    *s = '\0';
                    s++;
                    led->steps[led->num_steps].pause = atoi(s);
                }
                else {
                    // no separator, value taken as 0
                    led->steps[led->num_steps].pause = 0;
                }
            }
            else {
                // no separator, value taken as 0
                led->steps[led->num_steps].fade_delay = 0;
            }

            // Extract colour value
            // sensitive to hex and decimal
            if (strlen(step_buffer) > 2 &&
                step_buffer[0] == '0' &&
                (step_buffer[1] == 'x' || step_buffer[1] == 'X')) {
                // hex decode
                led->steps[led->num_steps].colour = 
                    strtoul(&step_buffer[2], NULL, 16);
            }
            else {
                // decimal unsigned int
                led->steps[led->num_steps].colour = 
                    strtoul(step_buffer, NULL, 10);
            }

            log_message("Decoded step[%d].. colour:0x%08X fade delay:%d pause:%d",
                        led->num_steps,
                        led->steps[led->num_steps].colour,
                        led->steps[led->num_steps].fade_delay,
                        led->steps[led->num_steps].pause);

            // increment steps
            led->num_steps++;
        }

        // nudge into motion
        set_led_state(led);
    }
    else {
        log_message("led not found in register");
    }
}

// Function: set_led_state
// Sets the LED to its next/first program step
// also applies msec interval counting for oauses
// between program steps
void set_led_state(struct gpio_led *led)
{
    int start_red, start_green, start_blue;
    int end_red, end_green, end_blue;
    unsigned long now;

    log_message("set_led_state(name=%s)",
                led->name);

    if (led->num_steps <= 0) {
        log_message("program has no defined steps.. nothing to do");
        return;
    }

    // Pause behaviour
    // applies only if we're actually running a program.. so we'll be on 
    // step_index >=0 (not -1)
    // The current step will also have to have an assigned
    // pause period. We then just apply that interval in 
    // timestamp msec motion before allowing us move to the next
    // step
    now = millis();
    if (led->step_index >= 0 &&
        led->steps[led->step_index].pause > 0 &&
        now - led->timestamp < led->steps[led->step_index].pause) {
        return;
    }

    // timestamp activity
    led->timestamp = now;

    // Module-rotate step index
    // initial increment will be from -1 so the first 
    // step is slot 0 of the array
    led->step_index = (led->step_index + 1) % led->num_steps;

    log_message("step index:%d.. colour:0x%08X fade:%d",
                led->step_index,
                led->steps[led->step_index].colour,
                led->steps[led->step_index].fade_delay);

    // Set desired colour (state)
    led->current_state = led->steps[led->step_index].colour;

    // parse the desired state into PWM
    // values
    parse_colour(led->current_state,
                 end_red,
                 end_green,
                 end_blue);

    // populate into desired state array
    led->desired_states[0] = end_red;
    led->desired_states[1] = end_green;
    led->desired_states[2] = end_blue;
}

// Function: setup_leds
// Scans the in-memory array and configures the defined led
// pins including initial values
void setup_leds()
{
    int i;

    log_message("setup_leds()");

    // loop until we reach the terminator where
    // name is NULL
    i = 0;
    while (gv_profile->led_register[i].name) {

        // Over-ride hard-coded name and initial value
        // with values set in config
        gv_profile->led_register[i].name = gv_config.led_names[i];
        gv_profile->led_register[i].current_state = 0;

        set_led_program(gv_profile->led_register[i].name,
                        i,
                        gv_config.led_programs[i]);

        // Only service led pins with set names
        // Allows for the config to disable hard-coded
        // defaults
        if (strlen(gv_profile->led_register[i].name) > 0) {
            log_message("Setting up LED:%s, initial value:%d",
                        gv_profile->led_register[i].name,
                        gv_profile->led_register[i].current_state);

            if (gv_profile->led_register[i].red_pin != NO_PIN) {
                log_message("    LED Red pin:%d",
                            gv_profile->led_register[i].red_pin);
                pinMode(gv_profile->led_register[i].red_pin, OUTPUT);
            }
            if (gv_profile->led_register[i].green_pin != NO_PIN) {
                log_message("    LED Green pin:%d",
                            gv_profile->led_register[i].green_pin);
                pinMode(gv_profile->led_register[i].green_pin, OUTPUT);
            }
            if (gv_profile->led_register[i].blue_pin != NO_PIN) {
                log_message("    LED Blue pin:%d",
                            gv_profile->led_register[i].blue_pin);
                pinMode(gv_profile->led_register[i].blue_pin, OUTPUT);
            }
            if (gv_profile->led_register[i].manual_pin != NO_PIN) {
                log_message("    Manual pin:%d",
                              gv_profile->led_register[i].manual_pin);
                pinMode(gv_profile->led_register[i].manual_pin, INPUT_PULLUP);
            }
        }
        i++; // to the next entry in register
    }
}

// Function: pin_in_use
// Returns 1 if specified pin is
// found in use in any of the switches,
// sensors or the wifi status pin
int pin_in_use(int pin)
{
    int i;

    log_message("pin_in_use(pin=%d)", pin);

    if (gv_profile->wifi_led_pin == pin) {
        log_message("pin in use on wifi status led");
        return 1;
    }

    // loop until we reach the terminator where
    // name is NULL
    i = 0;
    while (gv_profile->switch_register[i].name) {
        if (strlen(gv_profile->switch_register[i].name) > 0) {

            if (gv_profile->switch_register[i].relay_pin == pin) {
                log_message("pin in use on switch %s relay ",
                            gv_profile->switch_register[i].name);
                return 1;
            }

            if (gv_profile->switch_register[i].led_pin == pin) {
                log_message("pin in use on switch %s led ",
                            gv_profile->switch_register[i].name);
                return 1;
            }

            if (gv_profile->switch_register[i].manual_pin == pin) {
                log_message("pin in use on switch %s manual pin ",
                            gv_profile->switch_register[i].name);
                return 1;
            }

            i++; // to the next entry in register
        }
    }

    // Repeat for sensors
    i = 0;
    while (gv_profile->sensor_register[i].name) {
        if (strlen(gv_profile->sensor_register[i].name) > 0) {

            if (gv_profile->sensor_register[i].sensor_pin == pin) {
                log_message("pin in use on sensor %s",
                            gv_profile->sensor_register[i].name);
                return 1;
            }

            i++; // to the next entry in register
        }
    }

    // Repeat for LEDs
    i = 0;
    while (gv_profile->led_register[i].name) {
        if (strlen(gv_profile->led_register[i].name) > 0) {

            if (gv_profile->led_register[i].red_pin == pin ||
                gv_profile->led_register[i].green_pin == pin ||
                gv_profile->led_register[i].blue_pin == pin) {
                log_message("pin in use on led %s",
                            gv_profile->led_register[i].name);
                return 1;
            }

            i++; // to the next entry in register
        }
    }

    // if we got to hear, no matches found
    return 0;
}

// Function: get_json_status
// formats and returns a JSON string representing
// the device details, configuration status and system info
const char *get_json_status()
{
    char *str_ptr;
    char *led_prog_ptr;
    int i, j;

    log_message("get_json_status()");

    // refresh sensors
    read_sensors();

    /*  JSON specification for the status string we return
     *  { "name": "%s", "zone": "%s", "ota_enabled" : %d, "telnet_enabled" : %d,
     *  "manual_switches_enabled" : %d, "temp_offset" : "%s",
     *  "ssid" : "%s", "profile" : "%s",
     *  "controls": [%s],
     *  "sensors": [%s],
     *  "system" : { "reset_reason" : "%s",
     *  "free_heap" : %d, "chip_id" : %d, "flash_id" : %d, "flash_size" : %d,
     *  "flash_real_size" : %d, "flash_speed" : %d, "cycle_count" : %d } }
     *
     *  Control(switch): { "name": "%s", "type": "switch", "state": %d, "state_hex": "%s", 
     *                     "context": "%s", "behaviour": "%s" }
     *
     *  Control(LED): { "name": "%s", "type": "led", "state": %d, "state_hex": "%s", 
     *                  "program": "%s" }
     *
     *  Sensor: { "name": "%s", "type": "%s", "humidity": "%s", temp: "%s" }
     */

    // switches
    str_ptr = gv_small_buffer_1;
    gv_small_buffer_1[0] = 0;
    i = 0;
    while(gv_profile->switch_register[i].name) {

        // only detail configured switches
        // Those disabled will have had their name
        // set empty
        if (strlen(gv_profile->switch_register[i].name) > 0) {
            if (str_ptr != gv_small_buffer_1) {
                // separator
                str_ptr += ets_sprintf(str_ptr, ", ");
            }
            str_ptr += ets_sprintf(str_ptr,
                                   "{ \"name\": \"%s\", \"type\": \"switch\", "
                                   "\"state\": %u, \"state_hex\": \"0x%08X\", "
                                   "\"context\": \"%s\", "
                                   "\"behaviour\": \"%s\" }",
                                   gv_profile->switch_register[i].name,
                                   gv_profile->switch_register[i].current_state,
                                   gv_profile->switch_register[i].current_state,
                                   get_sw_context(gv_profile->switch_register[i].state_context),
                                   get_sw_behaviour(gv_profile->switch_register[i].switch_behaviour));
        }
        i++;
    }

    // LEDs
    i = 0;
    while(gv_profile->led_register[i].name) {

        // only detail configured led pins
        // Those disabled will have had their name
        // set empty

        // build LED program string
        led_prog_ptr = gv_small_buffer_2;
        gv_small_buffer_2[0] = 0;
        for (j = 0; 
             j < gv_profile->led_register[i].num_steps;
             j++) {
            if (led_prog_ptr != gv_small_buffer_2) {
                // separator
                led_prog_ptr += ets_sprintf(led_prog_ptr, ",");
            }
            led_prog_ptr += ets_sprintf(led_prog_ptr, 
                                        "0x%08X;%d;%d",
                                        gv_profile->led_register[i].steps[j].colour,
                                        gv_profile->led_register[i].steps[j].fade_delay,
                                        gv_profile->led_register[i].steps[j].pause);
        }

        if (strlen(gv_profile->led_register[i].name) > 0) {
            if (str_ptr != gv_small_buffer_1) {
                // separator
                str_ptr += ets_sprintf(str_ptr, ", ");
            }
            str_ptr += ets_sprintf(str_ptr,
                                   "{ \"name\": \"%s\", \"type\": \"led\", "
                                   "\"state\": %u, \"state_hex\": \"0x%08X\", "
                                   "\"program\": \"%s\" }",
                                   gv_profile->led_register[i].name,
                                   gv_profile->led_register[i].current_state,
                                   gv_profile->led_register[i].current_state,
                                   gv_small_buffer_2);
        }
        i++;
    }

    // sensors
    str_ptr = gv_small_buffer_2;
    gv_small_buffer_2[0] = 0;
    i = 0;
    while(gv_profile->sensor_register[i].name) {
        if (strlen(gv_profile->sensor_register[i].name) > 0) {
            if (str_ptr != gv_small_buffer_2) {
                // separator
                str_ptr += ets_sprintf(str_ptr, ", ");
            }

            switch (gv_profile->sensor_register[i].sensor_type) {
              case GP_SENS_TYPE_NONE:
                // dummy not expecting to go here really
                // but we can put out a dummy entry if only to keep
                // the JSON valid
                str_ptr += ets_sprintf(str_ptr,
                                       "{ \"name\": \"%s\", \"type\": \"dummy\" }",
                                       gv_profile->sensor_register[i].name);
                break;

              case GP_SENS_TYPE_DHT:
                str_ptr += ets_sprintf(str_ptr,
                                       "{ \"name\": \"%s\", \"type\": \"temp/humidity\", "
                                       "\"humidity\": \"%d.%02d\", "
                                       "\"temp\": \"%d.%02d\" }",
                                       gv_profile->sensor_register[i].name,
                                       (int)gv_profile->sensor_register[i].f1,
                                       float_get_fp(gv_profile->sensor_register[i].f1, 2),
                                       (int)gv_profile->sensor_register[i].f2,
                                       float_get_fp(gv_profile->sensor_register[i].f2, 2));
                break;
            }
        }

        i++;
    }


    ets_sprintf(gv_large_buffer,
                "{ \"name\": \"%s\", "
                "\"zone\": \"%s\", "
                "\"ota_enabled\" : %u, "
                "\"telnet_enabled\" : %u, "
                "\"manual_switches_enabled\" : %u, "
                "\"temp_offset\" : \"%s\", "
                "\"ssid\" : \"%s\", "
                "\"profile\" : \"%s\", "
                "\"controls\": [%s], "
                "\"sensors\": [%s], "
                "\"system\" : { \"reset_reason\" : \"%s\", \"free_heap\" : %u, "
                "\"chip_id\" : %u, \"flash_id\" : %u, \"flash_size\" : %u, "
                "\"flash_real_size\" : %u, \"flash_speed\" : %u, \"cycle_count\" : %u, "
                "\"millis\" : %lu "
                "} }\n",
                gv_mdns_hostname,
                gv_config.zone,
                gv_config.ota_enabled,
                gv_config.telnet_enabled,
                gv_config.manual_switches_enabled,
                gv_config.temp_offset,
                gv_config.wifi_ssid,
                gv_config.profile,
                gv_small_buffer_1,
                gv_small_buffer_2,
                ESP.getResetReason().c_str(),
                ESP.getFreeHeap(),
                ESP.getChipId(),
                ESP.getFlashChipId(),
                ESP.getFlashChipSize(),
                ESP.getFlashChipRealSize(),
                ESP.getFlashChipSpeed(),
                ESP.getCycleCount(),
                millis());

    return gv_large_buffer;
}

// Function: reset_config
// wipes all config and resets active
// profile to first in register
void reset_config()
{
    int i;
    log_message("reset_config()");

    // memset to 0, empty strings galore
    memset(&gv_config, 0, sizeof(gv_config));

    // clear profile name but
    // set profile pointer to first profile
    gv_config.profile[0] = '\0';
    gv_profile = &(gv_profile_register[0]);

    // OTA defaults to Enabled
    gv_config.ota_enabled = 1;

    // Temp offset set to 0
    // this is a string field
    // because it can contain a float value
    strcpy(gv_config.temp_offset, "0");

    // Telnet defaults to Enabled
    gv_config.telnet_enabled = 1;

    // Manual Switches enabled by default
    gv_config.manual_switches_enabled = 1;

    // Default zone to an init state
    strcpy(gv_config.zone, "Unknown");
}


// Function: apply_config_profile
// Sets the active profile to the specified value
// searches the register for a match, resetting config
// if we fail to find the named profile
// Otherwise, we asign the global profile pointer to the matched
// profile and set its name in config
void apply_config_profile(const char *profile)
{
    int i = 0;

    log_message("apply_config_profile(%s)", profile);

    while(gv_profile_register[i].name != NULL &&
          strcmp(gv_profile_register[i].name,
                 profile) != 0) {
        i++;
    }
    if (gv_profile_register[i].name != NULL) {
        log_message("located profile in register");
        gv_profile = &(gv_profile_register[i]);

        // Check if profile already applied
        // Otherwise we'll overwrite existing config
        if (strcmp(gv_config.profile, profile) == 0) {
          log_message("profile already applied");
        }
        else {
            // copy register defaults into profile
            strcpy(gv_config.profile,
                   gv_profile_register[i].name);

            // populate switch defaults from in-memory array
            i = 0;
            while (gv_profile->switch_register[i].name) {
                strcpy(&(gv_config.switch_names[i][0]),
                       gv_profile->switch_register[i].name);
                gv_config.switch_initial_states[i] = gv_profile->switch_register[i].initial_state;
                gv_config.switch_behaviours[i] = gv_profile->switch_register[i].switch_behaviour;
                i++;
            }

            // populate sensor defaults
            i = 0;
            while (gv_profile->sensor_register[i].name) {
                strcpy(&(gv_config.sensor_names[i][0]),
                       gv_profile->sensor_register[i].name);
                i++;
            }

            // populate led defaults from in-memory array
            i = 0;
            while (gv_profile->led_register[i].name) {
                strcpy(&(gv_config.led_names[i][0]),
                       gv_profile->led_register[i].name);
                strcpy(&(gv_config.led_programs[i][0]),
                       gv_profile->led_register[i].init_program);
                i++;
            }
        }
    }
    else {
        log_message("cannot locate profile in register");
        reset_config();
    }
}

// Function: load_config
// Loads config from EEPROM, checks for the marker
// octet value and resets config to in-memory array
// defaults
void load_config()
{
    int i;
    log_message("load_config()");

    log_message("Read EEPROM data..(%d bytes)", sizeof(gv_config));
    EEPROM.begin(sizeof(gv_config) + 10);
    EEPROM.get(0, gv_config);

    // Print out config details
    log_message("Marker:%02X Profile:%s",
                gv_config.marker,
                gv_config.profile);

    if (gv_config.marker != CFG_MARKER_VAL ||
        strlen(gv_config.profile) == 0) {
        log_message("marker field not matched to special "
                    "or profile set to empty string.. "
                    "resetting config to defaults");
        reset_config();
    }
    else {
        apply_config_profile(gv_config.profile);
    }

    // Print out config details
    log_message("Marker:%02X "
                "Profile:%s "
                "Zone:%s "
                "Wifi SSID:%s "
                "Wifi Password:%s "
                "OTA Update:%u "
                "Telnet:%u "
                "Temp Offset:%s "
                "Manual switches:%u ",
                gv_config.marker,
                gv_config.profile,
                gv_config.zone,
                gv_config.wifi_ssid,
                gv_config.wifi_password,
                gv_config.ota_enabled,
                gv_config.telnet_enabled,
                gv_config.temp_offset,
                gv_config.manual_switches_enabled);

    // Print values of each switch name
    for (i = 0; i < MAX_SWITCHES; i++) {
        log_message("Switch[%d]:%s initial state:%d press behaviour:%s",
                    i,
                    gv_config.switch_names[i],
                    gv_config.switch_initial_states[i],
                    get_sw_behaviour((enum switch_behaviour)gv_config.switch_behaviours[i]));
    }

    // Print values of each sensor name
    for (i = 0; i < MAX_SENSORS; i++) {
        log_message("Sensor[%d]:%s",
                    i,
                    gv_config.sensor_names[i]);
    }

    // Print values of each LED name
    for (i = 0; i < MAX_LEDS; i++) {
        log_message("LED[%d]:%s program:%s",
                    i,
                    gv_config.led_names[i],
                    gv_config.led_programs[i]);
    }
}

// Function: save_config
// Writes config to EEPROM
void save_config()
{
    log_message("save_config()");
    int i;

    gv_config.marker = CFG_MARKER_VAL;

    log_message("Writing EEPROM data..");
    log_message("Marker:%02X "
                "Profile:%s "
                "Zone:%s "
                "Wifi SSID:%s "
                "Wifi Password:%s "
                "OTA Update:%u "
                "Telnet:%u "
                "Temp Offset:%s "
                "Manual switches:%u ",
                gv_config.marker,
                gv_config.profile,
                gv_config.zone,
                gv_config.wifi_ssid,
                gv_config.wifi_password,
                gv_config.ota_enabled,
                gv_config.telnet_enabled,
                gv_config.temp_offset,
                gv_config.manual_switches_enabled);

    // Print values of each switch name
    for (i = 0; i < MAX_SWITCHES; i++) {
        log_message("Switch[%d]:%s initial state:%d press behaviour:%s",
                    i,
                    gv_config.switch_names[i],
                    gv_config.switch_initial_states[i],
                    get_sw_behaviour((enum switch_behaviour)gv_config.switch_behaviours[i]));
    }

    // Print values of each sensor name
    for (i = 0; i < MAX_SENSORS; i++) {
        log_message("Sensor[%d]:%s",
                      i,
                      gv_config.sensor_names[i]);
    }

    // Print values of each LED name
    for (i = 0; i < MAX_LEDS; i++) {
        log_message("LED[%d]:%s program:%s",
                    i,
                    gv_config.led_names[i],
                    gv_config.led_programs[i]);
    }

    log_message("Read EEPROM data..(%d bytes)", sizeof(gv_config));
    EEPROM.begin(sizeof(gv_config) + 10);
    EEPROM.put(0, gv_config);
    EEPROM.commit();
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
void ap_handle_root() {
    int i;
    unsigned int led_value;

    int store_config = 0; // default

    log_message("ap_handle_root()");

    // check for post args

    if (gv_web_server.hasArg("reset") &&
        atoi(gv_web_server.arg("reset").c_str()) == 1) {
        // reset arg set to 1
        // wipe and restart ap mode
        // will default back to profile selection
        store_config = 0;
        reset_config();
        start_ap_mode();
    }
    else if (gv_web_server.hasArg("profile")) {
        // profile selection.. post from profile config
        // apply profile and restart ap mode
        store_config = 0;
        apply_config_profile(gv_web_server.arg("profile").c_str());
        start_ap_mode();
    }
    else if (gv_web_server.hasArg("zone")) {
        // actual normal config saving
        // just store
        store_config = 1;

        strcpy(gv_config.zone,
               gv_web_server.arg("zone").c_str());
        log_message("Got Zone: %s", gv_config.zone);

        strcpy(gv_config.wifi_ssid,
               gv_web_server.arg("ssid").c_str());
        log_message("Got WiFI SSID: %s", gv_config.wifi_ssid);

        strcpy(gv_config.wifi_password,
               gv_web_server.arg("password").c_str());
        log_message("Got WiFI Password: %s", gv_config.wifi_password);

        gv_config.ota_enabled = atoi(gv_web_server.arg("ota_enabled").c_str());
        log_message("Got OTA Enabled: %u", gv_config.ota_enabled);

        gv_config.telnet_enabled = atoi(gv_web_server.arg("telnet_enabled").c_str());
        log_message("Got Telnet Enabled: %u", gv_config.telnet_enabled);

        gv_config.manual_switches_enabled = atoi(gv_web_server.arg("manual_switches_enabled").c_str());
        log_message("Got Manual Switches Enabled: %u", gv_config.manual_switches_enabled);

        strcpy(gv_config.temp_offset,
               gv_web_server.arg("temp_offset").c_str());
        log_message("Got Temp Offset: %s", gv_config.temp_offset);

        for (i = 0; i < MAX_SWITCHES; i++) {
            log_message("Getting post args for switches %d/%d",
                        i, MAX_SWITCHES - 1);
            // format switch arg name
            ets_sprintf(gv_small_buffer_1,
                        "switch%d",
                        i);
            // Retrieve if present
            if (gv_web_server.hasArg(gv_small_buffer_1)) {
                // Be careful here. Had to strcpy against
                // the address of the first char of the string array
                // just using gv_config.switch_names[i] on its own
                // caused exceptions so it needed to be clearly spelled
                // out in terms of address
                strcpy(&(gv_config.switch_names[i][0]),
                       gv_web_server.arg(gv_small_buffer_1).c_str());
                log_message("Got:%s:%s",
                            gv_small_buffer_1,
                            gv_config.switch_names[i]);
            }

            // format state arg name
            ets_sprintf(gv_small_buffer_1,
                        "state%d",
                        i);
            // Retrieve if present
            if (gv_web_server.hasArg(gv_small_buffer_1)) {
                log_message("Arg %s present",
                            gv_small_buffer_1);

                gv_config.switch_initial_states[i] =
                    atoi(gv_web_server.arg(gv_small_buffer_1).c_str());
                log_message("Got:%s:%d",
                            gv_small_buffer_1,
                            gv_config.switch_initial_states[i]);
            }
            
            // format behaviour arg name
            ets_sprintf(gv_small_buffer_1,
                        "behaviour%d",
                        i);
            // Retrieve if present
            if (gv_web_server.hasArg(gv_small_buffer_1)) {
                log_message("Arg %s present",
                            gv_small_buffer_1);

                gv_config.switch_behaviours[i] =
                    (enum switch_behaviour)atoi(gv_web_server.arg(gv_small_buffer_1).c_str());
                log_message("Got:%s:%d",
                            gv_small_buffer_1,
                            gv_config.switch_behaviours[i]);
            }
        }

        for (i = 0; i < MAX_LEDS; i++) {
            log_message("Getting post args for leds %d/%d",
                        i, MAX_LEDS - 1);
            // format led arg name
            ets_sprintf(gv_small_buffer_1,
                        "led%d",
                        i);
            // Retrieve if present
            if (gv_web_server.hasArg(gv_small_buffer_1)) {
                // Be careful here. Had to strcpy against
                // the address of the first char of the string array
                // just using gv_config.led_names[i] on its own
                // caused exceptions so it needed to be clearly spelled
                // out in terms of address
                strcpy(&(gv_config.led_names[i][0]),
                       gv_web_server.arg(gv_small_buffer_1).c_str());
                log_message("Got:%s:%s",
                            gv_small_buffer_1,
                            gv_config.led_names[i]);
            }

            // format value arg name
            ets_sprintf(gv_small_buffer_1,
                        "program%d",
                        i);
            // Retrieve if present
            if (gv_web_server.hasArg(gv_small_buffer_1)) {
                log_message("Arg %s present",
                            gv_small_buffer_1);

                strcpy(&(gv_config.led_programs[i][0]),
                       gv_web_server.arg(gv_small_buffer_1).c_str());

                log_message("Got:%s:%d",
                            gv_small_buffer_1,
                            gv_config.led_programs[i]);
            }
        }

        for (i = 0; i < MAX_SENSORS; i++) {
            log_message("Getting post args for sensors %d/%d",
                        i, MAX_SENSORS - 1);
            // format sensor arg name
            ets_sprintf(gv_small_buffer_1,
                        "sensor%d",
                        i);
            // Retrieve if present
            if (gv_web_server.hasArg(gv_small_buffer_1)) {
                // Be careful here. Had to strcpy against
                // the address of the first char of the string array
                // just using gv_config.switch_names[i] on its own
                // caused exceptions so it needed to be clearly spelled
                // out in terms of address
                strcpy(&(gv_config.sensor_names[i][0]),
                       gv_web_server.arg(gv_small_buffer_1).c_str());
                log_message("Got:%s:%s",
                            gv_small_buffer_1,
                            gv_config.sensor_names[i]);
            }
        }
    }

    if (store_config) {
        save_config();
        gv_web_server.send(200, "text/html", "Applying settings and rebooting");
        ESP.restart();
    }
    else {
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
void toggle_wifi_led(int delay_msecs)
{
    static int state = 0;

    // toggle
    state = (state + 1) % 2;

    digitalWrite(gv_profile->wifi_led_pin,
                 gv_led_state_reg[state]);

    if (delay > 0) {
        delay(delay_msecs);
    }
}

// Function: start_ota()
// Enables OTA service for formware
// flashing OTA
void start_ota()
{
    static int already_setup = 0;

    // Only do this once
    // lost wifi conections will re-run start_wifi_sta_mode() so
    // we dont want this function called over and over
    if (already_setup) {
        log_message("OTA already started");
        return;
    }

    if (!gv_config.ota_enabled) {
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
        gv_mode = MODE_WIFI_OTA;
    });

    ArduinoOTA.onEnd([]() {
        log_message("OTA End");
    });

    ArduinoOTA.onProgress([](unsigned int progress,
                             unsigned int total) {
        log_message("Progress: %02u%%\r", (progress / (total / 100)));
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
    gv_mode = MODE_WIFI_AP;
    int i;
    char *combi_selected = "selected";
    char *combi_not_selected = "";

    char *switch_initial_on_selected, *switch_initial_off_selected;
    char *ota_on_selected, *ota_off_selected;
    char *telnet_on_selected, *telnet_off_selected;
    char *manual_on_selected, *manual_off_selected;
    char *switch_bhvr_selected[3];

    if (strlen(gv_config.profile) == 0) {
        // combi for profile selection
        // here we build a set of values for the combi
        // based on each profile in the register
        i = 0;
        gv_small_buffer_1[0] = '\0';
        while(gv_profile_register[i].name != NULL) {
            ets_sprintf(gv_small_buffer_2,
                        "        <option value=\"%s\">%s</option>",
                        gv_profile_register[i].name,
                        gv_profile_register[i].name);
            i++;
            strcat(gv_small_buffer_1, gv_small_buffer_2);
        }

        // format the main part of the form
        ets_sprintf(gv_large_buffer,
                    "<h2>%s Setup</h2>"
                    "<form action=\"/\" method=\"post\">"
                    "<div><p>Select desired device profile</p></div>"
                    "<div>"
                    "    <label>Profile:</label>"
                    "    <select name=\"profile\">%s</select>"
                    "</div>",
                    gv_mdns_hostname,
                    gv_small_buffer_1);


        // Terminate form with post button and </form>
        strcat(gv_large_buffer,
               "<br><br>"
               "<div>"
               "    <button>Apply Profile</button>"
               "</div>"
               "</form>");
    }
    else {
        // combi state for OTA
        if (gv_config.ota_enabled) {
            ota_on_selected = combi_selected;
            ota_off_selected = combi_not_selected;
        }
        else {
            ota_on_selected = combi_not_selected;
            ota_off_selected = combi_selected;
        }

        // combi state for Telnet
        if (gv_config.telnet_enabled) {
            telnet_on_selected = combi_selected;
            telnet_off_selected = combi_not_selected;
        }
        else {
            telnet_on_selected = combi_not_selected;
            telnet_off_selected = combi_selected;
        }
        // combi state for Manual switches
        if (gv_config.manual_switches_enabled) {
            manual_on_selected = combi_selected;
            manual_off_selected = combi_not_selected;
        }
        else {
            manual_on_selected = combi_not_selected;
            manual_off_selected = combi_selected;
        }


        // format the main part of the form
        ets_sprintf(gv_large_buffer,
                    "<h2>%s Setup</h2>"
                    "<form action=\"/\" method=\"post\">"
                    "<div>"
                    "    <label>Reset Defaults</label>"
                    "    <select name=\"reset\">"
                    "        <option value=\"0\" selected>No</option>"
                    "        <option value=\"1\" >Yes</option>"
                    "    </select>"
                    "</div>"
                    "<div>"
                    "    <label>Profile: %s</label>"
                    "</div>"
                    "<div>"
                    "    <label>Zone:</label>"
                    "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"zone\">"
                    "</div>"
                    "<div>"
                    "    <label>WIFI SSID:</label>"
                    "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"ssid\">"
                    "</div>"
                    "<div>"
                    "    <label>WIFI Password:</label>"
                    "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"password\">"
                    "</div>"
                    "<div>"
                    "    <label>Temp Offset:</label>"
                    "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"temp_offset\">"
                    "</div>"
                    "<div>"
                    "    <label>OTA Update:</label>"
                    "    <select name=\"ota_enabled\">"
                    "        <option value=\"1\" %s>Enabled</option>"
                    "        <option value=\"0\" %s>Disabled</option>"
                    "    </select>"
                    "</div>"
                    "<div>"
                    "    <label>Telnet:</label>"
                    "    <select name=\"telnet_enabled\">"
                    "        <option value=\"1\" %s>Enabled</option>"
                    "        <option value=\"0\" %s>Disabled</option>"
                    "    </select>"
                    "</div>"
                    "<div>"
                    "    <label>Manual Switches:</label>"
                    "    <select name=\"manual_switches_enabled\">"
                    "        <option value=\"1\" %s>Enabled</option>"
                    "        <option value=\"0\" %s>Disabled</option>"
                    "    </select>"
                    "</div>",
            gv_mdns_hostname,
            gv_config.profile,
            gv_config.zone,
            MAX_FIELD_LEN,
            gv_config.wifi_ssid,
            MAX_FIELD_LEN,
            gv_config.wifi_password,
            MAX_FIELD_LEN,
            gv_config.temp_offset,
            MAX_FIELD_LEN,
            ota_on_selected,
            ota_off_selected,
            telnet_on_selected,
            telnet_off_selected,
            manual_on_selected,
            manual_off_selected);

        // append name entries for switches
        i = 0;
        while (gv_profile->switch_register[i].name) {

            // Set the initial state combi selected strings
            // the one set to "selected" will force the combi
            // to the current value as stored in config
            if (gv_config.switch_initial_states[i] == 1) {
                switch_initial_on_selected = combi_selected;
                switch_initial_off_selected = combi_not_selected;
            }
            else {
                switch_initial_on_selected = combi_not_selected;
                switch_initial_off_selected = combi_selected;
            }

            // Set the behaviour mode
            // using an array of strings here
            // initialised to emptr strings
            // and selected behaviour index then set to selected
            switch_bhvr_selected[0] = combi_not_selected;
            switch_bhvr_selected[1] = combi_not_selected;
            switch_bhvr_selected[2] = combi_not_selected;
            switch_bhvr_selected[gv_config.switch_behaviours[i]] = combi_selected;

            // Format the Switch config segment
            ets_sprintf(gv_small_buffer_2,
                        "<div>"
                        "    <label>Switch %d</label>"
                        "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"switch%d\">"
                        "    <br><label>Initial State</label>"
                        "    <select name=\"state%d\">"
                        "        <option value=\"1\" %s>On</option>"
                        "        <option value=\"0\" %s>Off</option>"
                        "    </select>"
                        "    <br><label>Switch Behaviour</label>"
                        "    <select name=\"behaviour%d\">"
                        "        <option value=\"0\" %s>Toggle</option>"
                        "        <option value=\"1\" %s>On</option>"
                        "        <option value=\"2\" %s>Off</option>"
                        "    </select>"
                        "</div>",
                        i + 1,
                        gv_config.switch_names[i],
                        MAX_FIELD_LEN,
                        i,
                        i,
                        switch_initial_on_selected,
                        switch_initial_off_selected,
                        i,
                        switch_bhvr_selected[0],
                        switch_bhvr_selected[1],
                        switch_bhvr_selected[2]);

            // append to the larger form
            strcat(gv_large_buffer, gv_small_buffer_2);
            i++; // to the next entry in register
        }

        // append name entries for led pins
        i = 0;
        while (gv_profile->led_register[i].name) {

            // Formt the Switch config segment
            ets_sprintf(gv_small_buffer_2,
                        "<div>"
                        "<label>LED %d</label>"
                        "<input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"led%d\">"
                        "<br><label>Program:</label><br>"
                        "<textarea rows=\"5\" cols=\"20\" name=\"program%d\">%s</textarea>"
                        "</div>",
                        i + 1,
                        gv_config.led_names[i],
                        MAX_FIELD_LEN,
                        i,
                        i,
                        gv_config.led_programs[i]);

            // append to the larger form
            strcat(gv_large_buffer, gv_small_buffer_2);
            i++; // to the next entry in register
        }

        // append name entries for sensors
        i = 0;
        while (gv_profile->sensor_register[i].name) {

            // Formt the sensor config segment
            ets_sprintf(gv_small_buffer_2,
                        "<div>"
                        "<label>Sensor %d</label>"
                        "<input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"sensor%d\">"
                        "</div>",
                        i + 1,
                        gv_config.sensor_names[i],
                        MAX_FIELD_LEN,
                        i);

            // append to the larger form
            strcat(gv_large_buffer, gv_small_buffer_2);
            i++; // to the next entry in register
        }

        // Terminate form with post button and </form>
        strcat(gv_large_buffer,
               "<br><br>"
               "<div>"
               "<button>Apply Settings</button>"
               "</div>"
               "</form>");
    }

    // Activate AP mode
    WiFi.disconnect();
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
    gv_web_server.on("/apply", ap_handle_root);
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
// Args "update_ip" amd "update_port" can be used to set an IP:PORT
// for a POST-based push notification to a server wishing to track 
// manual switch activity
void sta_handle_json() {
    unsigned int state;

    log_message("sta_handle_json()");

    // Check for switch control name and state
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("state")) {
        strcpy(gv_small_buffer_1, gv_web_server.arg("control").c_str());
        state = atoi(gv_web_server.arg("state").c_str());
        set_switch_state(gv_small_buffer_1,
                         -1,
                         state,
                         SW_ST_CTXT_NETWORK); // specifying name only
    }

    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("program")) {
        strcpy(gv_small_buffer_1, gv_web_server.arg("control").c_str());
        strcpy(gv_small_buffer_2, gv_web_server.arg("program").c_str());

        set_led_program(gv_small_buffer_1, 
                        -1,
                        gv_small_buffer_2);
    }

    // Set PUSH url for status updates
    if (gv_web_server.hasArg("update_ip") &&
        gv_web_server.hasArg("update_port")) {
        strcpy(gv_push_ip, gv_web_server.arg("update_ip").c_str());
        gv_push_port = atoi(gv_web_server.arg("update_port").c_str());
        log_message("Set push IP:port to %s:%d", gv_push_ip,
                                                   gv_push_port);
    }

    // Return current status as standard
    gv_web_server.send(200, "text/html", get_json_status());

    // reboot if directed
    if (gv_web_server.hasArg("reboot")) {
        log_message("Received reboot command");
        ESP.restart();
    }

    // jump to ap mode if directed
    // This done by making a save to config
    // with a force apmode flag
    if (gv_web_server.hasArg("apmode")) {
        log_message("Received apmode command");
        gv_config.force_apmode_onboot = 1;
        save_config();
        ESP.restart();
    }

    // factory reset if directed
    if (gv_web_server.hasArg("reset")) {
        log_message("Received reset command");
        reset_config();
        save_config();
        ESP.restart();
    }
}

// Function: start_wifi_sta_mode
// Configures the device as a WiFI client
// giving it about 2 minutes to get connected
// Once connected, it sets up the web handlers for root
// and /json
void start_wifi_sta_mode()
{
    log_message("start_wifi_sta_mode()");

    // set state to track wifi down
    // will drive main loop to act accordingly
    gv_mode = MODE_WIFI_STA_DOWN;

    // WIFI
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.hostname(gv_mdns_hostname);
    WiFi.begin(gv_config.wifi_ssid,
               gv_config.wifi_password);
}

void start_sta_mode_services()
{
    log_message("start_sta_mode_services()");

    // Activate switches, sensors & LEDs
    // to reset all to defaults
    setup_switches();
    setup_leds();
    setup_sensors();

    gv_sta_ip = WiFi.localIP();
    log_message("Connected.. IP:%d.%d.%d.%d",
                  gv_sta_ip[0],
                  gv_sta_ip[1],
                  gv_sta_ip[2],
                  gv_sta_ip[3]);

    // MDNS & DNS-SD using "JBHASD"
    log_message("Activating MDNS for hostname:%s", gv_mdns_hostname);
    if (!MDNS.begin(gv_mdns_hostname)) {
        log_message("Error setting up MDNS responder!");
    }
    else {
        log_message("Activating DNS-SD");
        MDNS.addService("JBHASD", "tcp", WEB_PORT);
    }

    // Web server
    gv_web_server.on("/json", sta_handle_json);
    gv_web_server.onNotFound(sta_handle_json);
    gv_web_server.begin();
    log_message("HTTP server started for client mode");

    start_ota();
    start_telnet();
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
    int pin_wait_timer = 25;
    int delay_msecs = 200;
    int button_state;

    gv_mode = MODE_INIT;

    // Get the config at this stage
    // as start_serial needs profiles setup
    load_config();
    start_serial();

    // timer reset
    // helps stop spontaneous watchdog
    // timers resetting the device
    ESP.wdtDisable();
    ESP.wdtEnable(WDTO_8S);

    log_message("Device boot: ChipId:%u FreeHeap:%u ResetReason:%s",
                ESP.getChipId(),
                ESP.getFreeHeap(),
                ESP.getResetReason().c_str());

    // Set mdns hostname based on prefix, chip ID and zone
    // will also use this for AP SSID
    // and OTA mode
    ets_sprintf(gv_mdns_hostname,
                "ESP-%08X-%s",
                ESP.getChipId(),
                gv_config.zone);

    // Init Push IP
    gv_push_ip[0] = '\0';

    // Set up status LED
    pinMode(gv_profile->wifi_led_pin, OUTPUT);

    // forced AP mode from config
    // Before we launch AP mode
    // we first unset this option and save
    // config.. that ensures that if power cycled, 
    // the device will boot as normal and not 
    // remain in AP mode
    if (gv_config.force_apmode_onboot == 1) {
        log_message("Detected forced AP Mode");
        gv_config.force_apmode_onboot = 0;
        save_config();
        start_ap_mode();
        return;
    }

    // If we have no SSID provisioned
    // then we go straight for AP mode
    if (strlen(gv_config.wifi_ssid) == 0) {
        log_message("No legit config present.. going directly to AP mode");
        start_ap_mode();
        return;
    }

    // Assuming we've got legit config in play
    // we give 5 seconds or so to activate the reset pin
    // in the form of 25x200ms delay. That will let us
    // jump into AP mode
    pinMode(gv_profile->boot_program_pin, INPUT_PULLUP);

    log_message("Entering pin wait stage");
    while (pin_wait_timer > 0) {
        log_message("Button wait #%d", pin_wait_timer);
        toggle_wifi_led(delay_msecs);
        button_state = digitalRead(gv_profile->boot_program_pin);
        if (button_state == LOW) {
            log_message("Detected pin down.. going to AP mode");
            start_ap_mode();
            return;
        }
        pin_wait_timer--;
    }

    log_message("Passed pin wait stage.. normal startup");

    // Activate switches, leds and sensors
    setup_switches();
    setup_leds();
    setup_sensors();

    start_wifi_sta_mode();
}

// task loop state machine 
// function wrappers
// Its based on void functions (no aegs, no returns)
// So object-based loop task for web server and 
// several others are manipulated in simple wrappers
// Some of the wrappers are more substantial

void loop_task_check_wifi_down(void)
{
    log_message("loop_task_check_wifi_down()");
    if (WiFi.status() != WL_CONNECTED) {
        log_message("WiFI is down");
        gv_mode = MODE_WIFI_STA_DOWN;

        log_message("Connecting to Wifi SSID:%s, Password:%s",
                    gv_config.wifi_ssid,
                    gv_config.wifi_password);

        start_wifi_sta_mode();
    }
}

void loop_task_check_wifi_up(void)
{
    int i;
    // every 2 secs * 1800.. about 1 hour
    // before it will auto-reboot after losing WiFI
    int max_checks = 1800; 
    static int check_count = 0;

    log_message("loop_task_check_wifi_up()");
    if (WiFi.status() == WL_CONNECTED) {
        log_message("WiFI has come up");
        gv_mode = MODE_WIFI_STA_UP;

        // quick LED burst
        for (i = 1; i <= 50; i++) {
            toggle_wifi_led(50);
        }

        // wifi LED off
        digitalWrite(gv_profile->wifi_led_pin,
                     gv_led_state_reg[0]);

        // reset connect count so that
        // we give a reconnect scenario the same max attempts
        // before reboot
        check_count = 0;

        // start additional services for sta
        // mode
        start_sta_mode_services();
    }
    else {
        // check for max checks and restart
        check_count++;
        if (check_count > max_checks) {
            log_message("Exceeded max checks of %d.. rebooting",
                        max_checks);
            ESP.restart();
        }
    }
}

void loop_task_webserver(void)
{
    gv_web_server.handleClient();
}

void loop_task_wdt(void)
{
    ESP.wdtFeed();
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

// loop tasks
// Each task lists its state machine modes (mask)
// msec delay between calls
// and handler function
// most functions will work fine with 1ms delays
// Others can easily wait longer
// The LED transition was the one that had to get a 
// zero delay to ensure it can compute timing correctly

struct loop_task gv_loop_tasks[] = {

    {
        // WDT Timer
        MODE_ALL,      // Mode
        1,             // msec delay
        loop_task_wdt  // Function
    },

    {
        // Web Server (AP)
        MODE_WIFI_AP | MODE_WIFI_STA_UP, // Mode
        1,                               // msec delay
        loop_task_webserver              // Function
    },
    {
        // DNS Server
        MODE_WIFI_AP,  // Mode
        1,             // msec delay
        loop_task_dns  // Function
    },
    {
        // AP WiFI LED
        MODE_WIFI_AP,       // Mode
        100,                // msec delay every 1/5 sec
        loop_task_wifi_led  // Function
    },

    {
        // STA WiFI LED
        MODE_WIFI_STA_DOWN, // Mode
        1000,               // msec delay every sec
        loop_task_wifi_led  // Function
    },

    {
        // WiFI Check (Down)
        MODE_WIFI_STA_DOWN,      // Mode
        2000,                    // msec delay every 2 secs
        loop_task_check_wifi_up  // Function
    },

    {
        // Manual Switches
        MODE_WIFI_STA_DOWN | MODE_WIFI_STA_UP,   // Mode
        100,                                     // msec delay every 1/10 sec
        check_manual_switches                    // Function
    },

    {
        // WiFI Check (Up)
        MODE_WIFI_STA_UP,          // Mode
        10000,                     // msec delay every 10 secs
        loop_task_check_wifi_down  // Function
    },

    {
        // LED Transtions
        // Requires no delay as the code uses 
        // its own internal msec scheduling
        MODE_WIFI_STA_UP | MODE_WIFI_STA_DOWN,   // Mode
        0,                                       // no delay
        transition_leds                          // Function
    },

    {
        // Telnet Sessions
        MODE_WIFI_STA_UP,      // Mode
        1000,                  // msec delay every 1 second
        handle_telnet_sessions // Function
    },

    {
        // OTA (STA)
        MODE_WIFI_STA_UP | MODE_WIFI_OTA,  // Mode
        1,                                 // 1 msec delay
        loop_task_ota                      // Function
    },

    {
        // terminator.. never delete
        MODE_INIT,
        0,
        NULL // null func ptr terminates loop
    }
};

// Function: loop
// Iterates loop task array state machine and executes
// functions based on gv_mode and msec interval 

void loop()
{
    int i;
    unsigned long now;

    while (gv_loop_tasks[i].fp != NULL) {
        now = millis();

        // Check if mode mask matches current mode
        // and that interval since last call >= delay between calls
        // this is unsigned arithmetic and will nicdely handle a 
        // wrap around of millis()
        if ((gv_loop_tasks[i].mode_mask & gv_mode) &&
            now - gv_loop_tasks[i].last_call 
            >= gv_loop_tasks[i].millis_delay) {
            gv_loop_tasks[i].last_call = now;
            gv_loop_tasks[i].fp();
        }
        i++;
    }
}
