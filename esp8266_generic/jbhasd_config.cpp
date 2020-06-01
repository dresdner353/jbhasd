#include "HandyTaskMan.h"
#include "jbhasd_types.h"

// Global config
char gv_config[MAX_CONFIG_LEN];

// Int value wrapper on Arduino JSON
int json_get_ival(JsonVariant variant,
                  int def_ival)
{
    int ival;

    log_message("json_get_ival(def=%d)", def_ival);

    if (variant.isNull()) {
        log_message("returning default %d", def_ival);
        return def_ival;
    }
    else {
        ival = variant;
        log_message("returning config %d", ival);
        return ival;
    }
}

// String value wrapper on Arduino JSON
const char *json_get_sval(JsonVariant variant,
                          const char *def_sval)
{
    const char *sval;
    log_message("json_get_sval(def=%s)", def_sval);

    if (variant.isNull()) {
        log_message("returning default %s", def_sval);
        return def_sval;
    }
    else {
        sval = variant;
        log_message("returning config %s", sval);
        return sval;
    }
}
// Function: save_config
// Writes config to EEPROM
void save_config(void)
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
// value or int value. Also optionally saves
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
void reset_config(void)
{
    struct gpio_switch *gpio_switch;
    struct gpio_sensor *gpio_sensor;
    struct gpio_rgb *gpio_rgb;

    log_message("reset_config()");

    // factory default settings
    // last field on most calls is 0 to 
    // delay EEPROM commit per call
    strcpy(gv_config, "{}");
    update_config("name", gv_device.hostname, 0, 0);
    update_config("zone", "Needs Setup", 0, 0);
    update_config("wifi_ssid", "", 0, 0);
    update_config("wifi_password", "", 0, 0);
    update_config("ota_enabled", NULL, 1, 0);
    update_config("telnet_enabled", NULL, 1, 0);
    update_config("mdns_enabled", NULL, 1, 0);
    update_config("manual_switches_enabled", NULL, 1, 0);
    update_config("boot_pin", NULL, 0, 0);
    update_config("status_led_pin", NULL, NO_PIN, 0);

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
void load_config(void)
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

    // Main three config fields, SSID, password and Zone
    // Wifi SSID is the main man here.. if empty, we
    // reset
    strcpy(gv_device.wifi_ssid, json_cfg["wifi_ssid"]);
    strcpy(gv_device.wifi_password, json_cfg["wifi_password"]);
    strcpy(gv_device.zone, json_get_sval(json_cfg["zone"], "Unknown"));

    // SSID is mandatory
    if (strlen(gv_device.wifi_ssid) == 0) {
        log_message("Empty WiFI SSID.. resetting");
        reset_config();
        return;
    }

    // Boot delay, default is 5 seconds
    gv_device.boot_wait = json_get_ival(json_cfg["boot_wait"], 5);

    // OTA enabled, default 1
    gv_device.ota_enabled = json_get_ival(json_cfg["ota_enabled"], 1);

    // Telnet logging enabled, default 1
    gv_device.telnet_enabled = json_get_ival(json_cfg["telnet_enabled"], 1);

    // MDNS Discovery, default 1
    gv_device.mdns_enabled = json_get_ival(json_cfg["mdns_enabled"], 1);

    // Manual switches enabled, default 1
    gv_device.manual_switches_enabled = json_get_ival(json_cfg["manual_switches_enabled"], 1);

    // Boot pin, default GPIO0
    gv_device.boot_pin = json_get_ival(json_cfg["boot_pin"], 0);

    // Status LED pin, default 255 (none)
    gv_device.status_led_pin = json_get_ival(json_cfg["status_led_pin"], NO_PIN);

    // Status LED high, default 0
    gv_device.status_led_on_high = json_get_ival(json_cfg["status_led_on_high"], 0);

    // Forced AP Mode, default 0
    gv_device.force_apmode_onboot = json_get_ival(json_cfg["force_apmode_onboot"], 0);

    // Configured state, default 0
    gv_device.configured = json_get_ival(json_cfg["configured"], 0);

    // Idle Status for WiFi Restart and reboot, default 0
    gv_device.idle_period_wifi = json_get_ival(json_cfg["idle_period_wifi"], 0);
    gv_device.idle_period_reboot = json_get_ival(json_cfg["idle_period_reboot"], 0);

    JsonArray controls = json_cfg["controls"];
    if (controls.isNull()) {
        log_message("Failed to parse controls array from json cfg");
        return;
    }

    // Loop through each control
    // each should have a name and type as standard
    for (JsonObject control : controls) {

        // contriol name, type and enabled fields
        const char *control_name = json_get_sval(control["name"], "");
        const char *control_type = json_get_sval(control["type"], "");
        uint8_t control_enabled = json_get_ival(control["enabled"], 0);

        // control name and type must be set to something
        if (strlen(control_name) > 0 &&
            strlen(control_type) > 0 &&
            control_enabled) {
            log_message("Control:%s, Type:%s Enabled:%d",
                        control_name, 
                        control_type,
                        control_enabled);

            if (!strcmp(control_type, "switch")) {
                // switch
                gpio_switch = gpio_switch_alloc();
                HTM_LIST_INSERT(gv_device.switch_list, gpio_switch);

                strcpy(gpio_switch->name, control_name);

                // Relay, default no PIN and on is HIGH
                gpio_switch->relay_pin = json_get_ival(control["relay_pin"], NO_PIN);
                gpio_switch->relay_on_high = json_get_ival(control["relay_on_high"], 1);

                // switch LED, default none and on is low
                gpio_switch->led_pin = json_get_ival(control["led_pin"], NO_PIN);
                gpio_switch->led_on_high = json_get_ival(control["led_on_high"], 0);

                // Manual Pin, interval and auto-off
                // default none with 0 second interval and no auto-off
                gpio_switch->manual_pin = json_get_ival(control["manual_pin"], NO_PIN);
                gpio_switch->manual_interval = json_get_ival(control["manual_interval"], 0);
                gpio_switch->manual_auto_off = json_get_ival(control["manual_auto_off"], 0);

                // Initial state, default 0 (off)
                gpio_switch->current_state = json_get_ival(control["init_state"], 0);

                // switch mode.. default toggle
                const char *switch_mode = json_get_sval(control["manual_mode"], "toggle");

                if (!strcmp(switch_mode, "on")) {
                    gpio_switch->switch_behaviour = SW_BHVR_ON;
                }
                else if (!strcmp(switch_mode, "off")) {
                    gpio_switch->switch_behaviour = SW_BHVR_OFF;
                }
                else {
                    // default
                    gpio_switch->switch_behaviour = SW_BHVR_TOGGLE;
                }

                // Motion pin & interval
                // default no pin and zero interval
                gpio_switch->motion_pin = json_get_ival(control["motion_pin"], NO_PIN);
                gpio_switch->motion_interval = json_get_ival(control["motion_interval"], 0);
            }

            if (!strcmp(control_type, "temp/humidity")) {
                gpio_sensor = gpio_sensor_alloc();
                HTM_LIST_INSERT(gv_device.sensor_list, gpio_sensor);

                const char *th_variant = json_get_sval(control["variant"], "DHT11");

                // Default variant DHT11
                // Check for DHT11, DHT21 and DHT22
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

                // sensor PIN, default NO_PIN (Fake)
                gpio_sensor->sensor_pin = json_get_ival(control["pin"], NO_PIN);

                // temp offset, default 0
                gpio_sensor->temp_offset = json_get_ival(control["temp_offset"], 0);
            }

            if (!strcmp(control_type, "rgb")) {
                gpio_rgb = gpio_rgb_alloc();
                HTM_LIST_INSERT(gv_device.rgb_list, gpio_rgb);

                strcpy(gpio_rgb->name, control_name);

                // RGB pins and manual switch
                // all default to NO_PIN
                gpio_rgb->red_pin = json_get_ival(control["red_pin"], NO_PIN);
                gpio_rgb->green_pin = json_get_ival(control["green_pin"], NO_PIN);
                gpio_rgb->blue_pin = json_get_ival(control["blue_pin"], NO_PIN);
                gpio_rgb->manual_pin = json_get_ival(control["manual_pin"], NO_PIN);

                // Default init program and init interval
                strcpy(gpio_rgb->program, json_get_sval(control["program"], ""));
                gpio_rgb->init_interval = json_get_ival(control["init_interval"], 0);
            }

            if (!strcmp(control_type, "argb")) {
                gpio_argb = gpio_argb_alloc();
                HTM_LIST_INSERT(gv_device.argb_list, gpio_argb);

                strcpy(gpio_argb->name, control_name);

                // aRGB pin and neopixel args
                // default to NO_PIN 
                gpio_argb->pin = json_get_ival(control["pin"], 255);
                gpio_argb->manual_pin = json_get_ival(control["manual_pin"], 255);
                gpio_argb->num_leds = json_get_ival(control["num_leds"], 0);
                gpio_argb->neopixel_flags = json_get_ival(control["neopixel_flags"], 0);
                strcpy(gpio_argb->program, json_get_sval(control["program"], ""));
            }
        }
    }
}
