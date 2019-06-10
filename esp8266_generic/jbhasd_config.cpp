#include "HandyTaskMan.h"
#include "jbhasd_types.h"

// Global config
char gv_config[MAX_CONFIG_LEN];


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
    gv_device.boot_pin = json_cfg["boot_pin"];
    gv_device.status_led_pin = json_cfg["status_led_pin"];
    gv_device.status_led_on_high = json_cfg["status_led_on_high"];
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
