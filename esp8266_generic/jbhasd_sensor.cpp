#include "HandyTaskMan.h"
#include "jbhasd_types.h"

// Function gpio_sensor_alloc
// Allocates a GPIO sensor struct
struct gpio_sensor* gpio_sensor_alloc(void)
{
    struct gpio_sensor *gpio_sensor;

    gpio_sensor = (struct gpio_sensor*) malloc(sizeof(struct gpio_sensor));

    return gpio_sensor;
}


// Function: setup_sensors
void setup_sensor(struct gpio_sensor *gpio_sensor)
{
    DHT *dhtp;

    log_message("setup_sensor(name:%s)", gpio_sensor->name);

    switch (gpio_sensor->sensor_type) {
      case GP_SENS_TYPE_NONE:
        // do nothing
        log_message("    Unknown Type (dummy)");
        break;

      case GP_SENS_TYPE_DHT:
        log_message("    DHT Type %d on pin %d",
                    gpio_sensor->sensor_variant,
                    gpio_sensor->sensor_pin);

        if (gpio_sensor->sensor_pin != NO_PIN) {
            // Setup DHT temp/humidity sensor and record
            // class pointer in void* ref
            dhtp = new DHT(gpio_sensor->sensor_pin,
                           gpio_sensor->sensor_variant);
            gpio_sensor->ref = dhtp;
        }
        else {
            log_message("    Sensor not assigned to pin (fake)");
            // non-pin assigned DHT
            // for faking/simulation
            gpio_sensor->ref = NULL;
        }
        break;
    }
}

// Function: float_get_fp
// Returns floating point part of float
// as integer. Needed due to limitations of
// formatting where it cant handle %f in ets_sprintf
uint32_t float_get_fp(float f, uint8_t precision) {

    int32_t f_int;
    uint32_t f_fp;
    double pwr_of_ten;

    // Calculate power of ten for precision
    pwr_of_ten = pow(10, precision);

    // Integer part
    f_int = (int)f;

    // decimal part
    if (f_int < 0) {
        f_fp = (int) (pwr_of_ten * -1 * f) % (int)pwr_of_ten;
    } else {
        f_fp = (int) (pwr_of_ten * f) % (int)pwr_of_ten;
    }

    return f_fp;
}



// Function read_sensors()
// Read sensor information from configured sensors
void read_sensors(void)
{
    DHT *dhtp;
    float f1, f2;
    struct gpio_sensor *gpio_sensor;

    log_message("read_sensors()");

    for (gpio_sensor = HTM_LIST_NEXT(gv_device.sensor_list);
         gpio_sensor != gv_device.sensor_list;
         gpio_sensor = HTM_LIST_NEXT(gpio_sensor)) {
        switch (gpio_sensor->sensor_type) {
          case GP_SENS_TYPE_DHT:
            dhtp = (DHT*)gpio_sensor->ref;

            if (gpio_sensor->sensor_pin != NO_PIN) {
                // Humidity
                f1 = dhtp->readHumidity();
                if (isnan(f1)) {
                    log_message("  Humidity sensor read failed");
                }
                else {
                    gpio_sensor->f1 = f1;
                }

                // Temp Celsius
                f2 = dhtp->readTemperature();
                if (isnan(f2)) {
                    log_message("Temperature sensor read failed");
                }
                else {
                    // record temp as read value offset
                    // by temp_offset in config
                    gpio_sensor->f2 = f2 +
                        gpio_sensor->temp_offset;
                }
            }
            else {
                // fake the values
                gpio_sensor->f1 = (ESP.getCycleCount() % 100) + 0.5;
                gpio_sensor->f2 = ((ESP.getCycleCount() +
                                    ESP.getFreeHeap()) % 100) + 0.25;
            }
            log_message("Sensor:%s "
                        "Humidity:%d.%02d "
                        "Temperature:%d.%02d "
                        "(temp offset:%d.%02d)",
                        gpio_sensor->name,
                        (int)gpio_sensor->f1,
                        float_get_fp(gpio_sensor->f1, 2),
                        (int)gpio_sensor->f2,
                        float_get_fp(gpio_sensor->f2, 2),
                        (int)gpio_sensor->temp_offset,
                        float_get_fp(gpio_sensor->temp_offset, 2));
            break;
        }
    }
}
