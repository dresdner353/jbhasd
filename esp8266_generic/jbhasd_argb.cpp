#include "HandyTaskMan.h"
#include "jbhasd_types.h"

struct gpio_argb* gpio_argb_alloc(void)
{
    struct gpio_argb *gpio_argb;

    gpio_argb = (struct gpio_argb*) malloc(sizeof(struct gpio_argb));

    return gpio_argb;
}


// Function set_argb_state
// Drives aRGB program by applying the 
// program list of colours to the addressable LED
// strip
void set_argb_state(struct gpio_argb *gpio_argb)
{
    uint32_t now;
    uint32_t colour;
    uint8_t red, green, blue;
    char colour_buf[12];
    char *p, *q;
    uint16_t led_count;
    uint32_t i;
    uint32_t start_index;
    uint8_t loop;
    uint8_t wipe = 0;

    if (gpio_argb->program_start == NULL) {
        log_message("program is empty.. nothing to do");
        return;
    }

    // invoke early return until delay
    // msec period reached
    now = millis();
    if (now - gpio_argb->timestamp < 
        gpio_argb->pause) {
        return;
    }

    log_message("set_argb_state(name=%s)",
                gpio_argb->name);

    start_index = gpio_argb->index;

    log_message("index:%d direction:%d fill_mode:%d pause:%d",
                start_index,
                gpio_argb->direction,
                gpio_argb->fill_mode,
                gpio_argb->pause);

    // Wipe strip before draw
    switch(gpio_argb->fill_mode) {
      case 0:
        wipe = 1;
        break;

      case 2:
        // For append mode, we wipe only at the start of 
        // the program
        if (start_index == 0) {
            wipe = 1;
        }
        break;

      default:
        // no wipe
        wipe = 0;
        break;
    }

    if (wipe) {
        for (i = 0; 
             i < gpio_argb->num_leds; 
             i++) {
            gpio_argb->neopixel->setPixelColor(i,
                                               gpio_argb->neopixel->Color(0,0,0));
        }
    }

    // enter permanent loop now to populate
    // the neopixel array from the program data
    // loop will exit depending on fill mode
    p = gpio_argb->program_start;
    led_count = 0;
    loop = 1;
    while (loop) {
        // find colour separator
        q = strchr(p, ',');
        if (q &&
            q - p < sizeof(colour_buf)) {

            // isolate colour 
            strncpy(colour_buf, 
                    p,
                    q - p);
            colour_buf[q - p] = '\0';

            // move to next in sequence
            p = q + 1;
        }
        else {
            // last colour in program
            strncpy(colour_buf, 
                    p,
                    sizeof(colour_buf) - 1);
            colour_buf[sizeof(colour_buf) - 1] = '\0';

            // Program reset scenarios
            switch(gpio_argb->fill_mode) {
              case 1:
                // reset the program for repeat
                // write 
                p = gpio_argb->program_start;
                break;

              default:
                // once
                p = NULL;
                break;
            }
        }

        log_message("Read colour from program: %s", 
                    colour_buf);

        // convert given colour to int
        if (strlen(colour_buf) > 2 &&
            colour_buf[0] == '0' &&
            (colour_buf[1] == 'x' || colour_buf[1] == 'X')) {
            // hex decode
            colour = strtoul(&colour_buf[2], NULL, 16);
        }
        else {
            // decimal unsigned int
            colour = strtoul(colour_buf, NULL, 10);
        }

        // split to RGB
        red = colour >> 16 & 0xFF;
        green = colour >> 8 & 0xFF;
        blue = colour & 0xFF;

        // Set current pixel index to colour
        // We use Neopixel Color method as this will 
        // apply the RGB, BGR, GBR variation accordingly
        log_message("Setting LED %d to Red:0x%02X Green:0x%02X Blue:0x%02X", 
                    gpio_argb->index,
                    red,
                    green,
                    blue);
        gpio_argb->neopixel->setPixelColor(gpio_argb->index, 
                                           gpio_argb->neopixel->Color(red, 
                                                                      green,
                                                                      blue));
        led_count++;

        // advance index for next colour
        gpio_argb->index = (gpio_argb->index + 1) % gpio_argb->num_leds;
        log_message("Next LED is %d", 
                    gpio_argb->index);

        log_message("LED count is %d", led_count);

        // Loop exit scenarios
        switch(gpio_argb->fill_mode) {
          case 0:
          case 2:
            if (p == NULL) {
                loop = 0;
            }
            break;

          case 1:
            if (led_count >= gpio_argb->num_leds) {
                loop = 0;
            }
            break;

          default:
            // break loop for safety
            loop = 0;
            break;
        }
    }

    gpio_argb->neopixel->show();

    // reset index for next call
    switch(gpio_argb->fill_mode) {
      default:
      case 0:
      case 1:
        // Standard single pixel movement according 
        // to set direction
        // or standstill
        if (gpio_argb->direction > 0) {
            gpio_argb->index = (start_index + 1) % gpio_argb->num_leds;
        }
        if (gpio_argb->direction < 0) {
            gpio_argb->index = (start_index - 1) % gpio_argb->num_leds;
        }
        if (gpio_argb->direction == 0) {
            gpio_argb->index = start_index;
        }
        break;

      case 2:
        // append draw
        // for forward motion, we leave it as is
        // for backward, we need to offset from start_index
        // so we determine the unsigned difference between where
        // we started and are now and then further subtract
        // this from the current position
        if (gpio_argb->direction < 0) {
            gpio_argb->index = (start_index - led_count) % gpio_argb->num_leds;
        }
        break;

    }

    log_message("Done.. LEDs written:%d index:%d",
                led_count,
                gpio_argb->index);

    // Update activity timestamp
    gpio_argb->timestamp = millis();
}


// Function set_argb_program
// Sets the desired aRGB program for the 
// addessable LED strip
void set_argb_program(struct gpio_argb *gpio_argb,
                     const char *program)
{
    char *p;
    char *direction_p = NULL;
    char *pause_p = NULL;
    char *fill_p = NULL; 
    char field_buffer[50];
    uint16_t i;

    if (!gpio_argb) {
        log_message("No argb specified");
        return;
    }

    log_message("set_argb_program(name=%s, program=%s)",
                gpio_argb->name,
                program);

    // resets on neopixel
    // turn all pixels off
    for (i = 0; 
         i < gpio_argb->num_leds; 
         i++) {
        gpio_argb->neopixel->setPixelColor(i,
                                           gpio_argb->neopixel->Color(0,0,0));
    }
    gpio_argb->neopixel->show(); 

    gpio_argb->timestamp = 0;

    // copy in program string
    // if its not a pointer to itself
    if (gpio_argb->program != program) {
        strcpy(gpio_argb->program, program);
    }
    gpio_argb->index = 0;
    gpio_argb->program_start = NULL;

    // parse program
    // <direction>;<pause>;<fill mode>;RRGGBB,RRGGBB,.....

    // copy over first 50 octets of text
    // should be enough to contain main front 
    // fields
    strncpy(field_buffer, 
            gpio_argb->program, 
            sizeof(field_buffer) - 1);
    field_buffer[sizeof(field_buffer) - 1] = '\0';

    // Set pointers and NULL separators
    // for key lead fields for direction, pause and fill mode
    direction_p = field_buffer;
    p = strchr(direction_p, ';');
    if (p) {
        *p = '\0';
        p++;
        pause_p = p;
        p = strchr(pause_p, ';');
        if (p) {
            *p = '\0';
            p++;
            fill_p = p;
            p = strchr(fill_p, ';');
            if (p) {
                *p = '\0';
                p++;

                // program start in main string will be same offset as 
                // p is now having traversed pass all ';' delimiters
                gpio_argb->program_start = gpio_argb->program + 
                    (p - field_buffer);
            }
            else {
                log_message("Failed to find program separator");
            }
        }
        else {
            log_message("Failed to find fill mode separator");
        }
    }
    else {
        log_message("Failed to find pause separator");
    }

    if (direction_p && 
        pause_p && 
        fill_p && 
        gpio_argb->program_start) {
        gpio_argb->direction = atoi(direction_p);
        gpio_argb->pause = atoi(pause_p);
        gpio_argb->fill_mode = atoi(fill_p);

    }
    else {
        log_message("Failed to parse program");
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
        if (gpio_argb->program_start != NULL) {
            set_argb_state(gpio_argb);
        }
    }
}


// Function: setup_argbs
void setup_argbs(void)
{
    struct gpio_argb *gpio_argb;
    uint8_t argb_count = 0;

    log_message("setup_argbs()");

    for (gpio_argb = HTM_LIST_NEXT(gv_device.argb_list);
         gpio_argb != gv_device.argb_list;
         gpio_argb = HTM_LIST_NEXT(gpio_argb)) {
            log_message("Setting up A-RGB:%s LEDs:%d Pin:%d Neopixel Flags:0x%08X",
                        gpio_argb->name,
                        gpio_argb->num_leds, 
                        gpio_argb->pin, 
                        gpio_argb->neopixel_flags);

            if (gpio_argb->pin == NO_PIN) {
                log_message("A-RGB pin disabled.. skipping");
            }
            else {
                argb_count++;

                gpio_argb->neopixel = 
                    new Adafruit_NeoPixel(gpio_argb->num_leds, 
                                          gpio_argb->pin, 
                                          gpio_argb->neopixel_flags);

                // Initialize all pixels to 'off'
                gpio_argb->neopixel->begin();
                gpio_argb->neopixel->show(); 

                set_argb_program(gpio_argb,
                                 gpio_argb->program);

                if (gpio_argb->manual_pin != NO_PIN) {
                    log_message("    Manual pin:%d",
                                gpio_argb->manual_pin);
                    pinMode(gpio_argb->manual_pin, INPUT_PULLUP);
                }
            }
    }

    if (argb_count > 0) {
        TaskMan.add_task("Neopixel LED Transitions",
                         RUN_STATE_WIFI_STA_UP,
                         1,
                         loop_task_transition_argb);
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
