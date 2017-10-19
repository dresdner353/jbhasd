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
#include <ESP8266mDNS.h>
#include <DHT.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// Definition for the use of GPIO pins as
// switches where one pin can control a relay, another can
// control a LED and a final pin can be used as a manual
// trigger for the relay.
// All pin selections are optional.
// Values for pins are unsigned char 0-255 and NO_PIN(255) acts
// as the unset value
struct gpio_switch {
    char *name;
    unsigned char relay_pin; // output pin used for relay
    unsigned char led_pin; // output pin used for LED
    unsigned char manual_pin; // input pin used for manual toggle
    unsigned char initial_state;
    unsigned char current_state;
};

// enum type for sensor types
// only one supported for now in DHT
// defined a NONE type for clarity
enum gpio_sensor_type {
    GP_SENS_TYPE_NONE,  // None
    GP_SENS_TYPE_DHT    // DHT-type sensor
};

// Definition for a sensor
// name, type, variant, pin, struct/class obj ref
// and 2 floats to hold values
struct gpio_sensor {
    char *name;
    enum gpio_sensor_type sensor_type;
    unsigned char sensor_variant; // DHT11 DHT22, DHT21 etc
    unsigned char sensor_pin; // pin for sensor
    void *ref; // point to allocated reference struct/class

    // value place holders
    float f1;
    float f2;
};

// Definition for the use of GPIO pins as
// analog PWM to control LED RGB strips and other
// dimmable LED lighting
// Struct supports 3 pins, one each for RGB
// colours. However you can define just a single pin
// if desired for a single colour.
// Best advised to use the blue pin for this
// as it matches the lower octet of the set colour
// value
// There is also a manual switch pin which can be assigned to apply
// random colour changes

# define MAX_PWM_VALUE 1023

struct gpio_led {
    char *name;
    unsigned char red_pin;   // Red pin
    unsigned char green_pin; // Green pin
    unsigned char blue_pin;  // Blue pin

    unsigned char manual_pin;  // manual toggle

    // Set Hue + RGB values
    unsigned int current_state;

    // arrays of desired and current states
    // for pins
    // these are PWM values not RGB
    unsigned short desired_states[3];
    unsigned short current_states[3];

    // fade delay counted as skipped
    // calls to the function using a
    // ticker variable
    unsigned int fade_delay;
    unsigned int ticker;
};

// Value used to define an unset PIN
// using 255 as we're operating in unsigned char
#define NO_PIN 255

// Switch and sensor definitions per device
// variant

// ESP-01
// Using Tx/Rx pins for LED switch functions
// no defined relays
// Pin 2 assigned DHT21 temp/humidity sensor plus
// a fake sensor
struct gpio_switch gv_switch_register_esp01[] = {
    {
        "Tx",       // Name
        NO_PIN,     // Relay Pin
        1,          // LED Pin
        0,          // Manual Pin
        0,          // Init State
        0 },        // Current state
    {
        "Rx",        // Name
        NO_PIN,      // Relay Pin
        3,           // LED Pin
        0,           // Manual Pin
        0,           // Init State
        0 },         // Current state
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        0,
        0
    }
};

struct gpio_sensor gv_sensor_register_esp01[] = {
    {
        "Temp",           // Name
        GP_SENS_TYPE_DHT, // Sensor Type
        DHT21,            // Sensor Variant
        2,                // Pin
        NULL,             // void ref
        0,                // f1
        0                 // f2
    },
    {
        // Fake DHT with no pin
        "Fake",            // Name
        GP_SENS_TYPE_DHT,  // Sensor Type
        0,                 // Sensor Variant
        NO_PIN,            // Pin
        NULL,              // void ref
        0,                 // f1
        0                  // f2
    },
    {
        // terminator.. never delete
        NULL,
        GP_SENS_TYPE_NONE,
        0,
        0,
        NULL,
        0,
        0
    }
};

// Sonoff Basic
// First switch assigned to onboard relay(12) and LED(13)
// Three dummy switches defined then for testing
// Pin 14 assigned to DHT21 sensor plus a fake sensor
struct gpio_switch gv_switch_register_sonoff_basic[] = {
    {
        "A", // Name
        12,  // Relay Pin
        13,  // LED Pin
        0,   // Manual Pin
        1,   // Init State
        0    // Current State
    },
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        0,
        0
    }
};

struct gpio_sensor gv_sensor_register_sonoff_basic[] = {
    {
        "Temp",            // Name
        GP_SENS_TYPE_DHT,  // Sensor Type
        DHT21,             // Sensor Variant
        14,                // Pin
        NULL,              // void ref
        0,                 // f1
        0                  // f2
    },
    {
        "Fake",             // Name
        GP_SENS_TYPE_DHT,   // Sensor Type
        0,                  // Sensor Variant
        NO_PIN,             // Pin
        NULL,               // void ref
        0,                  // f1
        0                   // f2
    }, // Fake DHT with no pin
    {
        // terminator.. never delete
        NULL,
        GP_SENS_TYPE_NONE,
        0,
        0,
        NULL,
        0,
        0
    }
};

// Sonoff S20 mains socket
// main switch assigned to replay on PIN 12
// No LED pin assigned for switch as there is a built-in blue LED
// So PIN 13 is defined for its own switch as a LED indicator with no
// assigned relay
// 1 fake sensor defined
struct gpio_switch gv_switch_register_sonoff_s20[] = {
    {
        // S20 relay+Blue LED GPIO 12
        "Socket",    // Name
        12,          // Relay Pin
        NO_PIN,      // LED Pin
        0,           // Manual Pin
        1,           // Init State
        0            // Current State
    },
    {
        // S20 Green LED GPIO 13
        "Green LED", // Name
        NO_PIN,      // Relay Pin
        13,          // LED Pin
        NO_PIN,      // Manual Pin
        1,           // Init State
        0            // Current State
    },
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        0,
        0
    }
};

struct gpio_sensor gv_sensor_register_sonoff_s20[] = {
    {
        // DHT21 on Tx Pin
        "Temp",             // Name
        GP_SENS_TYPE_DHT,   // Sensor Type
        DHT21,              // Sensor Variant
        1,                  // Pin
        NULL,               // void ref
        0,                  // f1
        0                   // f2
    },
    {
        // terminator.. never delete
        NULL,
        GP_SENS_TYPE_NONE,
        0,
        0,
        NULL,
        0,
        0
    }
};

// H801 LED Wifi Controller
// manual switch set to reset pin 0
// Onboard Red LED assigned to pin 5
// Onboard Green LED assigned to pin 1 and used as WiFI LED
// No sensors for this profile
// RGB PWM Pins Red:15, Green:13, Blue:12
// White1:14, White2:4

struct gpio_switch gv_switch_register_h801[] = {
    {
        "Red LED",  // Name
        NO_PIN,     // Relay Pin
        5,          // LED Pin
        NO_PIN,     // Manual Pin
        0,          // Init State
        0           // Current State
    },
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        0,
        0
    }
};

struct gpio_led gv_led_register_h801[] = {
    {
        "RGB",   // Name
        15,      // Red Pin
        13,      // Green Pin
        12,      // Blue Pin
        0,       // Manual Pin
        0        // Initial state
    },
    {
        "W1",    // Name
        NO_PIN,  // Red Pin
        NO_PIN,  // Green Pin
        14,      // Blue Pin
        NO_PIN,  // Manual Pin
        0        // Initial state
    },
    {
        "W2",     // Name
        NO_PIN,   // Red Pin
        NO_PIN,   // Green Pin
        4,        // Blue Pin
        NO_PIN,   // Manual Pin
        0         // Initial state
    },
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        0
    }
};

struct gpio_sensor gv_sensor_register_h801[] = {
    {
        // terminator.. never delete
        NULL,
        GP_SENS_TYPE_NONE,
        0,
        0,
        NULL,
        0,
        0
    }
};

// Generic empty LED reg for most devices
struct gpio_led gv_led_register_dummy[] = {
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        0
    }
};


// Device Profiles
// The device profile ties together a switch,
// sensor and led register into a single structure
// complete with desired boot program pin and wifi LED
// status pin

struct device_gpio_profile {
    char *name;
    int boot_program_pin;
    int wifi_led_pin;
    struct gpio_switch *switch_register;
    struct gpio_sensor *sensor_register;
    struct gpio_led *led_register;
};

// The final profile register then ties all our in-memory
// arrays together into a set of supported profiles.
// When a device is first flashed, the default profile will be
// set to the first in the list. This can be changed from AP
// mode web I/F

struct device_gpio_profile gv_profile_register[] = {
    {
        "Sonoff Basic",                       // Name
        0,                                    // Boot Pin
        13,                                   // Wifi LED Pin
        &gv_switch_register_sonoff_basic[0],  // Switch Register
        &gv_sensor_register_sonoff_basic[0],  // Sensor Register
        &gv_led_register_dummy[0]             // LED Register
    },
    {
        "Sonoff S20",                         // Name
        0,                                    // Boot Pin
        13,                                   // Wifi LED Pin
        &gv_switch_register_sonoff_s20[0],    // Switch Register
        &gv_sensor_register_sonoff_s20[0],    // Sensor Register
        &gv_led_register_dummy[0]             // LED Register
    },
    {
        "ESP-01",                             // Name
        0,                                    // Boot Pin
        1,                                    // Wifi LED Pin
        &gv_switch_register_esp01[0],         // Switch Register
        &gv_sensor_register_esp01[0],         // Sensor Register
        &gv_led_register_dummy[0]             // LED Register
    },
    {
        "H801 LED Controller",                // Name
        0,                                    // Boot Pin
        1,                                    // Wifi LED Pin
        &gv_switch_register_h801[0],          // Switch Register
        &gv_sensor_register_h801[0],          // Sensor Register
        &gv_led_register_h801[0]              // LED Register
    },
    {
        // terminator.. never delete
        NULL,
        0,
        0,
        NULL,
        NULL,
        NULL
    }
};

// EEPROM Configuration
// It uses a struct of fields which is cast directly against
// the eeprom array. The marker field is used to store a dummy
// value as a means of detecting legit config on the first boot.
// That marker value can be changed as config is remodelled
// to ensure a first read is interpreted as blank/invalid and
// config is then reset

#define MAX_FIELD_LEN 20
#define MAX_SWITCHES 5
#define MAX_SENSORS 5
#define MAX_LEDS 5
#define CFG_MARKER_VAL 0x11

// Pointer to selected profile
// This will set at load_config() stage
struct device_gpio_profile *gv_profile = NULL;

// config struct that we cast into EEPROM
struct eeprom_config {
    unsigned char marker;
    char profile[MAX_FIELD_LEN];
    char zone[MAX_FIELD_LEN];
    char wifi_ssid[MAX_FIELD_LEN];
    char wifi_password[MAX_FIELD_LEN];
    char switch_names[MAX_SWITCHES][MAX_FIELD_LEN];
    unsigned char switch_initial_states[MAX_SWITCHES];
    char sensor_names[MAX_SENSORS][MAX_FIELD_LEN];
    char temp_offset[MAX_FIELD_LEN];
    char led_names[MAX_LEDS][MAX_FIELD_LEN];
    unsigned int led_initial_states[MAX_LEDS];
    unsigned char ota_enabled;
    unsigned char telnet_enabled;
    unsigned char manual_switches_enabled;
    unsigned char force_apmode_onboot;
} gv_config;

// Runtime mode
// Simple enum toggle between modes
// allowing common areas such as loop to
// perform mode-specific tasks
enum gv_mode_enum {
    MODE_INIT,      // Boot mode
    MODE_WIFI_AP,   // WiFI AP Mode (for config)
    MODE_WIFI_STA,  // WiFI station/client (operation mode)
    MODE_WIFI_OTA   // OTA mode invoked from STA mode
};
enum gv_mode_enum gv_mode = MODE_INIT;

// Web and DNS stuff
// Used to serve the web content
// in both AP (config) and STA (client) modes
// DNS server used only in AP mode to provide
// captive DNS and have most phones/tablets
// auto-launch the landing config page
#define WEB_PORT 80
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
#define LOGBUF_MAX 2048

enum gv_logging_enum {
    LOGGING_NONE,
    LOGGING_SERIAL,     // Log to Serial
    LOGGING_NW_CLIENT,  // Log to connected network client
};

enum gv_logging_enum gv_logging = LOGGING_NONE;

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
#define MAX_TELNET_CLIENTS 3
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
                      unsigned int state)
{
    int i = 0;
    int found = 0;

    log_message("set_switch_state(name=%s, index=%d, state=%u)",
                name,
                index,
                state);

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
                             gv_profile->switch_register[i].initial_state);
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
    int delay_msecs = 500;
    int took_action = 0;
    WiFiClient wifi_client;
    int rc;

    //disabled to keep the serial activity quiet
    //log_message("check_manual_switches()");
    //delay(delay_msecs);

    if (!gv_config.manual_switches_enabled) {
        //log_message("manual switches disabled.. returning");
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
                set_switch_state(gv_profile->switch_register[i].name,
                                 i,
                                 (gv_profile->switch_register[i].current_state + 1) % 2);
                took_action = 1; // note any activity
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
                set_led_state(gv_profile->led_register[i].name,
                              i,
                              (unsigned int)random(0x010101,
                                                   0xFFFFFF),
                              200);
                took_action = 1; // note any activity
            }
        }
        i++; // to the next entry in register
    }

    if (took_action) {
        // protect against a 2nd press detection of any of the switches
        // with a short delay
        delay(delay_msecs);

        // Send update push
        if (strlen(gv_push_ip) > 0) {

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
                delay(500);
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
    if (led->fade_delay <= 0) {
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
        // delay skip mechanism
        // Simply incrementing a tick counter
        // and applying MOD on fade_dalay
        // This will skip attempts to fade until
        // we get a MOD of 0
        led->ticker++;
        if (led->ticker % led->fade_delay != 0) {
            return;
        }

        // shift all three RGB values 1 PWM value
        // toward the desired states
        shift_rgb_led(led->current_states[0],
                      led->current_states[1],
                      led->current_states[2],
                      led->desired_states[0],
                      led->desired_states[1],
                      led->desired_states[2]);

        log_message("RGB Step.. Ticker:%u Delay:%d R:%d G:%d B:%d -> R:%d G:%d B:%d",
                    led->ticker,
                    led->fade_delay,
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
// progresses or applies transitions
void transition_leds()
{
    int i;

    i = 0;
    while (gv_profile->led_register[i].name) {

        // Active LED devices with one or more differing current and
        // desired states
        if (strlen(gv_profile->led_register[i].name) > 0 &&
            (gv_profile->led_register[i].desired_states[0] !=
             gv_profile->led_register[i].current_states[0] ||
             gv_profile->led_register[i].desired_states[1] !=
             gv_profile->led_register[i].current_states[1] ||
             gv_profile->led_register[i].desired_states[2] !=
             gv_profile->led_register[i].current_states[2])) {
            fade_rgb(&gv_profile->led_register[i]);
        }

        i++;
    }
}

// Function: set_led_state
// Sets the desired led pins to the desired value
// using an optional msec delay to perform a fade
// effect
// The led is referenced by name or index value
// for the in-memory array
void set_led_state(const char *name,
                   int index,
                   unsigned int state,
                   int fade_delay)
{
    int i = 0;
    int found = 0;
    int start_red, start_green, start_blue;
    int end_red, end_green, end_blue;

    log_message("set_led_state(name=%s, index=%d, state=0x%08X)",
                name,
                index,
                state);

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

    // change state as requested
    if (found) {
        // set desired delay
        // and reset ticker;
        gv_profile->led_register[i].fade_delay = fade_delay;
        gv_profile->led_register[i].ticker = 0;

        // Set desired colour (state)
        gv_profile->led_register[i].current_state = state;

        // parse the desired state into PWM
        // values
        parse_colour(state,
                     end_red,
                     end_green,
                     end_blue);

        // populate into desired state array
        gv_profile->led_register[i].desired_states[0] = end_red;
        gv_profile->led_register[i].desired_states[1] = end_green;
        gv_profile->led_register[i].desired_states[2] = end_blue;
    }
    else {
        log_message("led not found in register");
    }
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
        gv_profile->led_register[i].current_state = gv_config.led_initial_states[i];

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
            // set initial value
            // this is a nudge on its own value
            // but will setup the colour parses etc
            set_led_state(gv_profile->led_register[i].name,
                          i,
                          gv_profile->led_register[i].current_state,
                          0);
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
    int i;

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
     *  Control: { "name": "%s", "type": "%s", "state": %d }
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
                                   "\"state\": %u, \"state_hex\": \"0x%08X\" }",
                                   gv_profile->switch_register[i].name,
                                   gv_profile->switch_register[i].current_state,
                                   gv_profile->switch_register[i].current_state);
        }
        i++;
    }

    // LEDs
    i = 0;
    while(gv_profile->led_register[i].name) {

        // only detail configured led pins
        // Those disabled will have had their name
        // set empty
        if (strlen(gv_profile->led_register[i].name) > 0) {
            if (str_ptr != gv_small_buffer_1) {
                // separator
                str_ptr += ets_sprintf(str_ptr, ", ");
            }
            str_ptr += ets_sprintf(str_ptr,
                                   "{ \"name\": \"%s\", \"type\": \"led\", "
                                   "\"state\": %u, \"state_hex\": \"0x%08X\" }",
                                   gv_profile->led_register[i].name,
                                   gv_profile->led_register[i].current_state,
                                   gv_profile->led_register[i].current_state);
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
                "\"flash_real_size\" : %u, \"flash_speed\" : %u, \"cycle_count\" : %u } }\n",
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
                ESP.getCycleCount());

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
                gv_config.led_initial_states[i] = gv_profile->led_register[i].current_state;
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
        log_message("Switch[%d]:%s state:%d",
                    i,
                    gv_config.switch_names[i],
                    gv_config.switch_initial_states[i]);
    }

    // Print values of each sensor name
    for (i = 0; i < MAX_SENSORS; i++) {
        log_message("Sensor[%d]:%s",
                    i,
                    gv_config.sensor_names[i]);
    }

    // Print values of each LED name
    for (i = 0; i < MAX_LEDS; i++) {
        log_message("LED[%d]:%s value:0x%08X",
                    i,
                    gv_config.led_names[i],
                    gv_config.led_initial_states[i]);
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
        log_message("Switch[%d]:%s state:%d",
                      i,
                      gv_config.switch_names[i],
                      gv_config.switch_initial_states[i]);
    }

    // Print values of each sensor name
    for (i = 0; i < MAX_SENSORS; i++) {
        log_message("Sensor[%d]:%s",
                      i,
                      gv_config.sensor_names[i]);
    }

    // Print values of each LED name
    for (i = 0; i < MAX_LEDS; i++) {
        log_message("LED[%d]:%s value:0x%08X",
                    i,
                    gv_config.led_names[i],
                    gv_config.led_initial_states[i]);
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
                        "ledv%d",
                        i);
            // Retrieve if present
            if (gv_web_server.hasArg(gv_small_buffer_1)) {
                log_message("Arg %s present",
                            gv_small_buffer_1);

                strcpy(gv_small_buffer_2, gv_web_server.arg(gv_small_buffer_1).c_str());

                if (strlen(gv_small_buffer_2) > 2 &&
                    gv_small_buffer_2[0] == '0' &&
                    (gv_small_buffer_2[1] == 'x' || gv_small_buffer_2[1] == 'X')) {
                    // hex decode
                    led_value = strtoul(&gv_small_buffer_2[2], NULL, 16);
                }
                else {
                    // decimal unsigned int
                    led_value = strtoul(gv_small_buffer_2, NULL, 10);
                }
                gv_config.led_initial_states[i] = led_value;

                log_message("Got:%s:%d",
                            gv_small_buffer_1,
                            gv_config.led_initial_states[i]);
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
        // unset the force apmode option
        gv_config.force_apmode_onboot = 0;
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

    delay(delay_msecs);
}

// Function: start_ota()
// Enables OTA service for formware
// flashing OTA
void start_ota()
{
    static int already_setup = 0;

    // Only do this once
    // lost wifi conections will re-run start_sta_mode() so
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

            // Format the Switch config segment
            ets_sprintf(gv_small_buffer_2,
                        "<div>"
                        "    <label>Switch %d</label>"
                        "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"switch%d\">"
                        "    <select name=\"state%d\">"
                        "        <option value=\"1\" %s>On</option>"
                        "        <option value=\"0\" %s>Off</option>"
                        "    </select>"
                        "</div>",
                        i + 1,
                        gv_config.switch_names[i],
                        MAX_FIELD_LEN,
                        i,
                        i,
                        switch_initial_on_selected,
                        switch_initial_off_selected);

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
                        "    <label>LED %d</label>"
                        "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"led%d\">"
                        "    <input type=\"text\" value=\"%d\" maxlength=\"8\" name=\"ledv%d\">"
                        "</div>",
                        i + 1,
                        gv_config.led_names[i],
                        MAX_FIELD_LEN,
                        i,
                        gv_config.led_initial_states[i],
                        i);

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
                        "    <label>Sensor %d</label>"
                        "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"sensor%d\">"
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
               "    <button>Apply Settings</button>"
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

// Function: sta_handle_root
// Handles root web page calls while in client
// station modea. Displays a basic page with info and
// buttons for each configured switch.
// Also handles POST/GET args for "control" and "state"
// to control a desired switch. The on-page buttons make use of this
// handling
void sta_handle_root() {
    int control_state;
    int i;

    log_message("sta_handle_root()");

    // Update sensors
    read_sensors();

    // Check for switch and state
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("state")) {
        strcpy(gv_small_buffer_1, gv_web_server.arg("control").c_str());
        control_state = atoi(gv_web_server.arg("state").c_str());
        set_switch_state(gv_small_buffer_1, -1, control_state); // specifying name only
    }

    // Will display basic info page
    // with on/off buttons per configured
    // switch
    ets_sprintf(gv_large_buffer,
                "<div>hostname:&nbsp;%s</div>"
                "<div>Zone:&nbsp;%s</div>",
                gv_mdns_hostname,
                gv_config.zone);

    // append details on sensors
    i = 0;
    while (gv_profile->sensor_register[i].name) {
        if (strlen(gv_profile->sensor_register[i].name) > 0) {
            ets_sprintf(gv_small_buffer_1,
                        "<div>%s&nbsp;f1:%d.%02d f2:%d.%02d</div>",
                        gv_profile->sensor_register[i].name,
                        (int)gv_profile->sensor_register[i].f1,
                        float_get_fp(gv_profile->sensor_register[i].f1, 2),
                        (int)gv_profile->sensor_register[i].f2,
                        float_get_fp(gv_profile->sensor_register[i].f2, 2));
            strcat(gv_large_buffer, gv_small_buffer_1);
        }
        i++; // to the next entry in register
    }

    // append entries for switches
    // as simple on/off switch pairs
    i = 0;
    while (gv_profile->switch_register[i].name) {
        if (strlen(gv_profile->switch_register[i].name) > 0) {
            ets_sprintf(gv_small_buffer_1,
                        "<div><a href=\"/?control=%s&state=1\"><button>%s On</button></a>&nbsp;"
                        "<a href=\"/?control=%s&state=0\"><button>%s Off</button></a></div>",
                        gv_config.switch_names[i],
                        gv_config.switch_names[i],
                        gv_config.switch_names[i],
                        gv_config.switch_names[i]);
            strcat(gv_large_buffer, gv_small_buffer_1);
        }
        i++; // to the next entry in register
    }

    gv_web_server.send(200, "text/html", gv_large_buffer);
}

// Function: sta_handle_json
// This is the API handler that takes in POST/GET args for
// "control" and "state" and sets the desired comntrol to the
// specified state.
// Then it prints out the JSON status string listing device name
// and details along with the configured controls and their states
// One last check is made for a "reboot" arg and if present, the device
// reboots
void sta_handle_json() {
    unsigned int state;
    int delay;

    log_message("sta_handle_json()");

    // Check for switch/led control name and state
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("state")) {
        strcpy(gv_small_buffer_1, gv_web_server.arg("control").c_str());
        strcpy(gv_small_buffer_2, gv_web_server.arg("state").c_str());

        // Hex/Decimal decode of sate
        if (strlen(gv_small_buffer_2) > 2 &&
            gv_small_buffer_2[0] == '0' &&
            (gv_small_buffer_2[1] == 'x' || gv_small_buffer_2[1] == 'X')) {
            // hex decode
            state = strtoul(&gv_small_buffer_2[2], NULL, 16);
        }
        else {
            // decimal unsigned int
            state = strtoul(gv_small_buffer_2, NULL, 10);
        }
        if (gv_web_server.hasArg("delay")) {
            delay = atoi(gv_web_server.arg("delay").c_str());
        }
        else {
            delay = 0;
        }

        // Try both switch and LEDs for this
        // should only match one
        set_switch_state(gv_small_buffer_1,
                         -1,
                         state); // specifying name only

        set_led_state(gv_small_buffer_1,
                      -1,
                      state,
                      delay); // specifying name only
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

// Function: start_sta_mode
// Configures the device as a WiFI client
// giving it about 2 minutes to get connected
// Once connected, it sets up the web handlers for root
// and /json
void start_sta_mode()
{
    int connect_timeout = 60;
    int max_connect_attempts = 5;
    static int connect_count = 0; // track #times we try to connect
    int i;

    log_message("start_sta_mode()");
    gv_mode = MODE_WIFI_STA;

    // Count attempts and reboot if we exceed max
    connect_count++;
    if (connect_count > max_connect_attempts) {
        log_message("Exceeded max connect attempts of %d.. rebooting",
                      max_connect_attempts);
        ESP.restart();
    }

    log_message("Connecting to Wifi SSID:%s, Password:%s, Timeout:%d Attempt:%d/%d",
                  gv_config.wifi_ssid,
                  gv_config.wifi_password,
                  connect_timeout,
                  connect_count,
                  max_connect_attempts);

    // WIFI
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(gv_config.wifi_ssid,
               gv_config.wifi_password);

    while (WiFi.status() != WL_CONNECTED &&
           connect_timeout > 0) {
        toggle_wifi_led(1000);
        log_message("WiFI Connect.. %d", connect_timeout);
        connect_timeout--;
    }

    if (WiFi.status() != WL_CONNECTED) {
        log_message("Timed out trying to connect to %s",
                      gv_config.wifi_ssid);
        return;
    }

    // reset connect count so that
    // we give a reconnect scenario the same max attempts
    // before reboot
    connect_count = 0;

    // 50 quick flashes to signal WIFI connected
    for (i = 1; i <= 50; i++) {
        toggle_wifi_led(50);
    }

    // wifi LED off
    digitalWrite(gv_profile->wifi_led_pin,
                 gv_led_state_reg[0]);

    // Activate switches
    // to reset all to defaults
    setup_switches();
    setup_leds();

    // sensors
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

    gv_web_server.on("/", sta_handle_root);
    gv_web_server.on("/json", sta_handle_json);
    gv_web_server.onNotFound(sta_handle_root);
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
                "esp8266-%d-%s",
                ESP.getChipId(),
                gv_config.zone);

    // Init Push IP
    gv_push_ip[0] = '\0';

    // Activate switches and leds
    // will perform full setup
    // even though we may over-ride some pins
    // helps get relays on ASAP if you have chosen
    // initial state of on
    setup_switches();
    setup_leds();

    // Set up status LED
    pinMode(gv_profile->wifi_led_pin, OUTPUT);

    // forced AP mode from config
    if (gv_config.force_apmode_onboot == 1) {
        log_message("Detected forced AP Mode");
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
    int pin_wait_timer = 25;
    int delay_msecs = 200;
    int button_state;

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
    start_sta_mode();
}

// Function: loop
// Main loop driver callback.
// In both AP and STA modes, it drives the web server
// AP mode must addtionally operate the DNS server, and toggle
// its LED state.
// STA mode is also calling check_manual_switches() to catch
// any user intervention to turn things on/off in normal use.
// STA mode also calls the OTA handler to drive any OTA updating
// while in STA mode
// If WiFI is detected down in STA mode, it simply calls start_sta_mode()
// again which will repeat the connect attempt. Helps cater for periodic
// loss of WiFI
void loop()
{
    // timer reset
    // for preventing watchdog resets
    ESP.wdtFeed();

    // wifi check interval
    static unsigned long wifi_last_check = 0;
    unsigned long now;

    switch (gv_mode) {
      case MODE_INIT:
        break;

      case MODE_WIFI_AP:
        gv_dns_server.processNextRequest();
        gv_web_server.handleClient();
        toggle_wifi_led(100); // fast flashing in AP mode
        break;

      case MODE_WIFI_STA:
        // check wifi every 5s initiating
        // sta mode again if required
        now = millis();
        if (now - wifi_last_check >= 5000) {
            wifi_last_check = now;
            //log_message("Checking wifi status");
            if (WiFi.status() != WL_CONNECTED) {
                 log_message("Detected WiFI Down in main loop");
                 start_sta_mode();
            }
        }
        else {
            // normal STA mode loop handlers
            gv_web_server.handleClient();
            ArduinoOTA.handle();
            check_manual_switches();
            transition_leds();
            handle_telnet_sessions();
        }
        break;

      case MODE_WIFI_OTA:
        // pure OTA only behaviour
        ArduinoOTA.handle();
        break;
    }
}
