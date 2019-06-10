#include "HandyTaskMan.h"
#include "jbhasd_types.h"

// Function gpio_tgb_alloc
// allocated RGB controller struct
struct gpio_rgb* gpio_rgb_alloc()
{
    struct gpio_rgb *gpio_rgb;

    gpio_rgb = (struct gpio_rgb*) malloc(sizeof(struct gpio_rgb));

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
// Checks active RGB devices and
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
// So each step is a semi-colon separated triplet of colour, fade and pause
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

    set_rgb_program(gpio_rgb, program);

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
// Scans the configured RGB controls and configures the defined led
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


