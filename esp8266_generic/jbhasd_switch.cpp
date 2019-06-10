#include "HandyTaskMan.h"
#include "jbhasd_types.h"

// LOW/HIGH registers for GPIO states
// Used when turning on/off LEDs and relays
// depending on which variant applies
uint8_t gv_high_state_reg[] = { LOW, HIGH };
uint8_t gv_low_state_reg[] = { HIGH, LOW };

// Function gpio_switch_alloc
// allocates gpio switch struct
struct gpio_switch* gpio_switch_alloc()
{
    struct gpio_switch *gpio_switch;

    gpio_switch = (struct gpio_switch*) malloc(sizeof(struct gpio_switch));

    return gpio_switch;
}


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

// Function: restore_status_led_state
// Restores state of status LED to match
// it's assigned switch state if applicable
void restore_status_led_state()
{
    uint8_t found = 0;
    struct gpio_switch *gpio_switch;

    if (gv_device.status_led_pin == NO_PIN) {
        // nothing to do if the pin is not set
        return;
    }

    log_message("restore_status_led_state()");

    // Start by turning off
    if (gv_device.status_led_on_high) {
        digitalWrite(gv_device.status_led_pin,
                     gv_high_state_reg[0]);
    }
    else {
        digitalWrite(gv_device.status_led_pin,
                     gv_low_state_reg[0]);
    }

    // locate the switch by status LED pin value in register
    for (gpio_switch = HTM_LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = HTM_LIST_NEXT(gpio_switch)) {
        if (gpio_switch->led_pin == gv_device.status_led_pin) {
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
        if (gv_device.status_led_on_high) {
            digitalWrite(gv_device.status_led_pin,
                         gv_high_state_reg[gpio_switch->current_state]);
        }
        else {
            digitalWrite(gv_device.status_led_pin,
                         gv_low_state_reg[gpio_switch->current_state]);
        }
    }
    else {
        log_message("no switch found assigned to status LED");
    }

}


// Function: toggle_status_led
// Toggles the status LED on/off with
// the specified delay
// Drives the visual aspect of the boot and run-time 
// sequence to indicate the config mode in play
void toggle_status_led(uint16_t delay_msecs)
{
    static uint8_t state = 0;

    if (gv_device.status_led_pin == NO_PIN) {
        // nothing to do if not set
        return;
    }

    // toggle
    state = (state + 1) % 2;

    // ambiguous use of state here for LOW/HIGH
    // as we don't know the low/high nature of 
    // the LED
    // But given its a toggle, it doesn't matter
    // It will work as a toggle when repeatedly called
    digitalWrite(gv_device.status_led_pin,
                 state);

    if (delay > 0) {
        delay(delay_msecs);
    }
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

    // Task managers for switch checks

    // Init Mode button push every 200ms
    TaskMan.add_task("Boot AP Switch",
                     RUN_STATE_INIT,
                     200,
                     loop_task_check_boot_switch);

    // Manual Switches every 200ms
    TaskMan.add_task("Switch Checks",
                     RUN_STATE_WIFI_STA_DOWN | RUN_STATE_WIFI_STA_UP,
                     200,
                     loop_task_check_switches);
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
    struct gpio_switch *gpio_switch;
    struct gpio_rgb *gpio_rgb;

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
    toggle_status_led(0);

    // decrement pin wait timer on each call
    // 25 calls against a 200msec call interval
    // is roughly 5 seconds
    if (pin_wait_timer > 0) {
        log_message("Boot wait #%d", pin_wait_timer);
        button_state = digitalRead(gv_device.boot_pin);
        if (button_state == LOW) {
            log_message("Detected pin down.. going to AP mode");
            start_wifi_ap_mode();
            return;
        }
        pin_wait_timer--;
    }
    else {
        log_message("Passed boot wait stage.. going to STA mode");
        start_wifi_sta_mode();
    }
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

