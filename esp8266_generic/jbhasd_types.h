// JBHASD Types
// Cormac Long June 2019

// Common Arduino Libraries
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <DHT.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>


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
    uint8_t random; // randomise indicator
};

struct gpio_rgb {
    uint8_t enabled;   // enabled
    struct gpio_rgb *prev, *next;
    char name[MAX_FIELD_LEN];
    uint8_t red_pin;   // Red pin
    uint8_t green_pin; // Green pin
    uint8_t blue_pin;  // Blue pin
    struct led_program_step *program; // default program
    uint16_t program_len;
    int index;
    uint32_t init_interval;

    // arrays of desired and current states
    // for pins
    // these are PWM values not RGB
    // also tracking of fade 
    uint16_t desired_states[3];
    uint16_t current_states[3];
    uint8_t fade_in_progress; 

    // Determined if we discover we encounter an end
    // of program while extracting first step
    // used to drive a bypass of the set_rgb_state()
    // call
    uint8_t single_step;

    // msec timestamp for tracking fade 
    unsigned long timestamp;
};

// Addressable RGB (Neopixel)
struct gpio_argb {
    struct gpio_argb *prev, *next;
    char name[MAX_FIELD_LEN];
    uint8_t pin; // Data pin
    uint16_t num_leds;
    uint32_t neopixel_flags;

    uint32_t timestamp;
    uint16_t index;
    uint16_t temp_index;
    uint16_t draw_count;

    // program details
    char mode[MAX_FIELD_LEN];
    uint8_t enabled;
    uint8_t wipe;
    uint8_t fill;
    uint8_t brightness;
    int16_t offset;
    uint16_t delay;
    uint16_t toggle;
    uint32_t *program;
    uint16_t program_len;

    Adafruit_NeoPixel *neopixel;
};


struct device_profile {
    char hostname[MAX_FIELD_LEN + MAX_FIELD_LEN];
    char zone[MAX_FIELD_LEN];
    char wifi_ssid[MAX_FIELD_LEN];
    char wifi_password[MAX_FIELD_LEN];
    uint8_t ota_enabled;
    uint8_t telnet_enabled;
    uint8_t mdns_enabled;
    uint8_t manual_switches_enabled;
    uint8_t boot_pin;
    uint16_t boot_wait;
    uint8_t status_led_pin;
    uint8_t status_led_on_high;
    uint8_t force_apmode_onboot;
    uint32_t idle_period_wifi;
    uint32_t idle_period_reboot;
    uint8_t configured;
    struct gpio_switch *switch_list;
    struct gpio_sensor *sensor_list;
    struct gpio_rgb *rgb_list;
    struct gpio_argb *argb_list;
};


#define RUN_STATE_INIT           HTM_RUN_STATE_00
#define RUN_STATE_WIFI_AP        HTM_RUN_STATE_01
#define RUN_STATE_WIFI_STA_DOWN  HTM_RUN_STATE_02
#define RUN_STATE_WIFI_STA_UP    HTM_RUN_STATE_03
#define RUN_STATE_WIFI_OTA       HTM_RUN_STATE_04
#define RUN_STATE_ALL            HTM_RUN_STATE_ALL

#define LOGBUF_MAX 2048

enum gv_logging_enum {
    LOGGING_NONE,
    LOGGING_SERIAL,     // Log to Serial
    LOGGING_NW_CLIENT,  // Log to connected network client
};


// Telnet server
#define MAX_TELNET_CLIENTS 3

// Web
#define WEB_PORT 80

// Buffer declarations

// Global variables
extern char gv_config[MAX_CONFIG_LEN];
extern HandyTaskMan TaskMan;
extern char gv_mdns_hostname[MAX_FIELD_LEN + MAX_FIELD_LEN];
extern const char *gv_sw_compile_date;
extern struct device_profile gv_device;
extern enum gv_logging_enum gv_logging;
extern uint8_t gv_reboot_requested;

// Logging
char *millis_str(uint32_t msecs);
void vlog_message(char *format, va_list args);
void log_message(char *format, ... );
void loop_task_telnet(void);
void start_telnet(void);


// Sensor
struct gpio_sensor* gpio_sensor_alloc(void);
void setup_sensor(gpio_sensor *gpio_sensor);
void read_sensors(void);


// Switch
struct gpio_switch* gpio_switch_alloc(void);
const char *get_sw_context(enum switch_state_context context);
const char *get_sw_behaviour(enum switch_behaviour behaviour);
void restore_status_led_state(void);
void toggle_status_led(uint16_t delay_msecs);
void set_switch_state(struct gpio_switch *gpio_switch,
                      uint8_t state,
                      enum switch_state_context context);
void set_switch_motion_interval(struct gpio_switch *gpio_switch,
                                uint32_t interval);
void set_switch_manual_interval(struct gpio_switch *gpio_switch,
                                uint32_t interval);
void set_switch_manual_auto_off(struct gpio_switch *gpio_switch,
                                uint8_t auto_off);
void setup_switch(struct gpio_switch* gpio_switch);
void switch_init(void);
void loop_task_check_switches(void);
void loop_task_check_boot_switch(void);
struct gpio_switch* find_switch(const char *name);


// RGB
struct gpio_rgb* gpio_rgb_alloc(void);
void loop_task_transition_rgb(void);
void set_rgb_program(struct gpio_rgb *gpio_rgb,
                     JsonObject program);
void set_rgb_state(struct gpio_rgb *gpio_rgb);
void setup_rgb(struct gpio_rgb* gpio_rgb);
void rgb_init(void);
struct gpio_rgb* find_rgb(const char *name);


// ARGB
struct gpio_argb* gpio_argb_alloc(void);
void set_argb_state(struct gpio_argb *gpio_argb);
void set_argb_program(struct gpio_argb *gpio_argb,
                     JsonObject program);
void loop_task_transition_argb(void);
void setup_argb(struct gpio_argb* gpio_argb);
void argb_init(void);
struct gpio_argb* find_argb(const char *name);

// Config
int json_get_ival(JsonVariant variant,
                  int def_ival);
const char *json_get_sval(JsonVariant variant,
                          const char *def_sval);
void save_config(void);
void update_config(char *field, 
                   const char *sval,
                   int32_t ival,
                   uint8_t save_now);
void reset_config(void);
void load_config(void);

// OTA
void start_ota(void);
void loop_task_ota(void);

// Network
void start_wifi_ap_mode(void);
void start_wifi_sta_mode(void);
void start_mdns(void);
void start_sta_mode_services(void);
void loop_task_webserver(void);
void loop_task_dns(void);
void loop_task_mdns(void);
void loop_task_check_wifi_down(void);
void loop_task_check_wifi_up(void);
void loop_task_status_led(void);
void loop_task_ap_reboot(void);
void loop_task_check_idle_status(void);



// main ino file
uint8_t pin_in_use(uint8_t pin);
