#include "HandyTaskMan.h"
#include "jbhasd_types.h"

// RGB (0-255) -> ESP8266 10-bit PWM (0-1023)
// Applies CIE1931 gamma correction on the perceived
// RGB colour
const uint16_t PROGMEM gamma10[] = {
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4,
    4, 5, 5, 6, 6, 7, 7, 8, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
    14, 15, 15, 16, 17, 17, 18, 19, 19, 20,
    21, 22, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
    40, 42, 43, 44, 45, 47, 48, 50, 51, 52,
    54, 55, 57, 58, 60, 61, 63, 65, 66, 68,
    70, 71, 73, 75, 77, 79, 81, 83, 84, 86,
    88, 90, 93, 95, 97, 99, 101, 103, 106, 108,
    110, 113, 115, 118, 120, 123, 125, 128, 130, 133,
    136, 138, 141, 144, 147, 149, 152, 155, 158, 161,
    164, 167, 171, 174, 177, 180, 183, 187, 190, 194,
    197, 200, 204, 208, 211, 215, 218, 222, 226, 230,
    234, 237, 241, 245, 249, 254, 258, 262, 266, 270,
    275, 279, 283, 288, 292, 297, 301, 306, 311, 315,
    320, 325, 330, 335, 340, 345, 350, 355, 360, 365,
    370, 376, 381, 386, 392, 397, 403, 408, 414, 420,
    425, 431, 437, 443, 449, 455, 461, 467, 473, 480,
    486, 492, 499, 505, 512, 518, 525, 532, 538, 545,
    552, 559, 566, 573, 580, 587, 594, 601, 609, 616,
    624, 631, 639, 646, 654, 662, 669, 677, 685, 693,
    701, 709, 717, 726, 734, 742, 751, 759, 768, 776,
    785, 794, 802, 811, 820, 829, 838, 847, 857, 866,
    875, 885, 894, 903, 913, 923, 932, 942, 952, 962,
    972, 982, 992, 1002, 1013, 1023
};

// Function gpio_tgb_alloc
// allocated RGB controller struct
struct gpio_rgb* gpio_rgb_alloc(void)
{
    struct gpio_rgb *gpio_rgb;

    gpio_rgb = (struct gpio_rgb*) malloc(sizeof(struct gpio_rgb));
    memset(gpio_rgb, 0, sizeof(struct gpio_rgb));

    return gpio_rgb;
}


// Function parse_rgb_colour
// Parses brigtness, red, green and blue
// values from 4-octet int
// Then applies MAX_PWM_VALUE against
// 0-255 ranges of RGB to render into the PWM
// range of the ESP-8266 (0-1023)
// Finally optional brightness value applied against
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

        log_message("Applied Brightness adjustment.. Red:0x%02X Green:0x%02X Blue:0x%02X",
                    red,
                    green,
                    blue);
    }

    // apply mapping to 10-bit (0-1023) PWM via CIE1931 table
    red = pgm_read_word(&gamma10[red]);
    green = pgm_read_word(&gamma10[green]);
    blue = pgm_read_word(&gamma10[blue]);

    log_message("Applied PWM mapping (10-bit CIE1931).. Red:%u Green:%u Blue:%u",
                red,
                green,
                blue);
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

    if (gpio_rgb->program[gpio_rgb->index].fade_delay <= 0) {
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
        if (gpio_rgb->program[gpio_rgb->index].fade_delay > 0 &&
            now - gpio_rgb->timestamp < gpio_rgb->program[gpio_rgb->index].fade_delay) {
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

        log_message("RGB Step.. Timestamp:%lu Delay:%d R:%u G:%u B:%u -> R:%u G:%u B:%u",
                    gpio_rgb->timestamp,
                    gpio_rgb->program[gpio_rgb->index].fade_delay,
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
// Checks active RGB devices and
// progresses to next step in program
// or applies transitions to existing step
void loop_task_transition_rgb(void)
{
    struct gpio_rgb *gpio_rgb;

    for (gpio_rgb = HTM_LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = HTM_LIST_NEXT(gpio_rgb)) {
        if (gpio_rgb->enabled && 
            gpio_rgb->index  == -1) {
            // initial kick
            set_rgb_state(gpio_rgb);
        }
        else if (gpio_rgb->index >= 0) {
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
            else if (!gpio_rgb->single_step && 
                     gpio_rgb->enabled) {
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
// So each step is a semi-colon separated triplet of colour, fade and pause
// Steps are then comma-separated.
// The fade and pause args can be omitted
void set_rgb_program(struct gpio_rgb *gpio_rgb,
                     JsonObject program)
{
    char colour_buffer[50];
    int i;

    if (!gpio_rgb) {
        log_message("No led specified");
        return;
    }
    
    if (program.isNull()) {
        log_message("No program present");
        return;
    }

    log_message("set_rgb_program(name=%s)",
                gpio_rgb->name);

    // init interval protection
    // if set, the millis() miust match or exceed the interval 
    // before we allow a program to be set
    // but we hinge on the enabled property which will not be set initially 
    // until the first program set... allowing that initial call to go through
    if (gpio_rgb->enabled && 
        gpio_rgb->init_interval) {

        if (millis() < gpio_rgb->init_interval * 1000) {
            log_message("ignoring network program event.. init interval in play (%d secs)",
                        gpio_rgb->init_interval);
            return;
        }
        else {
            gpio_rgb->init_interval = 0;
            log_message("init interval now expired and disabled");
        }
    }

    gpio_rgb->timestamp = 0;

    // wipe any existing program
    gpio_rgb->program_len = 0;
    gpio_rgb->single_step = 0;
    gpio_rgb->index = -1;
    if (gpio_rgb->program) {
        free(gpio_rgb->program);
        gpio_rgb->program = NULL;
    }

    JsonArray steps = program["steps"];
    if (steps.isNull()) {
        log_message("No steps array present");
        gpio_rgb->enabled = 0;
        return;
    }

    gpio_rgb->program_len = steps.size();
    log_message("Detected %d steps in program", 
                gpio_rgb->program_len);
    gpio_rgb->program = new led_program_step[gpio_rgb->program_len];

    i = 0;
    for (JsonObject step : steps) {
        if (step.isNull()) {
            log_message("step parse failed");
            continue;
        }

        // parse colour, pause and fade_delay
        gpio_rgb->program[i].random = 0;
        strcpy(colour_buffer, 
               json_get_sval(step["colour"], "random"));
        gpio_rgb->program[i].pause = json_get_ival(step["pause"], 0);
        gpio_rgb->program[i].fade_delay = json_get_ival(step["fade_delay"], 0);

        if (!strcmp(colour_buffer, "random")) {
            gpio_rgb->program[i].colour = 0;
            gpio_rgb->program[i].random = 1;
        }
        else if (strlen(colour_buffer) > 2 &&
                 colour_buffer[0] == '0' &&
                 (colour_buffer[1] == 'x' || colour_buffer[1] == 'X')) {
            gpio_rgb->program[i].colour = 
                strtoul(&colour_buffer[2], NULL, 16);
        }
        else {
            gpio_rgb->program[i].colour = 
                strtoul(colour_buffer, NULL, 10);
        }

        log_message("Colour[%d] %s -> 0x%08X",
                    i,
                    colour_buffer,
                    gpio_rgb->program[i].colour);

        i++;
    }

    // single step check
    if (gpio_rgb->program_len == 1 &&
        !gpio_rgb->program[0].random) {
        log_message("Single Step program detected");
        gpio_rgb->single_step = 1;
    }

    // mark as enabled
    gpio_rgb->enabled = 1;

    // nudge into motion
    set_rgb_state(gpio_rgb);
}


// Function set_rgb_random_program
// Generates a simple random program for the given RGB
void set_rgb_random_program(struct gpio_rgb *gpio_rgb)
{
    static uint8_t variant = 0;
    char *program;

    log_message("set_rgb_random_program(name=%s, variant=%d)",
                gpio_rgb->name, 
                variant);

    switch(variant) {
      case 0:
        // White fixed
        program = "0xFFFFFF";
        break;

      case 1:
        // Red
        program = "0xFF0000";
        break;

      case 2:
        // Green
        program = "0x00FF00";
        break;

      case 3:
        // Blue
        program = "0x0000FF";
        break;

      case 4:
        // Random 1 second
        // no fade
        program = "random;0;1000";
        break;

      case 5:
        // Random 1 second
        // 3ms fade
        program = "random;3;1000";
        break;

      case 6:
        // Random 200ms
        // no fade
        program = "random;0;200";
        break;

      case 7:
        // Random 200ms
        // 1ms fade
        program = "random;1;200";
        break;

      case 8:
        // RGB cycle
        // 10ms fade
        program = "0xFF0000;10;0,0x00FF00;10;0,0x0000FF;10;0";
        break;

      case 9:
      default:
        program = "0x000000";
        // Off
    }

    //set_rgb_program(gpio_rgb, program);
    // FIXME

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

    if (!gpio_rgb->enabled || 
        gpio_rgb->program_len == 0) {
        log_message("program is empty/disabled.. nothing to do");
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
    if (gpio_rgb->index >= 0 &&
        gpio_rgb->program[gpio_rgb->index].pause > 0 &&
        now - gpio_rgb->timestamp < gpio_rgb->program[gpio_rgb->index].pause) {
        return;
    }

    // timestamp activity
    gpio_rgb->timestamp = now;

    // next step in program
    // includes initial transition from -1
    gpio_rgb->index = (gpio_rgb->index + 1) % gpio_rgb->program_len;
    log_message("index set to %d", gpio_rgb->index);

    log_message("Step[%d] colour:0x%08X fade delay:%d pause:%d",
                gpio_rgb->index,
                gpio_rgb->program[gpio_rgb->index].colour,
                gpio_rgb->program[gpio_rgb->index].fade_delay,
                gpio_rgb->program[gpio_rgb->index].pause);


    // generate random colour if applicable
    if (gpio_rgb->program[gpio_rgb->index].random) {
        gpio_rgb->program[gpio_rgb->index].colour = random(0, 0xFFFFFF);
        log_message("Generated random colour.. 0x%06X",
                    gpio_rgb->program[gpio_rgb->index].colour);
    }

    // Parse colour into PWM gamma-mapped values
    parse_rgb_colour(gpio_rgb->program[gpio_rgb->index].colour,
                     end_red,
                     end_green,
                     end_blue);

    // populate into desired state array
    gpio_rgb->desired_states[0] = end_red;
    gpio_rgb->desired_states[1] = end_green;
    gpio_rgb->desired_states[2] = end_blue;
}

void setup_rgb(struct gpio_rgb *gpio_rgb)
{
    int i;

    log_message("setup_rgb(name:%s)", gpio_rgb->name);

    gpio_rgb->enabled = 0;
    gpio_rgb->index = -1;

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

void rgb_init()
{
    if (!HTM_LIST_EMPTY(gv_device.rgb_list)) {
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


