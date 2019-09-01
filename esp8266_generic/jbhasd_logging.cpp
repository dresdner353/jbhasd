#include "HandyTaskMan.h"
#include "jbhasd_types.h"

// Function start_serial
// Starts serial logging after
// checking GPIO pin usage
// across switches and sensors
// Pins 3 and 1 are the danger scenarios 
// here.. if in use, serial logging can't be 
// performed
void start_serial(void)
{
    if (!pin_in_use(3) &&  // Rx
        !pin_in_use(1)) {  // Tx
        gv_logging = LOGGING_SERIAL;
        Serial.begin(115200);
        delay(1000);
    }
}

// Global logging mode
enum gv_logging_enum gv_logging = LOGGING_NONE;

// Telnet server
// Used for logging diversion from serial
// to connected clients
WiFiServer gv_telnet_server(23);
WiFiClient gv_telnet_clients[MAX_TELNET_CLIENTS];
uint8_t  gv_num_telnet_clients = 0;


// Function: vlog_message
// Wraps calls to Serial.print or connected
// telnet client
// takes form of typical vprintf-like function
// with va_list passed in args where upper
// function is managing the va_start/end
void vlog_message(char *format, va_list args)
{
    static char log_buf[LOGBUF_MAX + 1];
    uint8_t  i;
    uint8_t  prefix_len;
    uint32_t now;
    uint16_t days;
    uint16_t hours;
    uint16_t mins;
    uint16_t secs;
    uint16_t msecs;
    uint16_t remainder;
    
    if (gv_logging == LOGGING_NONE) {
        // Logging not enabled
        return;
    }

    if (gv_logging == LOGGING_NW_CLIENT &&
        gv_num_telnet_clients == 0) {
        // No logging to do if we have no
        // network clients
        return;
    }

    // Timestamp
    // Break down relative msecs
    // into days, hours. mins, secs & msecs
    now = millis();
    days = now / (1000 * 60 * 60 * 24);
    remainder = now % (1000 * 60 * 60 * 24);
    hours = remainder / (1000 * 60 * 60);
    remainder = remainder % (1000 * 60 * 60);
    mins = remainder / (1000 * 60);
    remainder = remainder % (1000 * 60);
    secs = remainder / 1000;
    msecs = remainder % 1000;

    // pre-write timestamp to log buffer
    ets_sprintf(log_buf,
                "%02u:%02u:%02u:%02u:%03u  ",
                days,
                hours,
                mins,
                secs,
                msecs);

    prefix_len = strlen(log_buf);

    // handle va arg list and write to buffer offset by 
    // existing timestamp length
    vsnprintf(log_buf + prefix_len,
              LOGBUF_MAX - prefix_len,
              format,
              args);
    log_buf[LOGBUF_MAX] = 0; // force terminate last character

    // CRLF termination
    strcat(log_buf, "\r\n");

    switch(gv_logging) {
      case LOGGING_SERIAL:
        Serial.print(log_buf);
        break;

      case LOGGING_NW_CLIENT:
        for(i = 0; i < MAX_TELNET_CLIENTS; i++) {
            if (gv_telnet_clients[i] && 
                gv_telnet_clients[i].connected()) {
                gv_telnet_clients[i].write((uint8_t*)log_buf, 
                                        strlen(log_buf));
                gv_telnet_clients[i].flush();
            }
        }
        break;
    }
}


// va_start/end wrapper for vlog_message()
// used directly in this code as a top
// level call point
void log_message(char *format, ... )
{
    va_list args;

    va_start(args, format);
    vlog_message(format, args);
    va_end(args);

}


// Function loop_task_telnet
// loop function for driving telnet
// session handling both accepting new
// sessions and flushing data from existing
// sessions
void loop_task_telnet(void)
{
    uint8_t i;
    static char banner[128];

    if (gv_logging != LOGGING_NW_CLIENT) {
        return;
    }

    // check for new sessions
    if (gv_telnet_server.hasClient()) {
        for(i = 0; i < MAX_TELNET_CLIENTS; i++) {
            // find free/disconnected spot
            if (!gv_telnet_clients[i] || 
                !gv_telnet_clients[i].connected()) {
                if(gv_telnet_clients[i]) {
                    gv_telnet_clients[i].stop();
                    gv_num_telnet_clients--;
                }
                gv_telnet_clients[i] = gv_telnet_server.available();
                gv_num_telnet_clients++;
                ets_sprintf(banner,
                            "JBHASD Logging Console client %d/%d\r\n"
                            "Name:%s Zone:%s\r\n",
                            i + 1,
                            MAX_TELNET_CLIENTS,
                            gv_device.hostname,
                            gv_device.zone);
                gv_telnet_clients[i].write((uint8_t*)banner, 
                                        strlen(banner));
                continue;
            }
        }

        //no free/disconnected slot so reject
        WiFiClient serverClient = gv_telnet_server.available();
        ets_sprintf(banner,
                    "JBHASD %s Logging Console.. no available slots\r\n",
                    gv_device.hostname);
        serverClient.write((uint8_t*)banner, 
                           strlen(banner));
        serverClient.stop();
    }

    //check clients for data
    for (i = 0; i < MAX_TELNET_CLIENTS; i++) {
        if (gv_telnet_clients[i] && 
            gv_telnet_clients[i].connected()) {
            if(gv_telnet_clients[i].available()) {
                // throw away any data
                while(gv_telnet_clients[i].available()) {
                    gv_telnet_clients[i].read();
                }
            }
        }
    }
}


// Function start_telnet
// enables telnet server
void start_telnet(void)
{
    log_message("start_telnet()");

    if (!gv_device.telnet_enabled) {
        log_message("Telnet not enabled.. returning");
        return;
    }

    // start telnet server
    gv_telnet_server.begin();
    gv_telnet_server.setNoDelay(true);

    gv_logging = LOGGING_NW_CLIENT;

    // Telnet Sessions every 1 second
    TaskMan.add_task("Telnet Sessions",
                     RUN_STATE_WIFI_STA_UP,
                     1000,
                     loop_task_telnet);
}
