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
#include "jbhasd_types.h"

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

// Global Data buffers
// In an effort to keep the RAM usage low
// It seems best to use a single large and two
// small buffers to do all string formatting for
// web pages and JSON strings
char gv_small_buffer[512];
char gv_large_buffer[4096];
char gv_config[MAX_CONFIG_LEN];

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
    unsigned long now;
    unsigned long days;
    unsigned long hours;
    unsigned long mins;
    unsigned long secs;
    unsigned long msecs;
    unsigned long remainder;

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

    // handle va arg list and write to buffer offset by 
    // existing timestamp length
    va_start(args, format);
    vsnprintf(log_buf + strlen(log_buf),
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
                telnet_clients[i].write((uint8_t*)log_buf, 
                                        strlen(log_buf));
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

    if (!gv_device.telnet_enabled) {
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
                ets_sprintf(gv_small_buffer,
                            "JBHASD %s Logging Console client %d/%d\r\n",
                            gv_mdns_hostname,
                            i + 1,
                            MAX_TELNET_CLIENTS);
                telnet_clients[i].write((uint8_t*)gv_small_buffer, 
                                        strlen(gv_small_buffer));
                continue;
            }
        }

        //no free/disconnected slot so reject
        WiFiClient serverClient = telnet_server.available();
        ets_sprintf(gv_small_buffer,
                    "JBHASD %s Logging Console.. no available slots\r\n",
                    gv_mdns_hostname);
        serverClient.write((uint8_t*)gv_small_buffer, 
                           strlen(gv_small_buffer));
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
void set_switch_state(struct gpio_switch *gpio_switch,
                      unsigned int state,
                      enum switch_state_context context)
{
    if (!gpio_switch) {
        log_message("No switch specified");
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

    // change state as requested
    // Set the current state
    gpio_switch->current_state = state;
    gpio_switch->state_context = context;

    if (gpio_switch->relay_pin != NO_PIN) {
        digitalWrite(gpio_switch->relay_pin,
                     gv_switch_state_reg[state]);
    }
    if (gpio_switch->led_pin != NO_PIN) {
        digitalWrite(gpio_switch->led_pin,
                     gv_led_state_reg[state]);
    }
}

// Function: setup_switches
// Scans the list of configured switches
// and performs the required pin setups
void setup_switches()
{
    struct gpio_switch *gpio_switch;

    log_message("setup_switches()");

    for (gpio_switch = LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = LIST_NEXT(gpio_switch)) {

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

        // set initial state
        set_switch_state(gpio_switch,
                         gpio_switch->current_state,
                         SW_ST_CTXT_INIT);

    }
}

// Function: check_manual_switches
// Scans the input pins of all switches and
// invokes a toggle of the current state if it detects
// LOW state
void check_manual_switches()
{
    int button_state;
    int took_action = 0;
    static unsigned long last_action_timestamp = 0;
    WiFiClient wifi_client;
    int rc;
    struct gpio_switch *gpio_switch;
    char post_buffer[50];

    if (!gv_device.manual_switches_enabled) {
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

    for (gpio_switch = LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = LIST_NEXT(gpio_switch)) {

        // Only work with entries with a set switchname
        // and manual pin
        // Excludes non-relevant or config-disabled entries
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
        }
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

            log_message("pushing update request to host:%s port:%d", 
                        gv_push_ip,
                        gv_push_port);

            ets_sprintf(post_buffer,
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
                wifi_client.println(strlen(post_buffer));
                wifi_client.println();
                wifi_client.print(post_buffer);
                //delay(500);
                if (wifi_client.connected()) {
                    wifi_client.stop();
                }
            }
        }
    }
}

// Function: restore_wifi_led_state
// Restores state of WIFI LED to match
// it's assigned switch state if applicable
void restore_wifi_led_state()
{
    int found = 0;
    struct gpio_switch *gpio_switch;

    log_message("restore_wifi_led_state()");

    // Start by turning off
    digitalWrite(gv_device.wifi_led_pin,
                 gv_led_state_reg[0]);

    // locate the switch by wifi LED pin in register
    for (gpio_switch = LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = LIST_NEXT(gpio_switch)) {
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
        digitalWrite(gv_device.wifi_led_pin,
                     gv_led_state_reg[gpio_switch->current_state]);
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
    static int first_run = 1;
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

    for (gpio_sensor = LIST_NEXT(gv_device.sensor_list);
         gpio_sensor != gv_device.sensor_list;
         gpio_sensor = LIST_NEXT(gpio_sensor)) {
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
    DHT *dhtp;
    float f1, f2;
    struct gpio_sensor *gpio_sensor;

    log_message("read_sensors()");

    for (gpio_sensor = LIST_NEXT(gv_device.sensor_list);
         gpio_sensor != gv_device.sensor_list;
         gpio_sensor = LIST_NEXT(gpio_sensor)) {
        switch (gpio_sensor->sensor_type) {
          case GP_SENS_TYPE_DHT:
            dhtp = (DHT*)gpio_sensor->ref;

            if (gpio_sensor->sensor_pin != NO_PIN) {
                // Humidity
                f1 = dhtp->readHumidity();
                if (isnan(f1)) {
                    log_message("Humidity sensor read failed for %s",
                                gpio_sensor->name);
                }
                else {
                    log_message("Humidity read from sensor %d.%02d",
                                (int)f1,
                                float_get_fp(f1, 2));
                    gpio_sensor->f1 = f1;
                }

                // Temp Celsius
                f2 = dhtp->readTemperature();
                if (isnan(f2)) {
                    log_message("Temperature sensor read failed for %s",
                                gpio_sensor->name);
                }
                else {
                    log_message("Temperature read from sensor %d.%02d",
                                (int)f2,
                                float_get_fp(f2, 2));
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
            log_message("Sensor: %s "
                        "Humidity: %d.%02d "
                        "Temperature: %d.%02d "
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
void parse_rgb_colour(unsigned int colour,
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

// Function shift_rgb
// Shifts RGB values for start_red,
// start_green & start_blue one notch
// each toward the end values
// Used to apply a fading effect on values
void shift_rgb(unsigned short &start_red,
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
// Takes a gpio_rgb object
// and applies a fade step
// toward a new colour setting
void fade_rgb(struct gpio_rgb *gpio_rgb)
{
    unsigned long now;

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

// Function transition_rgb()
// Checks active LED devices and
// progresses to next step in program
// or applies transitions to existing step
void transition_rgb()
{
    struct gpio_rgb *gpio_rgb;

    for (gpio_rgb = LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = LIST_NEXT(gpio_rgb)) {
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
    const char *p, *q; 
    char *r, *s;
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
    strcpy(gpio_rgb->program, program);
    gpio_rgb->program_ptr = NULL;
    gpio_rgb->step = -1;
    gpio_rgb->single_step = 0;

    // nudge into motion
    set_rgb_state(gpio_rgb);
}

// Function: set_rgb_state
// Sets the LED to its next/first program step
// also applies msec interval counting for pauses
// between program steps
void set_rgb_state(struct gpio_rgb *gpio_rgb)
{
    int start_red, start_green, start_blue;
    int end_red, end_green, end_blue;
    unsigned long now;
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
        // we found no step separator
        // so if the program pointer is pointing
        // to the start, then the entire program is a 
        // single step
        if (gpio_rgb->program_ptr == gpio_rgb->program) {
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
    if (strlen(step_buffer) > 2 &&
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

    log_message("setup_rgbs()");

    for (gpio_rgb = LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = LIST_NEXT(gpio_rgb)) {

        gpio_rgb->current_colour = 0;

        set_rgb_program(gpio_rgb,
                        gpio_rgb->program);

        log_message("Setting up LED:%s, initial value:%d",
                    gpio_rgb->name,
                    gpio_rgb->current_colour);

        if (gpio_rgb->red_pin != NO_PIN) {
            log_message("    LED Red pin:%d",
                        gpio_rgb->red_pin);
            pinMode(gpio_rgb->red_pin, OUTPUT);
        }
        if (gpio_rgb->green_pin != NO_PIN) {
            log_message("    LED Green pin:%d",
                        gpio_rgb->green_pin);
            pinMode(gpio_rgb->green_pin, OUTPUT);
        }
        if (gpio_rgb->blue_pin != NO_PIN) {
            log_message("    LED Blue pin:%d",
                        gpio_rgb->blue_pin);
            pinMode(gpio_rgb->blue_pin, OUTPUT);
        }
    }
}

// Function: pin_in_use
// Returns 1 if specified pin is
// found in use in any of the switches,
// sensors or the wifi status pin
int pin_in_use(int pin)
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

    for (gpio_switch = LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = LIST_NEXT(gpio_switch)) {

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

    for (gpio_sensor = LIST_NEXT(gv_device.sensor_list);
         gpio_sensor != gv_device.sensor_list;
         gpio_sensor = LIST_NEXT(gpio_sensor)) {

        if (gpio_sensor->sensor_pin == pin) {
            log_message("pin in use on sensor %s ",
                        gpio_sensor->name);
            return 1;
        }
    }

    for (gpio_rgb = LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = LIST_NEXT(gpio_rgb)) {

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
const char *get_json_status(int pretty)
{
    struct gpio_switch *gpio_switch;
    struct gpio_sensor *gpio_sensor;
    struct gpio_rgb *gpio_rgb;

    log_message("get_json_status(pretty=%d)", pretty);

    // refresh sensors
    read_sensors();

    const int capacity = 4096;
    DynamicJsonBuffer gv_json_buffer(capacity);
    JsonObject& json_status = gv_json_buffer.createObject();

    // top-level fields
    json_status["name"] = gv_mdns_hostname;
    json_status["zone"] = gv_device.zone;
    json_status["wifi_ssid"] = gv_device.wifi_ssid;
    json_status["ota_enabled"] = gv_device.ota_enabled;
    json_status["telnet_enabled"] = gv_device.telnet_enabled;
    json_status["mdns_enabled"] = gv_device.mdns_enabled;
    json_status["manual_switches_enabled"] = gv_device.manual_switches_enabled;
    json_status["provisioned"] = gv_device.provisioned;

    // system section
    JsonObject& system = json_status.createNestedObject("system");
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
    JsonArray& controls_arr = json_status.createNestedArray("controls");

    // switches
    for (gpio_switch = LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = LIST_NEXT(gpio_switch)) {

        JsonObject& obj = controls_arr.createNestedObject();
        obj["name"] = gpio_switch->name;
        obj["type"] = "switch";
        obj["state"] = gpio_switch->current_state;
        obj["context"] = get_sw_context(gpio_switch->state_context);
        obj["behaviour"] = get_sw_behaviour(gpio_switch->switch_behaviour);
    }

    // sensors section
    JsonArray& sensors_arr = json_status.createNestedArray("sensors");

    for (gpio_sensor = LIST_NEXT(gv_device.sensor_list);
         gpio_sensor != gv_device.sensor_list;
         gpio_sensor = LIST_NEXT(gpio_sensor)) {

        JsonObject& obj = sensors_arr.createNestedObject();
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

    // LED RGB section
    JsonArray& rgbs_arr = json_status.createNestedArray("rgb");

    for (gpio_rgb = LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = LIST_NEXT(gpio_rgb)) {

        JsonObject& obj = rgbs_arr.createNestedObject();
        obj["name"] = gpio_rgb->name;
        obj["program"] = gpio_rgb->program;
        obj["current_colour"] = gpio_rgb->current_colour;
        obj["step"] = gpio_rgb->step;
    }

    // Format string in compact or prety format
    if (pretty){
        json_status.prettyPrintTo(gv_large_buffer);
    }
    else {
        json_status.printTo(gv_large_buffer);
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


struct gpio_switch* find_switch(const char *name)
{
    struct gpio_switch *gpio_switch;

    for (gpio_switch = LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = LIST_NEXT(gpio_switch)) {

        if (!strcmp(gpio_switch->name, name)) {
            log_message("found");
            return gpio_switch;
        }
    }

    log_message("not found");
    return NULL;
}

struct gpio_rgb* find_led(const char *name)
{
    struct gpio_rgb *gpio_rgb;

    for (gpio_rgb = LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = LIST_NEXT(gpio_rgb)) {

        if (!strcmp(gpio_rgb->name, name)) {
            log_message("found");
            return gpio_rgb;
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

void update_config(char *field, 
                   const char *sval,
                   int ival,
                   int save_now)
{    
    log_message("update_config()");

    log_message("Current Config:\n%s", gv_config);

    // JSON parse from config
    const int capacity = 4096;
    DynamicJsonBuffer gv_json_buffer(capacity);
    JsonObject& json_cfg = 
        gv_json_buffer.parseObject((const char*)gv_config);

    if (!json_cfg.success()) {
        log_message("JSON decode failed for config");

        // build fresh JSON document
        strcpy(gv_config, "{}");
        JsonObject& json_cfg = gv_json_buffer.createObject();

        if (!json_cfg.success()) {
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
    json_cfg.prettyPrintTo(gv_large_buffer);
    strcpy(gv_config, gv_large_buffer);

    log_message("Config updated to:\n%s", gv_config);

    if (save_now) {
        // Commit to disk
        save_config();
    }
}

// Function: reset_config
// wipes all config 
// puts in sensible defaults
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

    // Mark provisioned state to 0 to label 
    // ready for auto-config
    // also set last field to 1 to commit 
    // the lot to EEPROM
    update_config("provisioned", NULL, 0, 1);
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

    log_message("load_config()");

    // Init of device 
    memset(&gv_device, 0, sizeof(gv_device));

    // Initialise lists for switches, sensors and LEDs
    gv_device.switch_list = gpio_switch_alloc();
    LIST_SELFLINK(gv_device.switch_list);

    gv_device.sensor_list = gpio_sensor_alloc();
    LIST_SELFLINK(gv_device.sensor_list);

    gv_device.rgb_list = gpio_rgb_alloc();
    LIST_SELFLINK(gv_device.rgb_list);

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
    const int capacity = 4096;
    DynamicJsonBuffer gv_json_buffer(capacity);
    JsonObject& json_cfg = 
        gv_json_buffer.parseObject((const char*)gv_config);

    if (!json_cfg.success()) {
        log_message("JSON decode failed for config.. resetting");
        reset_config();
        return;
    }

    // Standard top-level string and int fields
    strcpy(gv_device.zone, json_cfg["zone"]);
    strcpy(gv_device.wifi_ssid, json_cfg["wifi_ssid"]);
    strcpy(gv_device.wifi_password, json_cfg["wifi_password"]);
    gv_device.ota_enabled = json_cfg["ota_enabled"];
    gv_device.telnet_enabled = json_cfg["telnet_enabled"];
    gv_device.mdns_enabled = json_cfg["mdns_enabled"];
    gv_device.manual_switches_enabled = json_cfg["manual_switches_enabled"];
    gv_device.boot_program_pin = json_cfg["boot_pin"];
    gv_device.wifi_led_pin = json_cfg["wifi_led_pin"];
    gv_device.force_apmode_onboot = json_cfg["force_apmode_onboot"];
    gv_device.provisioned = json_cfg["provisioned"];

    JsonArray& controls = json_cfg["controls"];
    if (controls.success()) {
        log_message("Successfully parsed controls array from json cfg");
    }
    else {
        log_message("Failed to parse controls array from json cfg");
        return;
    }

    // Loop through each control
    // each should have a name and type as standard
    for (JsonObject& control : controls) {
        const char* control_name = control["name"];
        const char* control_type = control["type"];

        if (control_name && control_type) {
            log_message("Control:%s, Type:%s",
                        control_name, 
                        control_type);

            if (!strcmp(control_type, "switch")) {
                // switch
                const char* sw_mode = control["sw_mode"];
                const int sw_state = control["sw_state"];
                const int sw_relay_pin = control["sw_relay_pin"];
                const int sw_led_pin = control["sw_led_pin"];
                const int sw_man_pin = control["sw_man_pin"];

                gpio_switch = gpio_switch_alloc();
                LIST_INSERT(gv_device.switch_list, gpio_switch);

                strcpy(gpio_switch->name, control_name);
                gpio_switch->relay_pin = sw_relay_pin;
                gpio_switch->led_pin = sw_led_pin;
                gpio_switch->manual_pin = sw_man_pin;
                gpio_switch->current_state = sw_state;

                gpio_switch->switch_behaviour = SW_BHVR_TOGGLE;
                if (!strcmp(sw_mode, "on")) {
                    gpio_switch->switch_behaviour = SW_BHVR_ON;
                }
                else if (!strcmp(sw_mode, "off")) {
                    gpio_switch->switch_behaviour = SW_BHVR_OFF;
                }

            }

            if (!strcmp(control_type, "temp/humidity")) {
                const int th_pin = control["th_pin"];
                const char *th_variant = control["th_variant"];
                const float th_temp_offset = control["th_temp_offset"];

                gpio_sensor = gpio_sensor_alloc();
                LIST_INSERT(gv_device.sensor_list, gpio_sensor);

                // FIXME on sensor variant parsing 
                strcpy(gpio_sensor->name, control_name);
                gpio_sensor->sensor_type = GP_SENS_TYPE_DHT;
                gpio_sensor->sensor_variant = DHT21;
                gpio_sensor->sensor_pin = th_pin;
                gpio_sensor->temp_offset = th_temp_offset;
            }

            if (!strcmp(control_type, "rgb")) {
                const int red_pin = control["red_pin"];
                const int green_pin = control["green_pin"];
                const int blue_pin = control["blue_pin"];
                const char* program = control["program"];

                gpio_rgb = gpio_rgb_alloc();
                LIST_INSERT(gv_device.rgb_list, gpio_rgb);

                strcpy(gpio_rgb->name, control_name);
                strcpy(gpio_rgb->program, program);
                gpio_rgb->red_pin = red_pin;
                gpio_rgb->green_pin = green_pin;
                gpio_rgb->blue_pin = blue_pin;
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
    int i;
    unsigned int led_value;

    int apply_config = 0; // default

    log_message("ap_handle_root()");

    // check for post args

    if (gv_web_server.hasArg("zone")) {
        // actual normal config updates
        apply_config = 1;

        update_config("zone", 
                      gv_web_server.arg("zone").c_str(),
                      0,
                      0);

        update_config("wifi_ssid", 
                      gv_web_server.arg("ssid").c_str(),
                      0,
                      0);

        update_config("wifi_password", 
                      gv_web_server.arg("password").c_str(),
                      0,
                      1);
    }

    if (apply_config) {
        gv_web_server.send(200, "text/html", "Applying settings and rebooting");
        gv_reboot_requested = 1;
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

    digitalWrite(gv_device.wifi_led_pin,
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
                       gv_mode = MODE_WIFI_OTA;
                       });

    ArduinoOTA.onEnd([]() {
                     log_message("OTA End");
                     });

    ArduinoOTA.onProgress([](unsigned int progress,
                             unsigned int total) {
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

    // format the main part of the form
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
        gv_device.zone,
        MAX_FIELD_LEN,
        gv_device.wifi_ssid,
        MAX_FIELD_LEN,
        gv_device.wifi_password,
        MAX_FIELD_LEN,
        gv_config);

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
// Args "update_ip" amd "update_port" can be used to set an IP:PORT
// for a POST-based push notification to a server wishing to track 
// manual switch activity
void sta_handle_json() {
    unsigned int state;
    int pretty = 0;

    log_message("sta_handle_json()");

    // Check for switch control name and state
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("state")) {
        set_switch_state(find_switch(gv_web_server.arg("control").c_str()),
                         atoi(gv_web_server.arg("state").c_str()),
                         SW_ST_CTXT_NETWORK); // specifying name only
    }

    // LED Program
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("program")) {
        set_rgb_program(find_led(gv_web_server.arg("control").c_str()), 
                        gv_web_server.arg("program").c_str());
    }

    // Set PUSH url for status updates
    if (gv_web_server.hasArg("update_ip") &&
        gv_web_server.hasArg("update_port")) {
        strcpy(gv_push_ip, gv_web_server.arg("update_ip").c_str());
        gv_push_port = atoi(gv_web_server.arg("update_port").c_str());
        log_message("Set push IP:port to %s:%d", 
                    gv_push_ip,
                    gv_push_port);
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

    // config update
    // Copy over specified config
    // flag as provisioned
    // Also set name to actual device name
    if (gv_web_server.hasArg("config")) {
        log_message("Received configure command");
        strcpy(gv_config, gv_web_server.arg("config").c_str());
        update_config("name", gv_mdns_hostname, 0, 0);
        update_config("provisioned", NULL, 1, 1);
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
    gv_mode = MODE_WIFI_STA_DOWN;

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


// Function: check_boot_ap_switch
// Checks for a pressed state on the boot program
// pin to drive a switch to AP mode
void check_boot_ap_switch()
{
    static unsigned char pin_wait_timer = 25;
    int button_state;

    // Can toggle LED with no 
    // delay as the main loop tasks
    // apply the timing
    toggle_wifi_led(0);

    // decrement pin wait timer on each call
    // 25 calls against a 200msec call interval
    // is roughly 5 seconds
    if (pin_wait_timer > 0) {
        log_message("Button wait #%d", pin_wait_timer);
        button_state = digitalRead(gv_device.boot_program_pin);
        if (button_state == LOW) {
            log_message("Detected pin down.. going to AP mode");
            start_ap_mode();
            return;
        }
        pin_wait_timer--;
    }
    else {
        log_message("Passed pin wait stage.. going to STA mode");
        start_wifi_sta_mode();
    }
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
    gv_mode = MODE_INIT;

    // Get the config at this stage
    // as start_serial needs config setup
    load_config();
    start_serial();

    // Watchdog timer reconfigure
    // for longer period (8 seconds)
    // Helps stop spontaneous watchdog
    // timers resetting the device
    ESP.wdtDisable();
    ESP.wdtEnable(WDTO_8S);

    log_message("Device boot: ChipId:%u FreeHeap:%u ResetReason:%s",
                ESP.getChipId(),
                ESP.getFreeHeap(),
                ESP.getResetReason().c_str());

    // Set MDNS hostname based on prefix and chip ID
    ets_sprintf(gv_mdns_hostname,
                "ESP8266-%08X",
                ESP.getChipId());

    // Init Push IP
    gv_push_ip[0] = '\0';

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

    // If we have no zone provisioned
    // then we go straight for AP mode
    if (strlen(gv_device.wifi_ssid) == 0) {
        log_message("No WiFI SSID provisioned.. going directly to AP mode");
        start_ap_mode();
        return;
    }

    // Init Boot program pin for detecting manual
    // jump to AP mode at boot stage
    pinMode(gv_device.boot_program_pin, INPUT_PULLUP);

    // Activate switches, leds and sensors
    setup_switches();
    setup_rgbs();
    setup_sensors();

    log_message("Setup stage complete");
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
                    gv_device.wifi_ssid,
                    gv_device.wifi_password);

        start_wifi_sta_mode();
    }
}

void loop_task_check_wifi_up(void)
{
    int i;
    int status;
    static int check_count = 0;

    // This function gets called every 2 secs
    // So 1800 calls is about 1 hour
    int max_checks_before_reboot = 1800; 

    // Restart WiFI every 60 seconds if we continue
    // to remain disconnected
    int max_checks_before_wifi_restart = 30; 

    log_message("loop_task_check_wifi_up()");

    check_count++;

    status = WiFi.status();
    log_message("WiFI Status: %d", status);

    if (status == WL_CONNECTED) {
        log_message("WiFI is up");
        gv_mode = MODE_WIFI_STA_UP;

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
        // Reboot
        "Reboot",
        MODE_ALL,           // Mode
        5000,               // Every 5 seconds
        loop_task_reboot    // Function
    },

    {
        // AP auto-reboot
        "AP Reboot Timer",
        MODE_WIFI_AP,        // Mode
        300000,              // msec delay 5 mins
        loop_task_ap_reboot  // Function
    },

    {
        // Web Server (AP)
        "Webserver",
        MODE_WIFI_AP | MODE_WIFI_STA_UP, // Mode
        10,                              // msec delay
        loop_task_webserver              // Function
    },

    {
        // DNS Server
        "DNS",
        MODE_WIFI_AP,  // Mode
        10,            // msec delay
        loop_task_dns  // Function
    },

    {
        // Init Mode button push
        "Boot AP Switch",
        MODE_INIT,         // Mode
        200,               // msec delay every 1/5 sec
        check_boot_ap_switch  // Function
    },

    {
        // AP WiFI LED
        "AP Status LED",
        MODE_WIFI_AP,       // Mode
        100,                // msec delay every 1/5 sec
        loop_task_wifi_led  // Function
    },

    {
        // STA WiFI LED
        "STA Status LED",
        MODE_WIFI_STA_DOWN, // Mode
        1000,               // msec delay every sec
        loop_task_wifi_led  // Function
    },

    {
        // WiFI Check (While Down)
        "WiFI Status Up Check",
        MODE_WIFI_STA_DOWN,      // Mode
        2000,                    // msec delay every 2 secs
        loop_task_check_wifi_up  // Function
    },

    {
        // Manual Switches
        "Manual Switch Checks",
        MODE_WIFI_STA_DOWN | MODE_WIFI_STA_UP,   // Mode
        100,                                     // msec delay every 1/10 sec
        check_manual_switches                    // Function
    },

    {
        // WiFI Check (While Up)
        "WiFI Status Down Check",
        MODE_WIFI_STA_UP,          // Mode
        10000,                     // msec delay every 10 secs
        loop_task_check_wifi_down  // Function
    },

    {
        // LED Transtions
        // Requires no delay as the code uses 
        // its own internal msec scheduling
        // Also runs in both STA modes
        // and init mode ensuring LEDs start working right
        // away at boot time even during the 5-sec AP mode 
        // wait
        "PWM LED Transitions",
        MODE_WIFI_STA_UP | MODE_WIFI_STA_DOWN | MODE_INIT, // Mode
        0,                                                 // no delay
        transition_rgb                                    // Function
    },

    {
        // Telnet Sessions
        "Telnet Sessions",
        MODE_WIFI_STA_UP,      // Mode
        1000,                  // msec delay every 1 second
        handle_telnet_sessions // Function
    },

    {
        // OTA (STA)
        "OTA",
        MODE_WIFI_STA_UP | MODE_WIFI_OTA,  // Mode
        1,                                 // 1 msec delay
        loop_task_ota                      // Function
    },

    {
        // Task Stat logging
        "Stat Logging",
        MODE_ALL,           // Mode
        30000,              // Every 30 seconds
        loop_task_log_stats // Function
    },

    {
        // terminator.. never delete
        "Terminator",
        MODE_INIT,
        0,
        NULL // null func ptr terminates loop
    }
};

void loop_task_log_stats(void)
{
    int i = 0;

    log_message("loop_task_log_stats()");
    while (gv_loop_tasks[i].fp != NULL) {
        if (gv_loop_tasks[i].num_calls > 0) {
            log_message("  Task:%s Calls:%u CpuTime:%u",
                        gv_loop_tasks[i].name,
                        gv_loop_tasks[i].num_calls,
                        gv_loop_tasks[i].cpu_time);
        }

        gv_loop_tasks[i].num_calls = 0;
        gv_loop_tasks[i].cpu_time = 0;
        i++;
    }
}


// Function: loop
// Iterates loop task array state machine and executes
// functions based on gv_mode and msec interval 

void loop()
{
    int i = 0;
    unsigned long now;
    static int first_run = 1;

    // Keep the sw watchdog happy
    ESP.wdtFeed();

    while (gv_loop_tasks[i].fp != NULL) {
        now = millis();

        // inits for each task 
        // during very first loop() call
        if (first_run) {
            gv_loop_tasks[i].last_call = 0;
            gv_loop_tasks[i].num_calls = 0;
            gv_loop_tasks[i].cpu_time = 0;
        }

        // Check if mode mask matches current mode
        // and that interval since last call >= delay between calls
        // this is unsigned arithmetic and will nicely handle a 
        // wrap around of millis()
        if ((gv_loop_tasks[i].mode_mask & gv_mode) &&
            now - gv_loop_tasks[i].last_call 
            >= gv_loop_tasks[i].millis_delay) {

            // Record call time
            gv_loop_tasks[i].last_call = now;

            // Call function
            gv_loop_tasks[i].fp();

            // Calculate call stats
            now = millis();
            gv_loop_tasks[i].cpu_time += (now - gv_loop_tasks[i].last_call);
            gv_loop_tasks[i].num_calls++;
        }
        i++;
    }

    // Disable first run handling from now on
    first_run = 0;
}
