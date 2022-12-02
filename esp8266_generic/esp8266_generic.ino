// ESP-8266 Sketch for variations of devices
// using what I call the JBHASD "API"
// (Json-Based Home Automation with Service Discovery)
//
// Cormac Long June 2019


#include "HandyTaskMan.h"
#include "jbhasd_types.h"


// Software Version
// Crude compile-time grab of date and time
// into string
const char *gv_sw_compile_date = "JBHASD-VERSION " __DATE__ ;

// Global device provile constructed from config
struct device_profile gv_device;

// Global reboot flag
uint8_t gv_reboot_requested = 0;

// Loop Task management 
HandyTaskMan TaskMan;

// Function: pin_in_use
// Returns 1 if specified pin is
// found in use in any of the switches,
// sensors or the wifi status pin
// This function sits here as it transits all pin use
// areas
uint8_t pin_in_use(uint8_t pin)
{
    struct gpio_switch *gpio_switch;
    struct gpio_sensor *gpio_sensor;
    struct gpio_rgb *gpio_rgb;

    log_message("pin_in_use(pin=%d)", pin);

    if (gv_device.status_led_pin == pin) {
        log_message("pin in use on wifi status led");
        return 1;
    }

    if (gv_device.boot_pin == pin) {
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

        if (gpio_switch->motion_pin == pin) {
            log_message("pin in use on switch %s motion pin ",
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


// Local loop tasks
void loop_task_reboot(void)
{
    if (gv_reboot_requested) {
        log_message("Calling ESP.restart()");
        ESP.restart();
    }
}

void loop_task_log_stats(void)
{
    TaskMan.log_stats();
}


// Function: setup
// Standard setup initialisation callback function
// Does an initial config load and switch/sensor setup calls.
void setup()
{
    gv_logging = LOGGING_SERIAL;
    Serial.begin(115200);

    // Get the config
    load_config();

    // Set Task manager logging callback
    // Init run state
    TaskMan.set_logger(vlog_message);
    TaskMan.set_run_state(RUN_STATE_INIT);

    // Reboot Check every 5s
    TaskMan.add_task("Reboot Check",
                     RUN_STATE_ALL,
                     5000,
                     loop_task_reboot);

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

    log_message("Device boot: ChipId:%u FreeHeap:%u ResetReason:%s",
                ESP.getChipId(),
                ESP.getFreeHeap(),
                ESP.getResetReason().c_str());

    // Init Boot pin for detecting manual
    // jump to AP mode at boot stage
    pinMode(gv_device.boot_pin, INPUT_PULLUP);

    // Set up optional status LED
    if (gv_device.status_led_pin != NO_PIN) {
        pinMode(gv_device.status_led_pin, OUTPUT);
    }

    // forced AP mode from config
    // Before we launch AP mode
    // we first unset this option and save
    // config.. that ensures that if power cycled, 
    // the device will boot as normal and not 
    // remain in AP mode
    if (gv_device.force_apmode_onboot == 1) {
        log_message("Detected forced AP Mode");
        update_config("force_apmode_onboot", NULL, 0, 1);
        start_wifi_ap_mode();
        return;
    }

    // If we have no SSID set
    // then we go straight for AP mode
    if (strlen(gv_device.wifi_ssid) == 0) {
        log_message("No WiFI SSID set.. going directly to AP mode");
        start_wifi_ap_mode();
        return;
    }

    // Activate switches, leds and sensors
    switch_init();
    rgb_init();
    argb_init();

    log_message("Setup stage complete");
}


// Function: loop
// Except for watchgdog feeding, 
// the entire loop is run by the task manager
void loop()
{
    // Keep the sw watchdog happy
    // ESP.wdtFeed();

    // Tasks
    TaskMan.nudge();

    // sleep as required
    TaskMan.sleep();
}
