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

#define LIST_SELFLINK(head) { \
    (head)->prev = (head); \
    (head)->next = (head); \
}

#define LIST_INSERT(head, item) { \
    (item)->next = (head); \
    (item)->prev = (head)->prev; \
    (item)->prev->next = item; \
    (head)->prev = (item); \
}

#define LIST_RMOVE(item) { \
    (item)->prev->next = (item)->next; \
    (item)->next->prev = (item)->prev; \
    (item)->next = NULL; \
    (item)->prev = NULL; \
}

#define LIST_NEXT(item) { \
    (item)->next \
}

#define LIST_PREV(item) { \
    (item)->prev \
}

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

# define MAX_PWM_VALUE 1023

#define MAX_LED_STEPS 20

struct led_program_step {
    unsigned int colour;
    int fade_delay; // msec gap between RGB fade
    int pause; // msec gap at end of RGB fade
};

struct gpio_led {
    struct gpio_led *prev, *next;
    char name[MAX_FIELD_LEN];
    unsigned char red_pin;   // Red pin
    unsigned char green_pin; // Green pin
    unsigned char blue_pin;  // Blue pin
    unsigned char manual_pin;  // manual toggle
    char program[2048]; // default program

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



// Software Version
// Crude compile-time grab of date and time
// into string
const char *gv_sw_compile_date = __DATE__ " " __TIME__;

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
    struct gpio_switch *switch_list;
    struct gpio_sensor *sensor_list;
    struct gpio_led *led_list;
};

struct device_profile gv_device;

/*
char *gv_config_json_str = 
                     "{"
                     "\"zone\" : \"proto\", "
                     "\"wifi_ssid\" : \"cormac-L\", "
                     "\"wifi_password\" : \"h0tcak3y\", "
                     "\"ota_enabled\" : 1, "
                     "\"telnet_enabled\" : 1, "
                     "\"mdns_enabled\" : 1, "
                     "\"manual_switches_enabled\" : 1, "
                     "\"switches\" : 1, "
                     "\"boot_pin\" : 0, "
                     "\"wifi_led_pin\" : 13, "
                     "\"force_apmode_onboot\" : 0, "
                     "\"controls\" : ["
                     "{ \"name\" : \"relay\","
                     "  \"type\" : \"switch\", "
                     "  \"sw_mode\" : \"toggle\", "
                     "  \"sw_state\" : 1, "
                     "  \"sw_context\" : \"init\", "
                     "  \"sw_relay_pin\" : 12, "
                     "  \"sw_led_pin\" : 13, "
                     "  \"sw_man_pin\" : 0 "
                     "},"
                     "{ \"name\" : \"fake1\","
                     "  \"type\" : \"switch\", "
                     "  \"sw_mode\" : \"toggle\", "
                     "  \"sw_state\" : 0, "
                     "  \"sw_context\" : \"init\", "
                     "  \"sw_relay_pin\" : 255, "
                     "  \"sw_led_pin\" : 255, "
                     "  \"sw_man_pin\" : 255 "
                     "},"
                     "{ \"name\" : \"fake2\","
                     "  \"type\" : \"switch\", "
                     "  \"sw_mode\" : \"toggle\", "
                     "  \"sw_state\" : 0, "
                     "  \"sw_context\" : \"init\", "
                     "  \"sw_relay_pin\" : 255, "
                     "  \"sw_led_pin\" : 255, "
                     "  \"sw_man_pin\" : 255 "
                     "},"
                     "{ \"name\" : \"FakeTemp1\","
                     "  \"type\" : \"temp/humidity\", "
                     "  \"th_variant\" : \"DHT21\", "
                     "  \"th_temp\" : 5.02, "
                     "  \"th_humidity\" : 28.6, "
                     "  \"th_temp_offset\" : -2.4, "
                     "  \"th_pin\" : 255 "
                     "},"
                     "{ \"name\" : \"FakeTemp2\","
                     "  \"type\" : \"temp/humidity\", "
                     "  \"th_variant\" : \"DHT21\", "
                     "  \"th_temp\" : 5.02, "
                     "  \"th_humidity\" : 28.6, "
                     "  \"th_temp_offset\" : -2.4, "
                     "  \"th_pin\" : 255 "
                     "},"
                     "{ \"name\" : \"Temp\","
                     "  \"type\" : \"temp/humidity\", "
                     "  \"th_variant\" : \"DHT21\", "
                     "  \"th_temp\" : 5.02, "
                     "  \"th_humidity\" : 28.6, "
                     "  \"th_temp_offset\" : -2.4, "
                     "  \"th_pin\" : 2 "
                     "}"
                     "]"
                     "}";
                     */

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
