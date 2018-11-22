// JBHASD Types
// Cormac Long October 2017

// Definition for the use of GPIO pins as
// switches where one pin can control a relay, another can
// control a LED and a final pin can be used as a manual
// trigger for the relay.
// All pin selections are optional.
// Values for pins are unsigned char 0-255 and NO_PIN(255) acts
// as the unset value


// Value used to define an unset PIN
// using 255 as we're operating in unsigned char
#define NO_PIN 255

// EEPROM Configuration
// It uses a struct of fields which is cast directly against
// the eeprom array. The marker field is used to store a dummy
// value as a means of detecting legit config on the first boot.
// That marker value can be changed as config is remodelled
// to ensure a first read is interpreted as blank/invalid and
// config is then reset

#define MAX_FIELD_LEN 30
#define MAX_CONFIG_LEN 2048



// Context type for tracking how
// a switch was activated
enum switch_state_context {
    SW_ST_CTXT_INIT,    // Boot state 
    SW_ST_CTXT_MANUAL,  // Manually via button
    SW_ST_CTXT_NETWORK  // automatically via NW client
};

enum switch_behaviour {
    SW_BHVR_TOGGLE, // Toggle on/off
    SW_BHVR_ON,     // Only turns on
    SW_BHVR_OFF     // Only turns off
};

struct gpio_switch {
    struct gpio_switch *prev, *next;
    char name[MAX_FIELD_LEN];
    unsigned char relay_pin; // output pin used for relay
    unsigned char led_pin; // output pin used for LED
    unsigned char manual_pin; // input pin used for manual toggle
    unsigned char current_state;
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
    unsigned char sensor_variant; // DHT11 DHT22, DHT21 etc
    unsigned char sensor_pin; // pin for sensor
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
    unsigned int colour;
    int fade_delay; // msec gap between RGB fade
    int pause; // msec gap at end of RGB fade
};

struct gpio_rgb {
    struct gpio_rgb *prev, *next;
    char name[MAX_FIELD_LEN];
    unsigned char red_pin;   // Red pin
    unsigned char green_pin; // Green pin
    unsigned char blue_pin;  // Blue pin
    unsigned char manual_pin; // Random Program
    char program[2048]; // default program
    char *program_ptr;

    // Hue + RGB values
    // for current colour
    unsigned int current_colour;

    // arrays of desired and current states
    // for pins
    // these are PWM values not RGB
    unsigned short desired_states[3];
    unsigned short current_states[3];

    // current step
    // Tracks logical step in program
    // from 0 upward
    int step;

    // Determined if we discover we encounter an end
    // of program while extracting first step
    // used to drive a bypass of the set_rgb_state()
    // call
    int single_step;

    // msec timestamp for tracking fade 
    unsigned long timestamp;

    // delay and pauses
    // in msecs
    int fade_delay;
    int pause;
};

// Addressable RGB (Neopixel)
struct gpio_argb {
    struct gpio_argb *prev, *next;
    char name[MAX_FIELD_LEN];
    unsigned char pin; // Data pin
    unsigned char manual_pin; // Random Program
    int num_leds;
    int neopixel_flags;
    char program[2048]; // default program
    char *program_start;

    int timestamp;
    unsigned int index;

    int direction;
    int pause;
    int fill_mode;

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
    int ota_enabled;
    int telnet_enabled;
    int mdns_enabled;
    int manual_switches_enabled;
    int boot_program_pin;
    int wifi_led_pin;
    int force_apmode_onboot;
    int configured;
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

int gv_reboot_requested = 0;

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
