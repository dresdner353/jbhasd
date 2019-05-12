// JBHASD Types
// Cormac Long October 2017

// Definition for the use of GPIO pins as
// switches where one pin can control a relay, another can
// control a LED and a final pin can be used as a manual
// trigger for the relay.
// All pin selections are optional.
// Values for pins are uint8_t 0-255 and NO_PIN(255) acts
// as the unset value


// Value used to define an unset PIN
// using 255 as we're operating in uint8_t
#define NO_PIN 255

#define MAX_FIELD_LEN 30
#define MAX_CONFIG_LEN 2048

// Context type for tracking how
// a switch was activated
enum switch_state_context {
    SW_ST_CTXT_INIT,    // Boot state 
    SW_ST_CTXT_MANUAL,  // Manually via button
    SW_ST_CTXT_NETWORK, // automatically via NW client
    SW_ST_CTXT_MOTION   // motion pin
};

enum switch_behaviour {
    SW_BHVR_TOGGLE, // Toggle on/off
    SW_BHVR_ON,     // Only turns on
    SW_BHVR_OFF     // Only turns off
};

struct gpio_switch {
    struct gpio_switch *prev, *next;
    char name[MAX_FIELD_LEN];
    uint8_t relay_pin; // output pin used for relay
    uint8_t relay_on_high; // toggles on between HIGH/LOW
    uint8_t led_pin; // output pin used for LED
    uint8_t led_on_high; // toggles on between HIGH/LOW
    uint8_t manual_pin; // input pin used for manual toggle
    uint8_t motion_pin; // input pin used for PIR
    uint8_t current_state;
    uint32_t last_activity;
    uint32_t motion_interval;
    uint32_t manual_interval;
    uint8_t manual_auto_off;
    enum switch_behaviour switch_behaviour; 
    enum switch_state_context state_context;
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
    struct gpio_sensor *prev, *next;
    char name[MAX_FIELD_LEN];
    enum gpio_sensor_type sensor_type;
    uint8_t sensor_variant; // DHT11 DHT22, DHT21 etc
    uint8_t sensor_pin; // pin for sensor
    float temp_offset;
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

#define MAX_PWM_VALUE 1023

struct led_program_step {
    uint32_t colour;
    uint16_t fade_delay; // msec gap between RGB fade
    uint16_t pause; // msec gap at end of RGB fade
};

struct gpio_rgb {
    struct gpio_rgb *prev, *next;
    char name[MAX_FIELD_LEN];
    uint8_t red_pin;   // Red pin
    uint8_t green_pin; // Green pin
    uint8_t blue_pin;  // Blue pin
    uint8_t manual_pin; // Random Program
    char program[2048]; // default program
    char *program_ptr;

    // Hue + RGB values
    // for current colour
    uint32_t current_colour;

    // arrays of desired and current states
    // for pins
    // these are PWM values not RGB
    uint16_t desired_states[3];
    uint16_t current_states[3];

    // current step
    // Tracks logical step in program
    // from 0 upward
    uint8_t step;

    // Determined if we discover we encounter an end
    // of program while extracting first step
    // used to drive a bypass of the set_rgb_state()
    // call
    uint8_t single_step;

    // msec timestamp for tracking fade 
    unsigned long timestamp;

    // delay and pauses
    // in msecs
    uint16_t fade_delay;
    uint16_t pause;
};

// Addressable RGB (Neopixel)
struct gpio_argb {
    struct gpio_argb *prev, *next;
    char name[MAX_FIELD_LEN];
    uint8_t pin; // Data pin
    uint8_t manual_pin; // Random Program
    uint16_t num_leds;
    uint32_t neopixel_flags;
    char program[2048]; // default program
    char *program_start;

    uint32_t timestamp;
    uint16_t index;

    int8_t direction;
    uint16_t pause;
    uint8_t fill_mode;

    Adafruit_NeoPixel *neopixel;
};


// Software Version
// Crude compile-time grab of date and time
// into string
const char *gv_sw_compile_date = "JBHASD-VERSION " __DATE__ " " __TIME__;

struct device_profile {
    char zone[MAX_FIELD_LEN];
    char wifi_ssid[MAX_FIELD_LEN];
    char wifi_password[MAX_FIELD_LEN];
    uint8_t ota_enabled;
    uint8_t telnet_enabled;
    uint8_t mdns_enabled;
    uint8_t manual_switches_enabled;
    uint8_t boot_program_pin;
    uint8_t wifi_led_pin;
    uint8_t wifi_led_on_high;
    uint8_t force_apmode_onboot;
    uint8_t configured;
    struct gpio_switch *switch_list;
    struct gpio_sensor *sensor_list;
    struct gpio_rgb *rgb_list;
    struct gpio_argb *argb_list;
};

struct device_profile gv_device;

#define RUN_STATE_INIT           HTM_RUN_STATE_00
#define RUN_STATE_WIFI_AP        HTM_RUN_STATE_01
#define RUN_STATE_WIFI_STA_DOWN  HTM_RUN_STATE_02
#define RUN_STATE_WIFI_STA_UP    HTM_RUN_STATE_03
#define RUN_STATE_WIFI_OTA       HTM_RUN_STATE_04
#define RUN_STATE_ALL            HTM_RUN_STATE_ALL

uint8_t gv_reboot_requested = 0;

#define LOGBUF_MAX 2048

enum gv_logging_enum {
    LOGGING_NONE,
    LOGGING_SERIAL,     // Log to Serial
    LOGGING_NW_CLIENT,  // Log to connected network client
};

enum gv_logging_enum gv_logging = LOGGING_NONE;

// Telnet server
#define MAX_TELNET_CLIENTS 3

// Web
#define WEB_PORT 80
