// ESP-8266 Sketch for a standalone device
// using what I call the JBHASD "API" 
// (Json-Based Home Automation with Service Discovery)
// 
// Cormac Long April 2017
//
// The defaults below will work with a Sonoff wifi switch
// but can easily be adapted for other breakouts
// Also includes support for DHT temp sensors

#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <DHT.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// Boot time programming pin
// If grounded within first 5 seconds, it causes the device 
// to enter AP mode for configuration. Pin 0 seems the 
// most compatible pin to use. It matches the Sonoff reset button
// There's no fear here with this pin being used for the switch
// array defined above. This boot function only applies at boot
// time and the pin will be reset later on to the runtime
// behaviour.
int gv_boot_program_pin = 0;

// LED indicator pin for AP and STA connect states
// We use this LED to apply three flashing rates
// for Boot, AP mode and the wifi connect state
// This value matches the SONOFF 
int gv_wifi_led_pin = 13;

// Definition for the use of GPIO pins as 
// switches where one pin can control a relay, another can
// control a LED and a final pin can be used as a manual 
// trigger for the relay.
// All pin selections are optional.
// Values for pins are unsigned char 0-255 and NO_PIN(255) acts
// as the unset value
struct gpio_switch {
    char *name;
    unsigned char relay_pin; // output pin used fro relay
    unsigned char led_pin; // output pin used for LED
    unsigned char manual_pin; // input pin used fro manual toggle
    unsigned char initial_state;
    unsigned char current_state;
};

// Value used to define an unset PIN
// using 255 as we're operating in unsigned char
#define NO_PIN 255

// In memory definition of the gpio switches/leds we have.
// The last record of {NULL, NO_PIN, NO_PIN, NO_PIN, 0, 0} 
// is the terminator for the array so don't delete that.
// The NULL string in the first field is the terminator trigger
// for iterating the array.
//
// The name field acts as a default but may be overridden by config
// So read these entries as the name, relay-pin, LED-pin, manual-pin, 
// init state and current state.
// Either of the three pins can be NO_PIN to select nothing for that option
// init state of 1 is on, 0 is off
// The current state field is used at run-time to hold current on/off state.
// So while it gets initialised here, it will only be set at runtime as 
// items are turned on or off.
// Excluding the last NULL entry, this number of entries 
// in this array should not exceed MAX_SWITCHES
struct gpio_switch gv_switch_register[] = {
    {  "A",      12,     13,     0,  1, 0 }, // Standard Sonoff 
    {  "B",  NO_PIN, NO_PIN, NO_PIN, 0, 0 }, // dummy switch
    {  "C",  NO_PIN, NO_PIN, NO_PIN, 0, 0 }, // dummy switch
    {  "D",  NO_PIN, NO_PIN, NO_PIN, 0, 0 }, // dummy switch
    { NULL,  NO_PIN, NO_PIN, NO_PIN, 0, 0 }  // terminator.. never delete this
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

// Sensor register
// Read as name, type, variant, sensor pin, ref ptr and 2 float variables
// The name acts as default but can be over-ridden by config.
// Ideally, dont add entries of type GP_SENS_TYPE_NONE. These are pure dummy
// sensors and only end up populating a dummy entry in the JSON status
// You can however ddefine a DHT sensor and assign NO_PIN for its read pin
// That will result in it acting with pseudo random values based on cycle count
// Allows you to test the feature without having to use actual sensors.
struct gpio_sensor gv_sensor_register[] = {
    { "Temp",  GP_SENS_TYPE_DHT, DHT21,      14, NULL, 0, 0 }, // Standard Sonoff spare GPIO 14
    { "Wilma", GP_SENS_TYPE_DHT,     0,  NO_PIN, NULL, 0, 0 }, // Fake DHT with no pin
    { NULL,    GP_SENS_TYPE_NONE,    0,       0, NULL, 0, 0 }  // terminator.. never delete
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
#define CFG_MARKER_VAL 0x0B

struct eeprom_config {
    unsigned char marker;
    char zone[MAX_FIELD_LEN];
    char wifi_ssid[MAX_FIELD_LEN];
    char wifi_password[MAX_FIELD_LEN];
    char switch_names[MAX_SWITCHES][MAX_FIELD_LEN];
    unsigned char switch_initial_states[MAX_SWITCHES];
    char sensor_names[MAX_SENSORS][MAX_FIELD_LEN];
    unsigned char ota_enabled;
    unsigned char manual_switches_enabled;
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

// Output buffers
// In an effort to keep the RAM usage low
// It seems best to use a single large and two 
// small buffers to do all string formatting for
// web pages and JSON strings
char gv_large_buffer[4096];
char gv_small_buffer_1[1024];
char gv_small_buffer_2[1024];

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

// Function: set_switch_state
// Sets the desired switch state to the value of the state arg
// The switch is referenced by name or index value
// for the in-memory array
void set_switch_state(const char *name,
                      int index,
                      int state)
{
    int i = 0;
    int found = 0;

    Serial.printf("set_switch_state(name=%s, index=%d, state=%d)\n",
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
            Serial.printf("no name specified for switch.. ignoring\n");
            return;
        }
        // locate the switch by name in register
        while (gv_switch_register[i].name && !found) {
            if (!strcmp(gv_switch_register[i].name, name)) {
                found = 1;
                Serial.printf("found switch in register\n");
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
        gv_switch_register[i].current_state = state;

        if (gv_switch_register[i].relay_pin != NO_PIN) {
            digitalWrite(gv_switch_register[i].relay_pin, 
                         gv_switch_state_reg[state]);    
        }
        if (gv_switch_register[i].led_pin != NO_PIN) {
            digitalWrite(gv_switch_register[i].led_pin, 
                         gv_led_state_reg[state]);
        }
    }
    else {
        Serial.printf("switch not found in register\n");
    }
}

// Function: setup_switches
// Scans the in-memory array and configures the defined switch
// pins including initial states
void setup_switches()
{
    int i;

    Serial.printf("setup_switches()\n");

    // loop until we reach the terminator where
    // name is NULL
    i = 0;
    while (gv_switch_register[i].name) {

        // Over-ride hard-coded name and initial state 
        // with values set in config
        gv_switch_register[i].name = gv_config.switch_names[i];
        gv_switch_register[i].initial_state = gv_config.switch_initial_states[i];

        // Only service switches with set names
        // Allows for the config to disable hard-coded
        // defaults
        if (strlen(gv_switch_register[i].name) > 0) {
            Serial.printf("Setting up switch:%s, initial state:%d\n",
                          gv_switch_register[i].name,
                          gv_switch_register[i].initial_state);

            if (gv_switch_register[i].relay_pin != NO_PIN) {
                Serial.printf("    switch pin:%d\n",
                              gv_switch_register[i].relay_pin);
                pinMode(gv_switch_register[i].relay_pin, OUTPUT);
            }

            if (gv_switch_register[i].led_pin != NO_PIN) {
                Serial.printf("    LED pin:%d\n",
                              gv_switch_register[i].led_pin);
                pinMode(gv_switch_register[i].led_pin, OUTPUT);
            }

            if (gv_switch_register[i].manual_pin != NO_PIN) {
                Serial.printf("    Manual pin:%d\n",
                              gv_switch_register[i].manual_pin);
                pinMode(gv_switch_register[i].manual_pin, INPUT_PULLUP);
            }

            // set initial state
            set_switch_state(gv_switch_register[i].name,
                             i,
                             gv_switch_register[i].initial_state);
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

    //disabled to keep the serial activity quiet
    //Serial.printf("check_manual_switches()\n");
    //delay(delay_msecs);

    if (!gv_config.manual_switches_enabled) {
        //Serial.printf("manual switches disabled.. returning\n");
        return;
    }

    // loop until we reach the terminator where
    // name is NULL
    i = 0;
    while (gv_switch_register[i].name) {

        // Only work with entries with a set switchname
        // and manual pin
        // Excludes non-relevant or config-disabled entries
        if (strlen(gv_switch_register[i].name) > 0 && 
            gv_switch_register[i].manual_pin != NO_PIN) {
            //Serial.printf("Check Manual pin:%d\n", gv_switch_register[i].manual_pin);
            button_state = digitalRead(gv_switch_register[i].manual_pin);
            if (button_state == LOW) {
                Serial.printf("Detected manual push on switch:%s pin:%d\n",
                              gv_switch_register[i].name,
                              gv_switch_register[i].manual_pin);
                set_switch_state(gv_switch_register[i].name,
                                 i,
                                 (gv_switch_register[i].current_state + 1) % 2);
                took_action = 1; // note any activity
            }
        }
        i++; // to the next entry in register
    }

    if (took_action) {
        // protect against a 2nd press detection of any of the switches
        // with a short delay
        delay(delay_msecs);
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

    Serial.printf("setup_sensors()\n");

    // Protect against multiple calls
    // can only really set these sensors up once
    // because of the pointer ref field
    // could try to get smart and call delete on set pointers
    // but its probably safer to just do this once.
    if (already_setup) {
        Serial.printf("already setup (returning)\n");
        return;
    }
    already_setup = 1;

    // loop until we reach the terminator where
    // name is NULL
    i = 0;
    while (gv_sensor_register[i].name) {
        gv_sensor_register[i].name = gv_config.sensor_names[i];
        if (strlen(gv_sensor_register[i].name) > 0) {
            Serial.printf("Setting up sensor %s\n", 
                          gv_sensor_register[i].name);

            switch (gv_sensor_register[i].sensor_type) {
              case GP_SENS_TYPE_NONE:
                // do nothing
                break;

              case GP_SENS_TYPE_DHT:
                Serial.printf("DHT Type %d on pin %d\n", 
                              gv_sensor_register[i].sensor_variant,
                              gv_sensor_register[i].sensor_pin);

                if (gv_sensor_register[i].sensor_pin != NO_PIN) {
                    // Setup DHT temp/humidity sensor and record
                    // class pointer in void* ref 
                    dhtp = new DHT(gv_sensor_register[i].sensor_pin,
                                   gv_sensor_register[i].sensor_variant);
                    gv_sensor_register[i].ref = dhtp;
                }
                else {
                    Serial.printf("Sensor not assigned to pin (fake)\n");
                    // non-pin assigned DHT
                    // for faking/simulation
                    gv_sensor_register[i].ref = NULL;
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

    Serial.printf("read_sensors()\n");

    i = 0;
    while(gv_sensor_register[i].name) {
        if (strlen(gv_sensor_register[i].name) > 0) {
            switch (gv_sensor_register[i].sensor_type) {
              case GP_SENS_TYPE_DHT:
                dhtp = (DHT*)gv_sensor_register[i].ref;

                if (gv_sensor_register[i].sensor_pin != NO_PIN) {
                    // Humidity
                    f1 = dhtp->readHumidity();

                    // Temp Celsius
                    f2 = dhtp->readTemperature();

                    if (isnan(f1) || isnan(f2)) {
                        Serial.printf("Sensor read failed for %s\n", 
                                      gv_sensor_register[i].name);
                    }
                    else {
                        gv_sensor_register[i].f1 = f1;
                        gv_sensor_register[i].f2 = f2;
                    }
                }
                else {
                    // fake the values
                    gv_sensor_register[i].f1 = (ESP.getCycleCount() % 100) + 0.5;
                    gv_sensor_register[i].f2 = ((ESP.getCycleCount() + 
                                                 ESP.getFreeHeap()) % 100) + 0.25;
                }
                Serial.printf("Sensor: %s Humidity: %d.%02d Temperature: %d.%02d\n",
                              gv_sensor_register[i].name,
                              (int)gv_sensor_register[i].f1,
                              float_get_fp(gv_sensor_register[i].f1, 2),
                              (int)gv_sensor_register[i].f2,
                              float_get_fp(gv_sensor_register[i].f2, 2));
                break;
            }
        }

        i++;
    }
}

// Function: get_json_status
// formats and returns a JSON string representing
// the device details, configuration status and system info
const char *get_json_status()
{
    char *str_ptr;
    int i;

    Serial.printf("get_json_status()\n");

    // refresh sensors
    read_sensors();

    /*  JSON specification for the status string we return 
     *  { "name": "%s", "zone": "%s", "ota_enabled" : %d, "manual_switches_enabled" : %d, 
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
    while(gv_switch_register[i].name) {

        // only detail configured switches
        // Those disabled will have had their name
        // set empty
        if (strlen(gv_switch_register[i].name) > 0) {
            if (str_ptr != gv_small_buffer_1) {
                // separator
                str_ptr += ets_sprintf(str_ptr, ", ");
            }
            str_ptr += ets_sprintf(str_ptr,
                                   "{ \"name\": \"%s\", \"type\": \"switch\", "
                                   "\"state\": %d }",
                                   gv_switch_register[i].name,
                                   gv_switch_register[i].current_state);
        }
        i++;
    }

    // sensors
    str_ptr = gv_small_buffer_2;
    gv_small_buffer_2[0] = 0;
    i = 0;
    while(gv_sensor_register[i].name) {
        if (strlen(gv_sensor_register[i].name) > 0) {
            if (str_ptr != gv_small_buffer_2) {
                // separator
                str_ptr += ets_sprintf(str_ptr, ", ");
            }

            switch (gv_sensor_register[i].sensor_type) {
              case GP_SENS_TYPE_NONE:
                // dummy not expecting to go here really
                // but we can put out a dummy entry if only to keep 
                // the JSON valid
                str_ptr += ets_sprintf(str_ptr,
                                       "{ \"name\": \"%s\", \"type\": \"dummy\" }",
                                       gv_sensor_register[i].name);
                break;

              case GP_SENS_TYPE_DHT:
                str_ptr += ets_sprintf(str_ptr,
                                       "{ \"name\": \"%s\", \"type\": \"temp/humidity\", "
                                       "\"humidity\": \"%d.%02d\", "
                                       "\"temp\": \"%d.%02d\" }",
                                       gv_sensor_register[i].name,
                                       (int)gv_sensor_register[i].f1,
                                       float_get_fp(gv_sensor_register[i].f1, 2),
                                       (int)gv_sensor_register[i].f2,
                                       float_get_fp(gv_sensor_register[i].f2, 2));
                break;
            }
        }

        i++;
    }

    ets_sprintf(gv_large_buffer,
                "{ \"name\": \"%s\", "
                "\"zone\": \"%s\", "
                "\"ota_enabled\" : %u, "
                "\"manual_switches_enabled\" : %u, "
                "\"controls\": [%s], "
                "\"sensors\": [%s], "
                "\"system\" : { \"reset_reason\" : \"%s\", \"free_heap\" : %u, "
                "\"chip_id\" : %u, \"flash_id\" : %u, \"flash_size\" : %u, "
                "\"flash_real_size\" : %u, \"flash_speed\" : %u, \"cycle_count\" : %u } }\n",
                gv_mdns_hostname,
                gv_config.zone,
                gv_config.ota_enabled,
                gv_config.manual_switches_enabled,
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

// Function: load_config
// Loads config from EEPROM, checks for the marker
// octet value and resets config to in-memory array
// defaults
void load_config() 
{
    int i;
    Serial.printf("load_config()\n");

    Serial.printf("Read EEPROM data..(%d bytes)\n", sizeof(gv_config));

    EEPROM.begin(sizeof(gv_config) + 10);
    EEPROM.get(0, gv_config);

    if (gv_config.marker != CFG_MARKER_VAL) {
        Serial.printf("marker field not matched to special value.. "
                      "resetting config to defaults\n");
        // memset to 0, empty strings galore
        memset(&gv_config, 0, sizeof(gv_config));

        // populate switch defaults from in-memory array
        i = 0;
        while (gv_switch_register[i].name) {
            strcpy(&(gv_config.switch_names[i][0]),
                   gv_switch_register[i].name);
            gv_config.switch_initial_states[i] = gv_switch_register[i].initial_state;
            i++;
        }

        // populate sensor defaults
        i = 0;
        while (gv_sensor_register[i].name) {
            strcpy(&(gv_config.sensor_names[i][0]),
                   gv_sensor_register[i].name);
            i++;
        }

        // OTA defaults to Enabled
        gv_config.ota_enabled = 1;

        // Manual Switches enabled by default
        gv_config.manual_switches_enabled = 1;

        // Default zone to an init state
        strcpy(gv_config.zone, "Unknown");
    }

    // Print out config details
    Serial.printf("Marker:%02X\n"
                  "Zone:%s\n"
                  "Wifi SSID:%s\n"
                  "Wifi Password:%s\n"
                  "OTA Update:%u\n"
                  "Manual switches:%u\n",
                  gv_config.marker,
                  gv_config.zone,
                  gv_config.wifi_ssid,
                  gv_config.wifi_password,
                  gv_config.ota_enabled,
                  gv_config.manual_switches_enabled);

    // Print values of each switch name
    for (i = 0; i < MAX_SWITCHES; i++) {
        // format switch arg name
        Serial.printf("Switch[%d]:%s state:%d\n", 
                      i, 
                      gv_config.switch_names[i],
                      gv_config.switch_initial_states[i]);
    }
    
    // Print values of each sensor name
    for (i = 0; i < MAX_SENSORS; i++) {
        // format switch arg name
        Serial.printf("Sensor[%d]:%s\n", 
                      i, 
                      gv_config.sensor_names[i]);
    }
}

// Function: save_config
// Writes config to EEPROM
void save_config() 
{
    Serial.printf("save_config()\n");
    int i;

    // set marker field to special value
    gv_config.marker = CFG_MARKER_VAL;

    Serial.printf("Writing EEPROM data..\n");
    Serial.printf("Marker:%d\n"
                  "Zone:%s\n"
                  "Wifi SSID:%s\n"
                  "Wifi Password:%s\n"
                  "OTA Update:%u\n"
                  "Manual switches:%u\n",
                  gv_config.marker,
                  gv_config.zone,
                  gv_config.wifi_ssid,
                  gv_config.wifi_password,
                  gv_config.ota_enabled,
                  gv_config.manual_switches_enabled);

    // Print values of each switch name
    for (i = 0; i < MAX_SWITCHES; i++) {
        // format switch arg name
        Serial.printf("Switch[%d]:%s state:%d\n", 
                      i, 
                      gv_config.switch_names[i],
                      gv_config.switch_initial_states[i]);
    }
    
    // Print values of each sensor name
    for (i = 0; i < MAX_SENSORS; i++) {
        // format switch arg name
        Serial.printf("Sensor[%d]:%s\n", 
                      i, 
                      gv_config.sensor_names[i]);
    }

    EEPROM.begin(sizeof(gv_config) + 10);
    EEPROM.put(0, gv_config);
    EEPROM.commit();
}

// Function: ap_handle_root
// On the initial call, this will display the pre-built
// web form we create from the in-memory array and config
// Showing zone, SSID, password and all named switches
// But the same handler will be invoked on a GET/POST from 
// the form. So the code will test for the "zone" arg and act on
// that to drive population of the config record, save and reboot
void ap_handle_root() {
    int i;

    Serial.printf("ap_handle_root()\n");
    if (gv_web_server.hasArg("zone")) {
        Serial.printf("Detected config post\n");

        strcpy(gv_config.zone, 
               gv_web_server.arg("zone").c_str());
        Serial.printf("Got Zone: %s\n", gv_config.zone);

        strcpy(gv_config.wifi_ssid, 
               gv_web_server.arg("ssid").c_str());
        Serial.printf("Got WiFI SSID: %s\n", gv_config.wifi_ssid);

        strcpy(gv_config.wifi_password, 
               gv_web_server.arg("password").c_str());
        Serial.printf("Got WiFI Password: %s\n", gv_config.wifi_password);
        
        gv_config.ota_enabled = atoi(gv_web_server.arg("ota_enabled").c_str());
        Serial.printf("Got OTA Enabled: %u\n", gv_config.ota_enabled);

        gv_config.manual_switches_enabled = atoi(gv_web_server.arg("manual_switches_enabled").c_str());
        Serial.printf("Got Manual Switches Enabled: %u\n", gv_config.manual_switches_enabled);

        for (i = 0; i < MAX_SWITCHES; i++) {
            Serial.printf("Getting post args for switches %d/%d\n",
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
                Serial.printf("Got:%s:%s\n", 
                              gv_small_buffer_1,
                              gv_config.switch_names[i]);
            }

            // format state arg name
            ets_sprintf(gv_small_buffer_1,
                        "state%d",
                        i);
            // Retrieve if present
            if (gv_web_server.hasArg(gv_small_buffer_1)) {
                Serial.printf("Arg %s present\n",
                              gv_small_buffer_1);

                gv_config.switch_initial_states[i] =
                    atoi(gv_web_server.arg(gv_small_buffer_1).c_str());
                Serial.printf("Got:%s:%d\n", 
                              gv_small_buffer_1, 
                              gv_config.switch_initial_states[i]);
            }
        }
        
        for (i = 0; i < MAX_SENSORS; i++) {
            Serial.printf("Getting post args for sensors %d/%d\n",
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
                Serial.printf("Got:%s:%s\n", 
                              gv_small_buffer_1,
                              gv_config.sensor_names[i]);
            }
        }

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

    digitalWrite(gv_wifi_led_pin, 
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
        Serial.printf("OTA already started\n");
        return;
    }

    if (!gv_config.ota_enabled) {
        Serial.printf("OTA mode not enabled.. returning\n");
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
        Serial.printf("OTA Start\n");

        // Change mode to lock in OTA behaviour
        gv_mode = MODE_WIFI_OTA;
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.printf("\nEnd\n");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, 
                             unsigned int total) {
        Serial.printf("Progress: %02u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: \n", error);
        if (error == OTA_AUTH_ERROR) Serial.printf("Auth Failed\n");
        else if (error == OTA_BEGIN_ERROR) Serial.printf("Begin Failed\n");
        else if (error == OTA_CONNECT_ERROR) Serial.printf("Connect Failed\n");
        else if (error == OTA_RECEIVE_ERROR) Serial.printf("Receive Failed\n");
        else if (error == OTA_END_ERROR) Serial.printf("End Failed\n");
    });
    
    ArduinoOTA.begin();

    Serial.printf("OTA service started\n");
}

// Function: start_ap_mode
// Sets up the device in AP mode
// The function formats the config form used in 
// AP mode. It uses the in-memory array of switches to drive
// this along with config data
void start_ap_mode()
{
    Serial.printf("start_ap_mode()\n");
    gv_mode = MODE_WIFI_AP;
    int i;
    char *combi_selected = "selected";
    char *combi_not_selected = "";

    char *switch_initial_on_selected, *switch_initial_off_selected;
    char *ota_on_selected, *ota_off_selected;
    char *manual_on_selected, *manual_off_selected;

    // combi state for OTA 
    if (gv_config.ota_enabled) {
        ota_on_selected = combi_selected;
        ota_off_selected = combi_not_selected;
    }
    else {
        ota_on_selected = combi_not_selected;
        ota_off_selected = combi_selected;
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

    ets_sprintf(
                gv_large_buffer,
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
                "    <label>OTA Update:</label>"
                "    <select name=\"ota_enabled\">"
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
                gv_config.zone, 
                MAX_FIELD_LEN,
                gv_config.wifi_ssid,
                MAX_FIELD_LEN,
                gv_config.wifi_password,
                MAX_FIELD_LEN,
                ota_on_selected,
                ota_off_selected,
                manual_on_selected,
                manual_off_selected);

    // append name entries for switches    
    i = 0;
    while (gv_switch_register[i].name) {

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

        // Formt the Switch config segment
        ets_sprintf(
                    gv_small_buffer_1,
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
        strcat(gv_large_buffer, gv_small_buffer_1);
        i++; // to the next entry in register
    }

    // append name entries for sensors    
    i = 0;
    while (gv_sensor_register[i].name) {

        // Formt the sensor config segment
        ets_sprintf(
                    gv_small_buffer_1,
                    "<div>"
                    "    <label>Sensor %d</label>"
                    "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"sensor%d\">"
                    "</div>",
                    i + 1,
                    gv_config.sensor_names[i],
                    MAX_FIELD_LEN,
                    i);

        // append to the larger form
        strcat(gv_large_buffer, gv_small_buffer_1);
        i++; // to the next entry in register
    }

    // Terminate form with post button and </form>
    strcat(gv_large_buffer, 
           "<div>"
           "    <button>Apply</button>"
           "</div>"
           "</form>");

    // Activate AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(gv_ap_ip, 
                      gv_ap_ip, 
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(gv_mdns_hostname);

    // Captive DNS to try and force the client to the 
    // landing page
    gv_dns_server.start(DNS_PORT, "*", gv_ap_ip);

    Serial.printf("AP IP:%d.%d.%d.%d\n", 
                  gv_ap_ip[0],
                  gv_ap_ip[1],
                  gv_ap_ip[2],
                  gv_ap_ip[3]);


    gv_web_server.on("/", ap_handle_root);
    gv_web_server.on("/apply", ap_handle_root);
    gv_web_server.onNotFound(ap_handle_root);
    gv_web_server.begin();
    Serial.printf("HTTP server started for AP mode\n"); 
}

// Function: sta_handle_root
// Handles root web page calls while in client
// station modea. Displays a basic page with info and 
// buttons for each configured switch.
// Also handles POST/GET args for "control" and "state"
// to control a desired switch. The on-page buttons make use of this 
// handling 
void sta_handle_root() {
    int switch_state;
    int i;

    Serial.printf("sta_handle_root()\n");

    // Update sensors
    read_sensors();

    // Check for switch and state
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("state")) {
        strcpy(gv_small_buffer_1, gv_web_server.arg("control").c_str());
        switch_state = atoi(gv_web_server.arg("state").c_str());
        set_switch_state(gv_small_buffer_1, -1, switch_state); // specifying name only
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
    while (gv_sensor_register[i].name) {
        if (strlen(gv_sensor_register[i].name) > 0) {
            ets_sprintf(gv_small_buffer_1,
                        "<div>%s&nbsp;f1:%d.%02d f2:%d.%02d</div>",
                        gv_sensor_register[i].name,
                        (int)gv_sensor_register[i].f1,
                        float_get_fp(gv_sensor_register[i].f1, 2),
                        (int)gv_sensor_register[i].f2,
                        float_get_fp(gv_sensor_register[i].f2, 2));
            strcat(gv_large_buffer, gv_small_buffer_1);
        }
        i++; // to the next entry in register
    }

    // append entries for switches
    // as simple on/off switch pairs 
    i = 0;
    while (gv_switch_register[i].name) {
        if (strlen(gv_switch_register[i].name) > 0) {
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
    int switch_state;

    Serial.printf("sta_handle_json()\n");

    // Check for switch and state
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("state")) {
        strcpy(gv_small_buffer_1, gv_web_server.arg("control").c_str());
        switch_state = atoi(gv_web_server.arg("state").c_str());
        set_switch_state(gv_small_buffer_1, -1, switch_state); // specifying name only
    }

    // Return current status as standard
    gv_web_server.send(200, "text/html", get_json_status());

    // reboot if directed
    if (gv_web_server.hasArg("reboot")) {
        Serial.printf("Received reboot command\n");
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

    Serial.printf("start_sta_mode()\n");
    gv_mode = MODE_WIFI_STA;

    // Count attempts and reboot if we exceed max
    connect_count++;
    if (connect_count > max_connect_attempts) {
        Serial.printf("Exceeded max connect attempts of %d.. rebooting\n",
                      max_connect_attempts);
        ESP.restart();
    }

    Serial.printf("Connecting to Wifi SSID:%s, Password:%s, Timeout:%d Attempt:%d/%d\n", 
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
        delay(1000);
        toggle_wifi_led(1000);
        Serial.printf(".");
        connect_timeout--;
    }

    Serial.printf("\n");

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("Timed out trying to connect to %s\n",
                      gv_config.wifi_ssid);
        return;  
    }

    // reset connect count so that 
    // we give a reconnect scenario the same max attempts
    // before reboot
    connect_count = 0;

    // Activate switches
    // to reset all to defaults
    // including the status LED if used
    setup_switches();

    // sensors
    setup_sensors();

    gv_sta_ip = WiFi.localIP();
    Serial.printf("Connected.. IP:%d.%d.%d.%d\n", 
                  gv_sta_ip[0],
                  gv_sta_ip[1],
                  gv_sta_ip[2],
                  gv_sta_ip[3]);

    // MDNS & DNS-SD using "JBHASD"
    Serial.printf("Activating MDNS for hostname:%s\n", gv_mdns_hostname);
    if (!MDNS.begin(gv_mdns_hostname)) {
        Serial.printf("Error setting up MDNS responder!\n");
    }
    else {
        Serial.printf("Activating DNS-SD\n");
        MDNS.addService("JBHASD", "tcp", WEB_PORT);  
    }

    gv_web_server.on("/", sta_handle_root);
    gv_web_server.on("/json", sta_handle_json);
    gv_web_server.onNotFound(sta_handle_root);
    gv_web_server.begin();
    Serial.printf("HTTP server started for client mode\n"); 

    start_ota();
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
    Serial.begin(115200);
    delay(1000);

    gv_mode = MODE_INIT;

    Serial.printf("Device boot: ChipId:%u FreeHeap:%u ResetReason:%s\n",
                  ESP.getChipId(),
                  ESP.getFreeHeap(),
                  ESP.getResetReason().c_str());

    // Get the config at this stage
    load_config();

    // Set mdns hostname based on prefix, chip ID and zone
    // will also use this for AP SSID
    // and OTA mode
    ets_sprintf(gv_mdns_hostname,
                "esp8266-%d-%s",
                ESP.getChipId(),
                gv_config.zone);

    // Activate switches
    // will perform full setup
    // even though we may over-ride some pins
    // helps get relays on ASAP if you have chosen
    // initial state of on
    setup_switches();

    // Set up status LED
    pinMode(gv_wifi_led_pin, OUTPUT);
    
    // If we have no SSID provisioned
    // then we go straight for AP mode
    if (strlen(gv_config.wifi_ssid) == 0) {
        Serial.printf("No legit config present.. going directly to AP mode\n");
        start_ap_mode();
        return;
    }

    // Assuming we've got legit config in play
    // we give 5 seconds or so to activate the reset pin
    // in the form of 25x200ms delay. That will let us 
    // jump into AP mode
    pinMode(gv_boot_program_pin, INPUT_PULLUP);
    int pin_wait_timer = 25;
    int delay_msecs = 200;
    int button_state;

    Serial.printf("Entering pin wait stage\n");
    while (pin_wait_timer > 0) {
        Serial.printf("Button wait #%d\n", pin_wait_timer);
        toggle_wifi_led(delay_msecs);
        button_state = digitalRead(gv_boot_program_pin);
        if (button_state == LOW) {
            Serial.printf("Detected pin down.. going to AP mode\n");
            start_ap_mode();
            return;
        }
        pin_wait_timer--;
    }

    Serial.printf("Passed pin wait stage.. normal startup\n");
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
    switch (gv_mode) {
      case MODE_INIT:
        break;

      case MODE_WIFI_AP:
        gv_dns_server.processNextRequest();
        gv_web_server.handleClient();
        toggle_wifi_led(100); // fast flashing in AP mode
        break;
        
      case MODE_WIFI_STA:
        // reconnect wifi repeatedly if not connected
        if (WiFi.status() != WL_CONNECTED) {
             Serial.printf("Detected WiFI Down in main loop\n");
             start_sta_mode();
        }
        else {
            // normal STA mode loop handlers
            gv_web_server.handleClient();
            ArduinoOTA.handle();
            check_manual_switches();
        }
        break;

      case MODE_WIFI_OTA:
        // pure OTA only behaviour
        ArduinoOTA.handle();
        break;
    }
}
