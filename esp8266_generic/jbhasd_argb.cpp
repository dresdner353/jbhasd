#include "HandyTaskMan.h"
#include "jbhasd_types.h"

struct gpio_argb* gpio_argb_alloc(void)
{
    struct gpio_argb *gpio_argb;

    gpio_argb = (struct gpio_argb*) malloc(sizeof(struct gpio_argb));
    memset(gpio_argb, 0, sizeof(struct gpio_argb));

    return gpio_argb;
}

// rainbow effect adapted from Neopixel examples
// modified here to atore the firstPixelHue value statically
void rainbow(struct gpio_argb *gpio_argb) {
    static long firstPixelHue = 0; 
    for(int i = 0; i < gpio_argb->neopixel->numPixels(); i++) { 
        int pixelHue = firstPixelHue + (i * 65536L / gpio_argb->neopixel->numPixels());
        gpio_argb->neopixel->setPixelColor(i, 
                                           gpio_argb->neopixel->gamma32(gpio_argb->neopixel->ColorHSV(pixelHue)));
    }
    firstPixelHue = (firstPixelHue + 256) % 65536;
}

// chasing rainbow effect adapted from Neopixel 
// examples. Some variabls made static to hold state 
// between calls
void chase_rainbow(struct gpio_argb *gpio_argb) {
    static int firstPixelHue = 0;
    static int b = 0; 
    gpio_argb->neopixel->clear(); 
    for(int c = b; c < gpio_argb->neopixel->numPixels(); c += 3) {
        int hue = firstPixelHue + c * 65536L / gpio_argb->neopixel->numPixels();
        uint32_t color = gpio_argb->neopixel->gamma32(gpio_argb->neopixel->ColorHSV(hue)); 
        gpio_argb->neopixel->setPixelColor(c, color); 
    }
    firstPixelHue += 65536 / 90; 
    b = (b + 1) % 3;
}

void random_leds(struct gpio_argb *gpio_argb) {
    static uint8_t first_run = 1;
    uint16_t index, i;
    uint32_t colour;

    if (first_run) {
        gpio_argb->neopixel->clear(); 
        first_run = 0;
    }

    // Set random colour on random pixel
    index = random(0, 
                   gpio_argb->neopixel->numPixels());
    colour = gpio_argb->neopixel->gamma32(random(0, 0xFFFFFF)); 
    gpio_argb->neopixel->setPixelColor(index, colour);
}

void shift_primary(uint8_t &current_primary, 
                  uint8_t final_primary,
                  uint8_t interval) {

    if (current_primary < final_primary) {
        if (final_primary - current_primary >= interval) {
            current_primary += interval;
        }
        else {
            current_primary = final_primary;
        }
    }
    if (current_primary > final_primary) {
        if (current_primary - final_primary >= interval) {
            current_primary -= interval;
        }
        else {
            current_primary = final_primary;
        }
    }
}

void shift_colour(uint32_t &current_colour, 
                  uint32_t final_colour,
                  uint8_t interval) {
    uint8_t curr_red, curr_green, curr_blue;
    uint8_t final_red, final_green, final_blue;

    log_message("shift.. current:0x%08X final:0x%08X", current_colour, final_colour);
    curr_red = current_colour >> 16 & 0xFF;
    curr_green = current_colour >> 8 & 0xFF;
    curr_blue = current_colour & 0xFF;

    final_red = final_colour >> 16 & 0xFF;
    final_green = final_colour >> 8 & 0xFF;
    final_blue = final_colour & 0xFF;

    shift_primary(curr_red, final_red, interval);
    shift_primary(curr_green, final_green, interval);
    shift_primary(curr_blue, final_blue, interval);

    current_colour = 
        (curr_red << 16) & 0xFF0000 |
        (curr_green << 8) & 0x00FF00 | 
        curr_blue;
    log_message("done.. current:0x%08X", current_colour);
}

void reveal(struct gpio_argb *gpio_argb,
            uint8_t left,
            uint8_t right,
            uint8_t fade) {
    uint16_t prog_index, limit, led;
    uint32_t colour;
    uint8_t left_done, right_done;
    static uint32_t current_left = 0;
    static uint32_t current_right = 0;

    if (left && right) {
        limit = gpio_argb->num_leds / 2;
    }
    else {
        limit = gpio_argb->num_leds - 1;
    }

    if (gpio_argb->index > limit) {
        gpio_argb->index = 0;
    }

    if (gpio_argb->index == 0 && gpio_argb->wipe) {
        gpio_argb->neopixel->clear();
    }

    if (left) {
        prog_index = gpio_argb->index % gpio_argb->program_len;
        colour = gpio_argb->neopixel->gamma32(gpio_argb->program[prog_index]); 
        led = gpio_argb->index;

        if (fade == 0) {
            gpio_argb->neopixel->setPixelColor(led, colour);
        }
        else {
            left_done = 0;
            shift_colour(current_left, colour, fade);
            log_message("set.. led:%d colour:0x%08X (0x%08X)", led, current_left, colour);
            gpio_argb->neopixel->setPixelColor(led, current_left);

            if (current_left == colour) {
                left_done = 1;
            }
        }
    }
    else {
        left_done = 1;
    }

    if (right) {
        prog_index = (gpio_argb->program_len - 1) - (gpio_argb->index % gpio_argb->program_len);
        colour = gpio_argb->neopixel->gamma32(gpio_argb->program[prog_index]); 
        led = gpio_argb->num_leds - 1 - gpio_argb->index;

        if (fade == 0) {
            gpio_argb->neopixel->setPixelColor(led, colour);
        }
        else {
            right_done = 0;
            shift_colour(current_right, colour, fade);
            log_message("set.. led:%d colour:0x%08X (0x%08X)", led, current_right, colour);
            gpio_argb->neopixel->setPixelColor(led, current_right);

            if (current_right == colour) {
                right_done = 1;
            }
        }
    }
    else {
        right_done = 1;
    }

    // advance to next
    if (fade) {
        if (left_done && right_done) {
            gpio_argb->index = 
                (gpio_argb->index + gpio_argb->offset) % gpio_argb->num_leds;
            current_left = 0;
            current_right = 0;
        }
    }
    else {
        gpio_argb->index = 
            (gpio_argb->index + gpio_argb->offset) % gpio_argb->num_leds;
    }
}

// Function set_argb_state
// Drives aRGB program by applying the 
// program list of colours to the addressable LED
// strip
void set_argb_state(struct gpio_argb *gpio_argb)
{
    uint32_t now;
    uint16_t limit;
    uint16_t i, prog_index, pixel_index;
    uint32_t colour;

    // invoke early return until delay
    // msec period reached
    now = millis();
    if (gpio_argb->delay && 
        now - gpio_argb->timestamp < 
        gpio_argb->delay) {
        return;
    }

    log_message("set_argb_state(name=%s)",
                gpio_argb->name);

    log_message("index:%d offset:%d mode:%s delay:%d",
                gpio_argb->index,
                gpio_argb->offset,
                gpio_argb->mode,
                gpio_argb->delay);

    // special cases
    if (!strcmp(gpio_argb->mode, "off")) {
        log_message("Mode is off.. returning");
        gpio_argb->timestamp = now;
        return;
    }
    else if (!strcmp(gpio_argb->mode, "rainbow")) {
        rainbow(gpio_argb);
    }
    else if (!strcmp(gpio_argb->mode, "chase_rainbow")) {
        chase_rainbow(gpio_argb);
    }
    else if (!strcmp(gpio_argb->mode, "random")) {
        random_leds(gpio_argb);
    }
    else if (!strcmp(gpio_argb->mode, "reveal")) {
        reveal(gpio_argb, 1, 1, 0);
    }
    else if (!strcmp(gpio_argb->mode, "reveal_left")) {
        reveal(gpio_argb, 1, 0, 0);
    }
    else if (!strcmp(gpio_argb->mode, "reveal_right")) {
        reveal(gpio_argb, 0, 1, 0);
    }
    else if (!strcmp(gpio_argb->mode, "reveal_fade")) {
        reveal(gpio_argb, 1, 1, gpio_argb->fade);
    }
    else if (!strcmp(gpio_argb->mode, "reveal_left_fade")) {
        reveal(gpio_argb, 1, 0, gpio_argb->fade);
    }
    else if (!strcmp(gpio_argb->mode, "reveal_right_fade")) {
        reveal(gpio_argb, 0, 1, gpio_argb->fade);
    }
    else {
        // optional wipe on each draw
        if (gpio_argb->wipe) {
            gpio_argb->neopixel->clear();
        }

        pixel_index = gpio_argb->index;

        // Set limit based on num LEDS (fill)
        // or program
        if (gpio_argb->fill) {
            limit = gpio_argb->num_leds;
        }
        else {
            limit = gpio_argb->program_len;
        }

        for (i = 0;
             i < limit;
             i++) { 


            // read program in reverse always
            prog_index = (gpio_argb->program_len - 1) - (i % gpio_argb->program_len);
             
            colour = gpio_argb->program[prog_index];

            // full int value implies random
            if (colour == 0xFFFFFFFF) {
                colour = random(0, 0xFFFFFF);
            }
            log_message("Setting LED %d to program[%d] -> %08X", 
                        pixel_index,
                        prog_index,
                        colour);

            gpio_argb->neopixel->setPixelColor(pixel_index,
                                               gpio_argb->neopixel->gamma32(colour));

            if (gpio_argb->offset >= 0) {
                // forward modulo
                pixel_index = (pixel_index + 1) % gpio_argb->num_leds;
            }
            else {
                // reverse modulo
                pixel_index = (pixel_index - 1 + gpio_argb->num_leds) % gpio_argb->num_leds;
            }
        }

        // advance index for next colour
        if (gpio_argb->offset >= 0) {
            gpio_argb->index = 
                (gpio_argb->index + gpio_argb->offset) % gpio_argb->num_leds;
        }
        else {
            gpio_argb->index = 
                (gpio_argb->index + gpio_argb->offset + gpio_argb->num_leds) % gpio_argb->num_leds;
        }
        log_message("Next LED is %d", 
                    gpio_argb->index);
    }

    gpio_argb->neopixel->show();

    // Update activity timestamp
    gpio_argb->timestamp = millis();
}


// Function set_argb_program
// Sets the desired aRGB program for the 
// addessable LED strip
void set_argb_program(struct gpio_argb *gpio_argb,
                      JsonObject program)
{
    int i;
    char colour_buffer[50];

    if (!gpio_argb) {
        log_message("No argb specified");
        return;
    }

    if (program.isNull()) {
        log_message("No program present");
        return;
    }

    log_message("set_argb_program(name=%s)",
                gpio_argb->name);

    // resets on neopixel
    // turn all pixels off
    // but only if it's activated
    if (gpio_argb->neopixel) {
        gpio_argb->neopixel->clear();
        gpio_argb->neopixel->show(); 
    }

    gpio_argb->timestamp = 0;

    // wipe any existing program
    gpio_argb->program_len = 0;
    gpio_argb->index = 0;
    gpio_argb->enabled = 0;
    if (gpio_argb->program) {
        free(gpio_argb->program);
        gpio_argb->program = NULL;
    }

    // parse details from JSON program object
    strcpy(gpio_argb->mode, json_get_sval(program["mode"], "off"));
    gpio_argb->wipe = json_get_ival(program["wipe"], 0);
    gpio_argb->fade = json_get_ival(program["fade"], 0);
    gpio_argb->fill = json_get_ival(program["fill"], 0);
    gpio_argb->offset = json_get_ival(program["offset"], 0);
    gpio_argb->delay = json_get_ival(program["delay"], 0);

    if (!strcmp(gpio_argb->mode, "off")) {
        log_message("program mode set to off");
        gpio_argb->enabled = 0;
        return;
    }

    gpio_argb->enabled = 1;

    // register 1msec interval for argb transitions
    // as we have at least 1 active program now
    if (!HTM_LIST_EMPTY(gv_device.argb_list)) {
        TaskMan.add_task("Neopixel LED Transitions",
                         RUN_STATE_WIFI_STA_UP,
                         1,
                         loop_task_transition_argb);
    }

    log_message("Program: mode:%s offset:%d delay:%d",
                gpio_argb->mode,
                gpio_argb->offset,
                gpio_argb->delay);

    JsonArray colours = program["colours"];

    if (program.isNull()) {
        log_message("No colours array present");
        return;
    }
    gpio_argb->program_len = colours.size();
    gpio_argb->program = new uint32_t[gpio_argb->program_len];

    log_message("Program length %d",
                gpio_argb->program_len);

    i = 0;
    for (JsonVariant colour : colours) {
        strcpy(colour_buffer, colour);
        if (!strcmp(colour_buffer, "random")) {
            // Full value implies random
            gpio_argb->program[i] = 0xFFFFFFFF;
        }
        else if (strlen(colour_buffer) > 2 &&
            colour_buffer[0] == '0' &&
            (colour_buffer[1] == 'x' || colour_buffer[1] == 'X')) {
            gpio_argb->program[i] = 
                strtoul(&colour_buffer[2], NULL, 16);
        }
        else {
            gpio_argb->program[i] = 
                strtoul(colour_buffer, NULL, 10);
        }

        log_message("Colour[%d] %s -> 0x%06X",
                    i,
                    colour_buffer,
                    gpio_argb->program[i]);

        i++;
    }
}


// Function loop_task_transition_argb
// Drives aRGB strips from task 
// manager
void loop_task_transition_argb(void)
{
    struct gpio_argb *gpio_argb;

    for (gpio_argb = HTM_LIST_NEXT(gv_device.argb_list);
         gpio_argb != gv_device.argb_list;
         gpio_argb = HTM_LIST_NEXT(gpio_argb)) {
        if (gpio_argb->enabled) {
            set_argb_state(gpio_argb);
        }
    }
}


// Function loop_task_check_active_programs()
// disables the 1msec transition task if no active
// programs are found
// saves on power usage
void loop_task_check_active_argb_programs(void)
{
    struct gpio_argb *gpio_argb;

    for (gpio_argb = HTM_LIST_NEXT(gv_device.argb_list);
         gpio_argb != gv_device.argb_list;
         gpio_argb = HTM_LIST_NEXT(gpio_argb)) {
        if (gpio_argb->enabled) {
            // nothing to do as at least 1 program
            // is active
            return;
        }
    }

    // if we get here there are no active programs
    // So we remove the transition task to 
    // get us more sleep time and same power
    TaskMan.remove_task("Neopixel LED Transitions");
}


// Function: setup_argb
void setup_argb(struct gpio_argb *gpio_argb)
{
    log_message("setup_argb(name:%s LEDs:%d Pin:%d Neopixel Flags:0x%08X)",
                gpio_argb->name,
                gpio_argb->num_leds, 
                gpio_argb->pin, 
                gpio_argb->neopixel_flags);

    if (gpio_argb->pin == NO_PIN) {
        log_message("A-RGB pin disabled.. skipping");
    }
    else {
        gpio_argb->neopixel = 
            new Adafruit_NeoPixel(gpio_argb->num_leds, 
                                  gpio_argb->pin, 
                                  gpio_argb->neopixel_flags);

        // Initialize all pixels to 'off'
        gpio_argb->neopixel->begin();
        gpio_argb->neopixel->show(); 
    }
}


void argb_init(void)
{
    if (!HTM_LIST_EMPTY(gv_device.argb_list)) {
        TaskMan.add_task("RGB Active Program Check",
                         RUN_STATE_WIFI_STA_UP,
                         10000,
                         loop_task_check_active_argb_programs);
    }
}


// Function find_argb
// Finds aRGB device by name
struct gpio_argb* find_argb(const char *name)
{
    struct gpio_argb *gpio_argb;

    for (gpio_argb = HTM_LIST_NEXT(gv_device.argb_list);
         gpio_argb != gv_device.argb_list;
         gpio_argb = HTM_LIST_NEXT(gpio_argb)) {

        if (!strcmp(gpio_argb->name, name)) {
            log_message("found");
            return gpio_argb;
        }
    }

    log_message("not found");
    return NULL;
}
