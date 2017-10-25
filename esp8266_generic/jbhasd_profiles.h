// JBHASD Profiles
// Cormac Long October 2017

// ESP-01
// Using Tx/Rx pins for LED switch functions
// no defined relays
// Pin 2 assigned DHT21 temp/humidity sensor plus
// a fake sensor
struct gpio_switch gv_switch_register_esp01[] = {
    {
        "Tx",           // Name
        NO_PIN,         // Relay Pin
        1,              // LED Pin
        0,              // Manual Pin
        0,              // Init State
        0,              // Current state
        SW_BHVR_TOGGLE, // Toggle on/off
        SW_ST_CTXT_INIT // Switch context Init
    },        
    {
        "Rx",           // Name
        NO_PIN,         // Relay Pin
        3,              // LED Pin
        0,              // Manual Pin
        0,              // Init State
        0,              // Current state
        SW_BHVR_TOGGLE, // Toggle on/off
        SW_ST_CTXT_INIT // Switch context Init
    },        
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        0,
        0,
        SW_BHVR_TOGGLE, 
        SW_ST_CTXT_INIT 
    }
};

struct gpio_sensor gv_sensor_register_esp01[] = {
    {
        "Temp",           // Name
        GP_SENS_TYPE_DHT, // Sensor Type
        DHT21,            // Sensor Variant
        2,                // Pin
        NULL,             // void ref
        0,                // f1
        0                 // f2
    },
    {
        // Fake DHT with no pin
        "Fake",            // Name
        GP_SENS_TYPE_DHT,  // Sensor Type
        0,                 // Sensor Variant
        NO_PIN,            // Pin
        NULL,              // void ref
        0,                 // f1
        0                  // f2
    },
    {
        // terminator.. never delete
        NULL,
        GP_SENS_TYPE_NONE,
        0,
        0,
        NULL,
        0,
        0
    }
};

// Sonoff Basic
// First switch assigned to onboard relay(12) and LED(13)
// Three dummy switches defined then for testing
// Pin 14 assigned to DHT21 sensor plus a fake sensor
struct gpio_switch gv_switch_register_sonoff_basic[] = {
    {
        "A",            // Name
        12,             // Relay Pin
        13,             // LED Pin
        0,              // Manual Pin
        1,              // Init State
        0,              // Current State
        SW_BHVR_TOGGLE, // Toggle on/off
        SW_ST_CTXT_INIT // Switch context Init
    },
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        0,
        0,
        SW_BHVR_TOGGLE,
        SW_ST_CTXT_INIT 
    }
};

struct gpio_sensor gv_sensor_register_sonoff_basic[] = {
    {
        "Temp",            // Name
        GP_SENS_TYPE_DHT,  // Sensor Type
        DHT21,             // Sensor Variant
        14,                // Pin
        NULL,              // void ref
        0,                 // f1
        0                  // f2
    },
    {
        "Fake",             // Name
        GP_SENS_TYPE_DHT,   // Sensor Type
        0,                  // Sensor Variant
        NO_PIN,             // Pin
        NULL,               // void ref
        0,                  // f1
        0                   // f2
    }, // Fake DHT with no pin
    {
        // terminator.. never delete
        NULL,
        GP_SENS_TYPE_NONE,
        0,
        0,
        NULL,
        0,
        0
    }
};

// Sonoff S20 mains socket
// main switch assigned to replay on PIN 12
// No LED pin assigned for switch as there is a built-in blue LED
// So PIN 13 is defined for its own switch as a LED indicator with no
// assigned relay
// 1 fake sensor defined
struct gpio_switch gv_switch_register_sonoff_s20[] = {
    {
        // S20 relay+Blue LED GPIO 12
        "Socket",       // Name
        12,             // Relay Pin
        NO_PIN,         // LED Pin
        0,              // Manual Pin
        1,              // Init State
        0,              // Current State
        SW_BHVR_TOGGLE, // Toggle on/off
        SW_ST_CTXT_INIT // Switch context Init
    },
    {
        // S20 Green LED GPIO 13
        "Green LED",    // Name
        NO_PIN,         // Relay Pin
        13,             // LED Pin
        NO_PIN,         // Manual Pin
        1,              // Init State
        0,              // Current State
        SW_BHVR_TOGGLE, // Toggle on/off
        SW_ST_CTXT_INIT // Switch context Init
    },
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        0,
        0,
        SW_BHVR_TOGGLE,
        SW_ST_CTXT_INIT 
    }
};

struct gpio_sensor gv_sensor_register_sonoff_s20[] = {
    {
        // DHT21 on Tx Pin
        "Temp",             // Name
        GP_SENS_TYPE_DHT,   // Sensor Type
        DHT21,              // Sensor Variant
        1,                  // Pin
        NULL,               // void ref
        0,                  // f1
        0                   // f2
    },
    {
        // terminator.. never delete
        NULL,
        GP_SENS_TYPE_NONE,
        0,
        0,
        NULL,
        0,
        0
    }
};

// H801 LED Wifi Controller
// manual switch set to reset pin 0
// Onboard Red LED assigned to pin 5
// Onboard Green LED assigned to pin 1 and used as WiFI LED
// No sensors for this profile
// RGB PWM Pins Red:15, Green:13, Blue:12
// White1:14, White2:4

struct gpio_switch gv_switch_register_h801[] = {
    {
        "Red LED",      // Name
        NO_PIN,         // Relay Pin
        5,              // LED Pin
        NO_PIN,         // Manual Pin
        0,              // Init State
        0,              // Current State
        SW_BHVR_TOGGLE, // Toggle on/off
        SW_ST_CTXT_INIT // Switch context Init
    },
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        0,
        0,
        SW_BHVR_TOGGLE, 
        SW_ST_CTXT_INIT 
    }
};

struct gpio_led gv_led_register_h801[] = {
    {
        "RGB",   // Name
        15,      // Red Pin
        13,      // Green Pin
        12,      // Blue Pin
        0,       // Manual Pin
        // Initial program
        "0xFF0000;2;1000,0x00FF00;2;1000,0x0000FF;2;1000"       
    },
    {
        "W1",    // Name
        NO_PIN,  // Red Pin
        NO_PIN,  // Green Pin
        14,      // Blue Pin
        NO_PIN,  // Manual Pin
        // Initial program
        "0xFF;2;0,0x00;2;0"       
    },
    {
        "W2",     // Name
        NO_PIN,   // Red Pin
        NO_PIN,   // Green Pin
        4,        // Blue Pin
        NO_PIN,   // Manual Pin
        // Initial program
        "0xFF;2;0,0x00;2;0"       
    },
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        ""
    }
};

struct gpio_sensor gv_sensor_register_h801[] = {
    {
        // terminator.. never delete
        NULL,
        GP_SENS_TYPE_NONE,
        0,
        0,
        NULL,
        0,
        0
    }
};

// Generic empty LED reg for most devices
struct gpio_led gv_led_register_dummy[] = {
    {
        // terminator.. never delete this
        NULL,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        NO_PIN,
        ""
    }
};


// Device Profiles
// The device profile ties together a switch,
// sensor and led register into a single structure
// complete with desired boot program pin and wifi LED
// status pin

struct device_gpio_profile {
    char *name;
    int boot_program_pin;
    int wifi_led_pin;
    struct gpio_switch *switch_register;
    struct gpio_sensor *sensor_register;
    struct gpio_led *led_register;
};

// The final profile register then ties all our in-memory
// arrays together into a set of supported profiles.
// When a device is first flashed, the default profile will be
// set to the first in the list. This can be changed from AP
// mode web I/F

struct device_gpio_profile gv_profile_register[] = {
    {
        "Sonoff Basic",                       // Name
        0,                                    // Boot Pin
        13,                                   // Wifi LED Pin
        &gv_switch_register_sonoff_basic[0],  // Switch Register
        &gv_sensor_register_sonoff_basic[0],  // Sensor Register
        &gv_led_register_dummy[0]             // LED Register
    },
    {
        "Sonoff S20",                         // Name
        0,                                    // Boot Pin
        13,                                   // Wifi LED Pin
        &gv_switch_register_sonoff_s20[0],    // Switch Register
        &gv_sensor_register_sonoff_s20[0],    // Sensor Register
        &gv_led_register_dummy[0]             // LED Register
    },
    {
        "ESP-01",                             // Name
        0,                                    // Boot Pin
        1,                                    // Wifi LED Pin
        &gv_switch_register_esp01[0],         // Switch Register
        &gv_sensor_register_esp01[0],         // Sensor Register
        &gv_led_register_dummy[0]             // LED Register
    },
    {
        "H801 LED Controller",                // Name
        0,                                    // Boot Pin
        1,                                    // Wifi LED Pin
        &gv_switch_register_h801[0],          // Switch Register
        &gv_sensor_register_h801[0],          // Sensor Register
        &gv_led_register_h801[0]              // LED Register
    },
    {
        // terminator.. never delete
        NULL,
        0,
        0,
        NULL,
        NULL,
        NULL
    }
};
