// ESP-8266 Sketch for a standalone device
// using what I call the JBHASD "API" 
// (Json-Based Home Automation with Service Discovery)
// 
// Cormac Long April 2017
//
// The defaults below will work with a Sonoff wifi switch
// but can easily be adapted for other breakouts
// 
// These teo globals define the GPIO pins for the reset button 
// and LED indicator. They default to the values used on the 
// Sonoff devices..
//
// gv_boot_program_pin = 0;
// gv_wifi_led_pin = 13;
//
// Then further down, you will see the in-memory array defining the 
// switches..
// 
// struct gpio_switch gv_switch_register[] = {
//     { "A",  12, 13,  0,  1,  0 }, // Standard Sonoff 
//     { "B",  -1, -1, -1,  0,  0 }, // dummy switch
//     { "C",  -1, -1, -1,  0,  0 }, // dummy switch
//     { "D",  -1, -1, -1,  0,  0 }, // dummy switch
//     { NULL, -1, -1, -1, -1, -1 }  // terminator.. never delete this
// };
// 
// This array defines one functioning switch defaulted to the name "mains" that uses
// a relay GPIO of pin 12, LED indicator of 13, manual over-ride pin 0 and initial 
// state of on. I've also added some dummy entries to this to let it present 
// additional switches. The use of -1 for the pin assignments essentially
// nobbles these dummies from doing anything. But youl easily populate these
// for additional controls on the larger breakout variations of the ESP-8266
// or delete them. The last array entry acts as a terminator for the scan
//
// When you configure the device in AP mode, you can rename each of the defined
// switches or leave the name unset which effectively disables it.
// So the objective is to set this array in firmware according to how you
// have built the device and then at runtime give names to switches based on 
// what they are attached to. 

#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>

// EEPROM Configuration
// It uses a struct of strings which is cast
// directly against the eeprom array
// the "marker" field is used to store
// a dummy value as a means of 
// detecting legit config on the first boot
// That string can be changed as config is remodelled
// to ensure a first read is interpreted as blank
#define MAX_FIELD_LEN 20
#define MAX_SWITCHES 20
#define CFG_MARKER_VAL 0xB3
struct eeprom_config {
    unsigned char marker;
    char zone[MAX_FIELD_LEN];
    char wifi_ssid[MAX_FIELD_LEN];
    char wifi_password[MAX_FIELD_LEN];
    char switch_names[MAX_SWITCHES][MAX_FIELD_LEN];
    unsigned char switch_initial_states[MAX_SWITCHES];
} gv_config;

// Runtime mode
// Simple enum toggle between modes
// allowing common areas such as loop to 
// perform mode-specific tasks
enum gv_mode_enum {
  MODE_INIT,
  MODE_WIFI_AP,
  MODE_WIFI_STA
};
enum gv_mode_enum gv_mode = MODE_INIT;

// Boot time programming pin
// If grounded within first 5 seconds, it 
// causes the device to enter AP mode for 
// configuration
// Pin 0 seems the most compatible matching the
// Sonoff reset button
int gv_boot_program_pin = 0;

// LED indicator pin for AP
// and STA connect states
int gv_wifi_led_pin = 13;

// Web and DNS stuff
// Used to serve the web content
// in both AP (config) and STA (client) modes
// DNS server used only in AP mode to provide
// captive DNS and have most phones/tablets 
// auto-launch the landing config page
#define WEB_PORT 80
ESP8266WebServer gv_web_server(WEB_PORT);
char gv_large_buffer[4096];
char gv_small_buffer[512];
const byte DNS_PORT = 53;
IPAddress gv_ap_ip(192, 168, 1, 1);
IPAddress gv_sta_ip;
DNSServer gv_dns_server;
char gv_mdns_hostname[MAX_FIELD_LEN];

// Definition for the use of GPIO pins as 
// switches where one pin can control a relay
// and another a LED
// Both PIN selections are optional (-1 implies unset)
struct gpio_switch {
    char *name;
    char relay_pin; // output pin used fro relay
    char led_pin; // output pin used for LED
    char manual_pin; // input pin used fro manual toggle
    char initial_state;
    char current_state;
};

// In memory definition of the gpio switches/leds we have
// last record of {NULL, -1, -1, -1, -1, -1} is the terminator 
// for the array so don't delete that.
// So read these as the name, relay-pin, LED-pin, manual-pin, init state and 
// current state.
// Either or both pins can be -1 to select nothing for that option
// init state of 1 is on, 0 is off
// The name field acts as a default but may be overridden by config
// The current state field is used at run-time to hold current on/off state.
// So while it gets initialised here, it will really be set at runtime as 
// items are turned on or off.
// Excluding the last NULL entry, this number of entries 
// in this array should not exceed MAX_SWITCHES
struct gpio_switch gv_switch_register[] = {
    { "A",  12, 13,  0,  1,  0 }, // Standard Sonoff 
    { "B",  -1, -1, -1,  0,  0 }, // dummy switch
    { "C",  -1, -1, -1,  0,  0 }, // dummy switch
    { "D",  -1, -1, -1,  0,  0 }, // dummy switch
    { NULL, -1, -1, -1, -1, -1 }  // terminator.. never delete this
};

// Not sure how standard LOW,HIGH norms are
// but these registers allow us customise as required.
// Each array will be indexed with a state value of 0 or 1
// and used to return the appropriate LOW or HIGH value to use
// to act on that boolean input
// So index 1 of gv_switch_state_reg returns HIGH as the state
// needed to turn on the switch
// Similarly index 1 of gv_led_state_reg returns LOW as the state
// needed to turn on the LED
int gv_switch_state_reg[] = { LOW, HIGH };
int gv_led_state_reg[] = { HIGH, LOW };

void set_switch_state(const char *name,
                      int index,
                      int state)
{
    int i = 0;
    int found = 0;

    Serial.printf("set_switch_state(name=%s, index=%d, state=%d)\n",
                  name,
                  index,
                  state);

    // Trust index if provided
    // skips linear search
    // essentially ignores the name arg
    if (index >= 0) {
        i = index;
        found = 1;
    }
    else {
        // Check for a value for name
        // Run the risk of matching against an empty named switch otherwise
        if (!name or strlen(name) == 0) {
            Serial.printf("no name specified for switch.. ignoring\n");
            return;
        }
        // locate the switch by name in register
        while (gv_switch_register[i].name && !found) {
            if (!strcmp(gv_switch_register[i].name, name)) {
                found = 1;
                Serial.printf("found switch in register\n");
            }
            else {
                i++;
            }
        }
    }

    // set state to 1 or 0
    // for safety because of array derefs
    if (state) {
        // on
        state = 1;
    }
    else {
        // off
        state = 0;    
    }

    // change state as requested
    if (found) {
        // Set the current state
        gv_switch_register[i].current_state = state;
        
        if (gv_switch_register[i].relay_pin != -1) {
            digitalWrite(gv_switch_register[i].relay_pin, 
                         gv_switch_state_reg[state]);    
            }
        if (gv_switch_register[i].led_pin != -1) {
            digitalWrite(gv_switch_register[i].led_pin, 
                         gv_led_state_reg[state]);
            }
    }
    else {
        Serial.printf("switch not found in register\n");
    }
}

void setup_switches()
{
    int i;

    Serial.printf("setup_switches()\n");

    // loop until we reach the terminator where
    // name is NULL
    i = 0;
    while (gv_switch_register[i].name) {

        // Over-ride hard-coded name and initial state 
        // with values set in config
        gv_switch_register[i].name = gv_config.switch_names[i];
        gv_switch_register[i].initial_state = gv_config.switch_initial_states[i];

        // Only service switches with set names
        // Allows for the config to disable hard-coded
        // defaults
        if (strlen(gv_switch_register[i].name) > 0) {
            Serial.printf("Setting up switch:%s, initial state:%d\n",
                         gv_switch_register[i].name,
                         gv_switch_register[i].initial_state);
        
            if (gv_switch_register[i].relay_pin != -1) {
                Serial.printf("    switch pin:%d\n",
                              gv_switch_register[i].relay_pin);
                pinMode(gv_switch_register[i].relay_pin, OUTPUT);
             }
        
            if (gv_switch_register[i].led_pin != -1) {
                Serial.printf("    LED pin:%d\n",
                              gv_switch_register[i].led_pin);
                pinMode(gv_switch_register[i].led_pin, OUTPUT);
            }

            if (gv_switch_register[i].manual_pin != -1) {
                Serial.printf("    Manual pin:%d\n",
                             gv_switch_register[i].manual_pin);
                pinMode(gv_switch_register[i].manual_pin, INPUT_PULLUP);
            }

            // set initial state
            set_switch_state(gv_switch_register[i].name,
                             i,
                             gv_switch_register[i].initial_state);
        }
        i++; // to the next entry in register
    }
}

void check_manual_switches()
{
    int i;
    int button_state;
    int delay_msecs = 500;

    //disabled to keep the serial activity quiet
    //Serial.printf("check_manual_switches()\n");
    //delay(delay_msecs);

    // loop until we reach the terminator where
    // name is NULL
    i = 0;
    while (gv_switch_register[i].name) {

        // Only work with entries with a set switchname
        // and manual pin
        // Excludes non-relevant or config-disabled entries
        if (strlen(gv_switch_register[i].name) > 0 && 
            gv_switch_register[i].manual_pin != -1) {
            //Serial.printf("Check Manual pin:%d\n", gv_switch_register[i].manual_pin);
            button_state = digitalRead(gv_switch_register[i].manual_pin);
            if (button_state == LOW) {
                Serial.printf("Detected manual push on switch:%s pin:%d\n",
                              gv_switch_register[i].name,
                              gv_switch_register[i].manual_pin);
                set_switch_state(gv_switch_register[i].name,
                                 i,
                                 (gv_switch_register[i].current_state + 1) % 2);
                // protect against a 2nd press detection
                delay(delay_msecs);
            }
        }
        i++; // to the next entry in register
    }
}

const char *get_json_status()
{
    char *str_ptr;
    int i;

    Serial.printf("get_json_status()\n");

    /* 
     *  { "name": "%s", "zone": "%s", "controls": [%s], "system" : { "reset_reason" : "%s", "free_heap" : %d, 
     *  "chip_id" : %d, "flash_id" : %d, "flash_size" : %d, "flash_real_size" : %d, "flash_speed" : %d, "cycle_count" : %d }  }
     *  Control: { "name": "%s", "type": "%s", "state": %d }
     */

    str_ptr = gv_small_buffer;
    gv_small_buffer[0] = 0;
    i = 0;
    while(gv_switch_register[i].name) {

        // only detail configured switches
        // Those disabled will have had their name
        // set empty
        if (strlen(gv_switch_register[i].name) > 0) {
            if (str_ptr != gv_small_buffer) {
                // separator
                str_ptr += ets_sprintf(str_ptr, ", ");
            }
            str_ptr += ets_sprintf(str_ptr,
                                   "{ \"name\": \"%s\", \"type\": \"switch\", \"state\": %d }",
                                    gv_switch_register[i].name,
                                    gv_switch_register[i].current_state);
        }
        i++;
    }
                                
    ets_sprintf(gv_large_buffer,
                "{ \"name\": \"%s\", \"zone\": \"%s\", \"controls\": [%s], "
                "\"system\" : { \"reset_reason\" : \"%s\", \"free_heap\" : %u, "
                "\"chip_id\" : %u, \"flash_id\" : %u, \"flash_size\" : %u, "
                "\"flash_real_size\" : %u, \"flash_speed\" : %u, \"cycle_count\" : %u } }\n",
                 gv_mdns_hostname,
                 gv_config.zone,
                 gv_small_buffer,
                 ESP.getResetReason().c_str(),
                 ESP.getFreeHeap(),
                 ESP.getChipId(),
                 ESP.getFlashChipId(),
                 ESP.getFlashChipSize(),
                 ESP.getFlashChipRealSize(),
                 ESP.getFlashChipSpeed(),
                 ESP.getCycleCount());

    return gv_large_buffer;
}

void load_config() 
{
    int i;
    Serial.printf("load_config()\n");
    
    Serial.printf("Read EEPROM data..(%d bytes)\n", sizeof(gv_config));

    EEPROM.begin(512);
    EEPROM.get(0, gv_config);

    if (gv_config.marker != CFG_MARKER_VAL) {
        Serial.printf("marker field not matched to special value.. assuming unconfigured\n");
        // memset to 0, empty strings galore
        memset(&gv_config, 0, sizeof(gv_config));
    }
    else {
        Serial.printf("Marker:%02X\n"
                      "Zone:%s\n"
                      "Wifi SSID:%s\n"
                      "Wifi Password:%s\n",
                      gv_config.marker,
                      gv_config.zone,
                      gv_config.wifi_ssid,
                      gv_config.wifi_password);

        // Print values of each switch name
        for (i = 0; i < MAX_SWITCHES; i++) {
            // format switch arg name
            Serial.printf("Switch[%d]:%s state:%d\n", 
                          i, 
                          gv_config.switch_names[i],
                          gv_config.switch_initial_states[i]);
        }
    }
}

void save_config() 
{
    Serial.printf("save_config()\n");
    int i;
    
    // set marker field to special value
    gv_config.marker = CFG_MARKER_VAL;
    
    Serial.printf("Writing EEPROM data..\n");
    Serial.printf("Marker:%d\n"
                  "Zone:%s\n"
                  "Wifi SSID:%s\n"
                  "Wifi Password:%s\n",
                  gv_config.marker,
                  gv_config.zone,
                  gv_config.wifi_ssid,
                  gv_config.wifi_password);

    // Print values of each switch name
    for (i = 0; i < MAX_SWITCHES; i++) {
        // format switch arg name
        Serial.printf("Switch[%d]:%s state:%d\n", 
                      i, 
                      gv_config.switch_names[i],
                      gv_config.switch_initial_states[i]);
    }
    
    EEPROM.begin(512);
    EEPROM.put(0, gv_config);
    EEPROM.commit();
}

void ap_handle_root() {
    int i;
    
    Serial.printf("ap_handle_root()\n");
    if (gv_web_server.hasArg("zone")) {
        strcpy(gv_config.zone, 
               gv_web_server.arg("zone").c_str());
        strcpy(gv_config.wifi_ssid, 
               gv_web_server.arg("ssid").c_str());
        strcpy(gv_config.wifi_password, 
               gv_web_server.arg("password").c_str());

        for (i = 0; i < MAX_SWITCHES; i++) {
            Serial.printf("Getting post args for switches %d/%d\n",
                          i, MAX_SWITCHES - 1);
            // format switch arg name
            ets_sprintf(gv_small_buffer,
                        "switch%d",
                        i);
            // Retrieve if present
            if (gv_web_server.hasArg(gv_small_buffer)) {
                Serial.printf("Arg %s present\n",
                              gv_small_buffer);

                // Be careful here. Had to strcpy against
                // the address of the first char of the string array
                // just using gv_config.switch_names[i] on its own
                // caused exceptions so it needed to be clearly spelled 
                // out in terms of address
                strcpy(&(gv_config.switch_names[i][0]), 
                       gv_web_server.arg(gv_small_buffer).c_str());
                Serial.printf("Got:%s:%s\n", 
                              gv_small_buffer,
                              gv_config.switch_names[i]);
            }
             
            // format state arg name
            ets_sprintf(gv_small_buffer,
                        "state%d",
                        i);
            // Retrieve if present
            if (gv_web_server.hasArg(gv_small_buffer)) {
                Serial.printf("Arg %s present\n",
                              gv_small_buffer);
                              
                gv_config.switch_initial_states[i] =
                    atoi(gv_web_server.arg(gv_small_buffer).c_str());
                Serial.printf("Got:%s:%d\n", 
                              gv_small_buffer, 
                              gv_config.switch_initial_states[i]);
            }
        }
        
        save_config();
        gv_web_server.send(200, "text/html", "Applying settings and rebooting");
        ESP.restart();
    }
    else {
        gv_web_server.send(200, "text/html", gv_large_buffer);
    }
}

void toggle_wifi_led(int delay_msecs)
{
    static int state = 0;

    // toggle
    state = (state + 1) % 2;

    digitalWrite(gv_wifi_led_pin, 
                 gv_led_state_reg[state]);

    delay(delay_msecs);
}

void start_ap_mode()
{
    Serial.printf("start_ap_mode()\n");
    gv_mode = MODE_WIFI_AP;
    int i;

    ets_sprintf(
        gv_large_buffer,
        "<h2>%s Setup</h2>"
        "<form action=\"/\" method=\"post\">"
        "<div>"
        "    <label>Zone:</label>"
        "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"zone\">"
        "</div>"
        "<div>"
        "    <label>WIFI SSID:</label>"
        "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"ssid\">"
        "</div>"
        "<div>"
        "    <label>WIFI Password:</label>"
        "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"password\">"
        "</div>",
        gv_mdns_hostname,
        gv_config.zone, 
        MAX_FIELD_LEN,
        gv_config.wifi_ssid,
        MAX_FIELD_LEN,
        gv_config.wifi_password,
        MAX_FIELD_LEN);

    // append name entries for switches    
    i = 0;
    while (gv_switch_register[i].name) {
        ets_sprintf(
            gv_small_buffer,
            "<div>"
            "    <label>Switch %d</label>"
            "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"switch%d\">"
            "    <select name=\"state%d\">"
            "        <option value=\"1\">On</option>"
            "        <option value=\"0\">Off</option>"
            "    </select>"
            "</div>",
            i + 1,
            gv_config.switch_names[i],
            MAX_FIELD_LEN,
            i,
            i);
        strcat(gv_large_buffer, gv_small_buffer);
        i++; // to the next entry in register
    }

    // Terminate form with post button and </form>
    strcat(gv_large_buffer, 
           "<div>"
           "    <button>Apply</button>"
           "</div>"
           "</form>");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(gv_ap_ip, gv_ap_ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(gv_mdns_hostname);

    // Captive DNS
    gv_dns_server.start(DNS_PORT, "*", gv_ap_ip);
    
    Serial.printf("AP IP:%d.%d.%d.%d\n", 
                  gv_ap_ip[0],
                  gv_ap_ip[1],
                  gv_ap_ip[2],
                  gv_ap_ip[3]);

   
    gv_web_server.on("/", ap_handle_root);
    gv_web_server.on("/apply", ap_handle_root);
    gv_web_server.onNotFound(ap_handle_root);
    gv_web_server.begin();
    Serial.printf("HTTP server started for AP mode\n"); 
}

void sta_handle_root() {
    int switch_state;
    int i;

    Serial.printf("sta_handle_root()\n");

    // Check for switch and state
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("state")) {
        strcpy(gv_small_buffer, gv_web_server.arg("control").c_str());
        switch_state = atoi(gv_web_server.arg("state").c_str());
        set_switch_state(gv_small_buffer, -1, switch_state); // specifying name only
    }

    // Will display basic info page
    // with on/off buttons per configured 
    // switch
    ets_sprintf(gv_large_buffer,
                "<h2>%s</h2>"
                "<p>Zone: %s</p>",
                gv_mdns_hostname, 
                gv_config.zone);

    // append entries for switches
    // as simple on/off switch pairs 
    i = 0;
    while (gv_switch_register[i].name) {
        if (strlen(gv_switch_register[i].name) > 0) {
            ets_sprintf(gv_small_buffer,
                        "<p><a href=\"/?control=%s&state=1\"><button>%s On</button></a>&nbsp;"
                        "<a href=\"/?control=%s&state=0\"><button>%s Off</button></a></p>",
                        gv_config.switch_names[i],
                        gv_config.switch_names[i],
                        gv_config.switch_names[i],
                        gv_config.switch_names[i]);
            strcat(gv_large_buffer, gv_small_buffer);
        }
        i++; // to the next entry in register
    }

    gv_web_server.send(200, "text/html", gv_large_buffer);
}

void sta_handle_json() {
    int switch_state;
    
    Serial.printf("sta_handle_json()\n");

    // Check for switch and state
    if (gv_web_server.hasArg("control") && gv_web_server.hasArg("state")) {
        strcpy(gv_small_buffer, gv_web_server.arg("control").c_str());
        switch_state = atoi(gv_web_server.arg("state").c_str());
        set_switch_state(gv_small_buffer, -1, switch_state); // specifying name only
    }

    // Return current status as standard
    gv_web_server.send(200, "text/html", get_json_status());

    // reboot if directed
    if (gv_web_server.hasArg("reboot")) {
        Serial.printf("Received reboot command\n");
        ESP.restart();
    }
}

void start_sta_mode()
{
    int connect_timeout = 120;
    
    Serial.printf("start_sta_mode()\n");
    gv_mode = MODE_WIFI_STA;

    Serial.printf("Connecting to Wifi SSID:%s, Password:%s, Timeout:%d\n", 
                  gv_config.wifi_ssid,
                  gv_config.wifi_password,
                  connect_timeout);
    
    // WIFI
    WiFi.mode(WIFI_STA);
    WiFi.begin(gv_config.wifi_ssid, 
               gv_config.wifi_password);
               
    while (WiFi.status() != WL_CONNECTED && 
           connect_timeout > 0) {
        delay(1000);
        toggle_wifi_led(1000);
        Serial.printf(".");
        connect_timeout--;
    }

    Serial.printf("\n");

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("Timed out trying to connect to %s\n",
                      gv_config.wifi_ssid);
        return;  
    }

    // Activate switches
    // to reset all to defaults
    // including the status LED if used
    setup_switches();

    gv_sta_ip = WiFi.localIP();
    Serial.printf("Connected.. IP:%d.%d.%d.%d\n", 
                  gv_sta_ip[0],
                  gv_sta_ip[1],
                  gv_sta_ip[2],
                  gv_sta_ip[3]);

    // MDNS & DNS-SD using "JBHASD"
    Serial.printf("Activating MDNS for hostname:%s\n", gv_mdns_hostname);
    if (!MDNS.begin(gv_mdns_hostname)) {
        Serial.printf("Error setting up MDNS responder!\n");
    }
    else {
        Serial.printf("Activating DNS-SD\n");
        MDNS.addService("JBHASD", "tcp", WEB_PORT);  
    }
    
    gv_web_server.on("/", sta_handle_root);
    gv_web_server.on("/json", sta_handle_json);
    gv_web_server.onNotFound(sta_handle_root);
    gv_web_server.begin();
    Serial.printf("HTTP server started for client mode\n"); 
}

void setup() 
{
    Serial.begin(115200);
    delay(1000);

    gv_mode = MODE_INIT;

    // Set mdns hostname based on chip ID and prefix
    // will also use this for AP SSID
    ets_sprintf(gv_mdns_hostname,
               "esp8266-%d",
               ESP.getChipId());

    // Get the config at this stage
    load_config();

    // Activate switches
    // will perform full setup
    // even though we may over-ride some pins
    // helps get relays on ASAP if you have chosen
    // initial state of on
    setup_switches();

    // Set up status LED
    pinMode(gv_wifi_led_pin, OUTPUT);

    // 5 seconds or so to activate the reset pin
    // in the form of 25x200ms delay
    pinMode(gv_boot_program_pin, INPUT_PULLUP);
    int pin_wait_timer = 25;
    int delay_msecs = 200;
    int button_state;

    Serial.printf("Entering pin wait stage\n");
    while (pin_wait_timer > 0) {
        Serial.printf("Button wait #%d\n", pin_wait_timer);
        toggle_wifi_led(delay_msecs);
        button_state = digitalRead(gv_boot_program_pin);
        if (button_state == LOW) {
            Serial.printf("Detected pin down.. going to AP mode\n");
            start_ap_mode();
            return;
        }
        pin_wait_timer--;
    }

    Serial.printf("Passed pin wait stage.. normal startup\n");
    // If we have no SSID provisioned
    // then we go for AP mode
    if (strlen(gv_config.wifi_ssid) == 0) {
        start_ap_mode();
    }
    else {
        start_sta_mode();
    }
}

void loop() 
{
    switch (gv_mode) {
      case MODE_INIT:
        break;
        
      case MODE_WIFI_AP:
        gv_dns_server.processNextRequest();
        gv_web_server.handleClient();
        toggle_wifi_led(100); // fast flashing in AP mode
        break;
      case MODE_WIFI_STA:
        gv_web_server.handleClient();
        check_manual_switches();
        break;
    }
}
