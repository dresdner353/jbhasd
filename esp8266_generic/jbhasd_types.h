// JBHASD Types
// Cormac Long October 2017

// Definition for the use of GPIO pins as
// switches where one pin can control a relay, another can
// control a LED and a final pin can be used as a manual
// trigger for the relay.
// All pin selections are optional.
// Values for pins are unsigned char 0-255 and NO_PIN(255) acts
// as the unset value

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
    char *name;
    unsigned char relay_pin; // output pin used for relay
    unsigned char led_pin; // output pin used for LED
    unsigned char manual_pin; // input pin used for manual toggle
    unsigned char initial_state;
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
    char *name;
    enum gpio_sensor_type sensor_type;
    unsigned char sensor_variant; // DHT11 DHT22, DHT21 etc
    unsigned char sensor_pin; // pin for sensor
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

# define MAX_PWM_VALUE 1023

#define MAX_LED_STEPS 20

struct led_program_step {
    unsigned int colour;
    int fade_delay; // msec gap between RGB fade
    int pause; // msec gap at end of RGB fade
};

struct gpio_led {
    char *name;
    unsigned char red_pin;   // Red pin
    unsigned char green_pin; // Green pin
    unsigned char blue_pin;  // Blue pin
    unsigned char manual_pin;  // manual toggle
    char *init_program; // default program

    // Hue + RGB values
    // for current colour
    unsigned int current_state;

    // array of program steps that is iterated
    // to produce light shift sequences
    // step_index is the current active 
    // step
    struct led_program_step steps[MAX_LED_STEPS];
    int num_steps;
    int step_index;

    // arrays of desired and current states
    // for pins
    // these are PWM values not RGB
    unsigned short desired_states[3];
    unsigned short current_states[3];

    // msec timestamp for tracking fade 
    // delay and pauses
    unsigned long timestamp;
};

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

#define MAX_FIELD_LEN 20
#define MAX_SWITCHES 5
#define MAX_SENSORS 5
#define MAX_LEDS 5
#define MAX_PROGRAM_LEN 100
#define CFG_MARKER_VAL 0x16

// Software Version
// Crude compile-time grab of date and time
// into string
const char *gw_sw_compile_date = __DATE__ " " __TIME__;

// Pointer to selected profile
// This will set at load_config() stage
struct device_gpio_profile *gv_profile = NULL;

// config struct that we cast into EEPROM
struct eeprom_config {
    unsigned char marker;
    char profile[MAX_FIELD_LEN];
    char zone[MAX_FIELD_LEN];
    char wifi_ssid[MAX_FIELD_LEN];
    char wifi_password[MAX_FIELD_LEN];
    char switch_names[MAX_SWITCHES][MAX_FIELD_LEN];
    unsigned char switch_initial_states[MAX_SWITCHES];
    unsigned char switch_behaviours[MAX_SWITCHES];
    char sensor_names[MAX_SENSORS][MAX_FIELD_LEN];
    char temp_offset[MAX_FIELD_LEN];
    char led_names[MAX_LEDS][MAX_FIELD_LEN];
    char led_programs[MAX_LEDS][MAX_PROGRAM_LEN];
    unsigned char ota_enabled;
    unsigned char telnet_enabled;
    unsigned char manual_switches_enabled;
    unsigned char force_apmode_onboot;
} gv_config;

// Runtime mode
// using bitmasks so new modes need to 
// take unused bits. It's just a 1,2,4,8 sequence 
// in hex
#define MODE_INIT          0x01  // Boot mode
#define MODE_WIFI_AP       0x02  // WiFI AP Mode (for config)
#define MODE_WIFI_STA_DOWN 0x04  // WiFI station/client down
#define MODE_WIFI_STA_UP   0x08  // WiFI station/client up (normal operation)
#define MODE_WIFI_OTA      0x10  // OTA mode invoked from STA mode
#define MODE_ALL           0xFF  // All states 

int gv_mode = MODE_INIT;
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

// Main loop ticker timing
struct loop_task {
    char *name;
    int mode_mask;
    unsigned long millis_delay;
    void (*fp)(void);
    unsigned long last_call;
    unsigned long num_calls;
    unsigned long cpu_time;
};
