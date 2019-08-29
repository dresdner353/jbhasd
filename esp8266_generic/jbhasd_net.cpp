#include "HandyTaskMan.h"
#include "jbhasd_types.h"

// Web server
ESP8266WebServer gv_web_server(WEB_PORT);
#define DNS_PORT 53
static IPAddress ap_ip(192, 168, 1, 1);
static IPAddress sta_ip;
static DNSServer dns_server;

// Buffers
static char small_buffer[1024];
static char large_buffer[4096];

// status health check
static uint32_t last_status = 0;
static uint32_t last_wifi_restart = 0;
static uint16_t status_wifi_restart_count = 0;

// signal monitoring
static uint16_t signal_wifi_restart_count = 0;


// Function: get_json_status
// formats and returns a JSON string representing
// the device details, configuration status and system info
const char *get_json_status()
{
    struct gpio_switch *gpio_switch;
    struct gpio_sensor *gpio_sensor;
    struct gpio_rgb *gpio_rgb;
    struct gpio_argb *gpio_argb;
    uint32_t now;

    log_message("get_json_status()");

    // refresh sensors
    read_sensors();

    DynamicJsonDocument json_status(4096);

    // top-level fields
    json_status["name"] = gv_device.hostname;
    json_status["zone"] = gv_device.zone;
    json_status["wifi_ssid"] = gv_device.wifi_ssid;
    json_status["ota_enabled"] = gv_device.ota_enabled;
    json_status["telnet_enabled"] = gv_device.telnet_enabled;
    json_status["mdns_enabled"] = gv_device.mdns_enabled;
    json_status["manual_switches_enabled"] = gv_device.manual_switches_enabled;
    json_status["configured"] = gv_device.configured;

    // system section
    now = millis();
    JsonObject system  = json_status.createNestedObject("system");
    system["compile_date"] = gv_sw_compile_date;
    system["reset_reason"] = (char*)ESP.getResetReason().c_str();
    system["free_heap"] = ESP.getFreeHeap();
    system["chip_id"] = ESP.getChipId();
    system["flash_id"] = ESP.getFlashChipId();
    system["flash_size"] = ESP.getFlashChipSize();
    system["flash_real_size"] = ESP.getFlashChipRealSize();
    system["flash_speed"] = ESP.getFlashChipSpeed();
    system["cycle_count"] = ESP.getCycleCount();
    system["millis"] = now;
    system["status_wifi_restarts"] = status_wifi_restart_count;
    system["signal_wifi_restarts"] = signal_wifi_restart_count;

    // controls section for switches & leds 
    JsonArray controls_arr = json_status.createNestedArray("controls");

    // switches
    for (gpio_switch = HTM_LIST_NEXT(gv_device.switch_list);
         gpio_switch != gv_device.switch_list;
         gpio_switch = HTM_LIST_NEXT(gpio_switch)) {

        JsonObject obj = controls_arr.createNestedObject();
        obj["name"] = gpio_switch->name;
        obj["type"] = "switch";
        obj["state"] = gpio_switch->current_state;
        obj["context"] = get_sw_context(gpio_switch->state_context);
        obj["last_activity_millis"] = gpio_switch->last_activity;
        obj["last_activity_delta_secs"] = (now - gpio_switch->last_activity) / 1000;

        // Motion details if a motion pin in use
        if (gpio_switch->motion_pin != NO_PIN) {
            obj["motion_interval"] = gpio_switch->motion_interval;
        }

        // Manual interval and auto off detail is a manual
        // pin in use
        if (gpio_switch->manual_pin != NO_PIN) {
            obj["manual_interval"] = gpio_switch->manual_interval;
            obj["manual_auto_off"] = gpio_switch->manual_auto_off;
        }
    }

    // sensors
    for (gpio_sensor = HTM_LIST_NEXT(gv_device.sensor_list);
         gpio_sensor != gv_device.sensor_list;
         gpio_sensor = HTM_LIST_NEXT(gpio_sensor)) {

        JsonObject obj = controls_arr.createNestedObject();
        obj["name"] = gpio_sensor->name;

        switch (gpio_sensor->sensor_type) {
          case GP_SENS_TYPE_NONE:
            obj["type"] = "dummy";
            break;

          case GP_SENS_TYPE_DHT:
            obj["type"] = "temp/humidity";
            obj["humidity"] = gpio_sensor->f1;
            obj["temp"] = gpio_sensor->f2;
            break;
        }
    }

    // rgb
    for (gpio_rgb = HTM_LIST_NEXT(gv_device.rgb_list);
         gpio_rgb != gv_device.rgb_list;
         gpio_rgb = HTM_LIST_NEXT(gpio_rgb)) {

        JsonObject obj = controls_arr.createNestedObject();
        obj["name"] = gpio_rgb->name;
        obj["type"] = "rgb";
        obj["program"] = gpio_rgb->program;
        ets_sprintf(small_buffer,
                    "0x%08X",
                    gpio_rgb->current_colour);
        obj["current_colour"] = small_buffer;
        obj["step"] = gpio_rgb->step;
    }
    
    // argb
    for (gpio_argb = HTM_LIST_NEXT(gv_device.argb_list);
         gpio_argb != gv_device.argb_list;
         gpio_argb = HTM_LIST_NEXT(gpio_argb)) {

        JsonObject obj = controls_arr.createNestedObject();
        obj["name"] = gpio_argb->name;
        obj["type"] = "argb";
        obj["program"] = gpio_argb->program;
    }

    // Format string in prety format
    serializeJsonPretty(json_status, large_buffer);

    log_message("JSON status data: (%d bytes) \n%s", 
                strlen(large_buffer), 
                large_buffer);

    return large_buffer;
}


// Function: ap_handle_root
// Provides basic web page allowing user to select WiFI
// and input pasword. The current JSON config is also shown
// The same code will process a rest request and setting of SSID
// as part of the POST handling
void ap_handle_root() 
{
    uint8_t i;
    char *str_ptr;
    char *selected_str;
    static uint8_t initial_scan = 1;

    static uint8_t num_networks;
    uint32_t now;

    log_message("ap_handle_root()");

    // check for post args

    if (gv_web_server.hasArg("rescan") || initial_scan) {
        log_message("scanning wifi networks");
        num_networks = WiFi.scanNetworks();
        log_message("found %d SSIDs", num_networks);
        initial_scan = 0;
    }
    else if (gv_web_server.hasArg("reset") &&
        // Reset will trump others if set to 1
        strcmp(gv_web_server.arg("reset").c_str(), "1") == 0) {
        log_message("Reset via AP Mode");
        reset_config();
        gv_reboot_requested = 1;
    }
    else if (gv_web_server.hasArg("ssid")) {
        // actual normal config updates
        gv_reboot_requested = 1;

        update_config("wifi_ssid", 
                      gv_web_server.arg("ssid").c_str(),
                      0,
                      0);

        update_config("wifi_password", 
                      gv_web_server.arg("password").c_str(),
                      0,
                      1);
    }

    if (gv_reboot_requested) {
        gv_web_server.send(200, "text/html", "Applying settings and rebooting");
    }
    else {
        // Build combo list of discovered 
        // networks and try to set current SSID
        // as selected entry
        str_ptr = small_buffer;
        small_buffer[0] = '\0';

        for (i = 0; i < num_networks; i++) {

            // try to mark curent SSID selected
            if (!strcmp(gv_device.wifi_ssid, WiFi.SSID(i).c_str())) {
                selected_str = "selected";
            }
            else {
                selected_str = "";
            }

            str_ptr += ets_sprintf(str_ptr,
                                   "<option value=\"%s\" %s>%s</option>",
                                   WiFi.SSID(i).c_str(),
                                   selected_str,
                                   WiFi.SSID(i).c_str());
        }

        ets_sprintf(large_buffer,
                    "<head>"
                    "<title>JBHASD Device Setup</title>"
                    "<meta charset=\"utf-8\">"
                    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                    "</head>"
                    "<body>"
                    "<h2>%s Setup</h2>"
                    "<form action=\"/\" method=\"post\">"
                    "<div>"
                    "<input type=\"hidden\" id=\"rescan\" name=\"rescan\" value=\"1\">"
                    "<button>Rescan WiFi Networks</button>"
                    "</div>"
                    "</form>"
                    "<form action=\"/\" method=\"post\">"
                    "<div>"
                    "    <label>WIFI SSID:</label>"
                    "    <select name=\"ssid\">"
                    "    %s"
                    "    </select>"
                    "</div>"
                    "<div>"
                    "    <label>WIFI Password:</label>"
                    "    <input type=\"text\" value=\"%s\" maxlength=\"%d\" name=\"password\">"
                    "</div>"
                    "<div>"
                    "    <label>Reset Config:</label>"
                    "    <select name=\"reset\">"
                    "       <option value=\"0\" selected>No</option>"
                    "       <option value=\"1\" >Yes</option>"
                    "    </select>"
                    "</div>"
                    "<div>"
                    "<button>Apply Settings</button>"
                    "</div>"
                    "<br><br>"
                    "<br><br>"
                    "<div>"
                    "    <label>JSON Config</label>"
                    "</div>"
                    "<div>"
                    "<pre>"
                    "%s"
                    "</pre>"
                    "</div>"
                    "</form>"
                    "</body>",
            gv_device.hostname,
            small_buffer,
            gv_device.wifi_password,
            MAX_FIELD_LEN,
            gv_config);

        // Just return the pre-formatted web page we built at
        // setup
        gv_web_server.send(200, "text/html", large_buffer);
    }
}


// Function: wifi_init
// common Wifi inits 
// and the stack of task management functions for WiFi 
// modes
void wifi_init()
{
    static uint8_t first_run = 1;

    // No need to let these calls
    // get repeated
    if (!first_run) {
        return;
    }
    first_run = 0;

    // Set MDNS hostname based on prefix and chip ID
    ets_sprintf(gv_device.hostname,
                "JBHASD-%08X",
                ESP.getChipId());

    // Loop tasks
    // AP auto-reboot after 5 mins
    TaskMan.add_task("AP Reboot Timer",
                     RUN_STATE_WIFI_AP,
                     300000,
                     loop_task_ap_reboot);

    // Web Server (AP) every 50 ms
    TaskMan.add_task("Webserver",
                     RUN_STATE_WIFI_AP |
                     RUN_STATE_WIFI_STA_UP,
                     50,
                     loop_task_webserver);

    // DNS Server every 50ms
    TaskMan.add_task("DNS",
                     RUN_STATE_WIFI_AP,
                     50,
                     loop_task_dns);

    // MDNS Server every 50ms
    TaskMan.add_task("MDNS",
                     RUN_STATE_WIFI_STA_UP,
                     50,
                     loop_task_mdns);

    // AP WiFI LED every 100 ms (fast)
    TaskMan.add_task("AP Status LED",
                     RUN_STATE_WIFI_AP,
                     100,
                     loop_task_status_led);

    // STA WiFI LED every 1s (slow)
    TaskMan.add_task("STA Status LED",
                     RUN_STATE_WIFI_STA_DOWN,
                     1000,
                     loop_task_status_led);

    // WiFI Check (While Down) Every 2s
    TaskMan.add_task("WiFI Status Up Check",
                     RUN_STATE_WIFI_STA_DOWN,
                     2000,
                     loop_task_check_wifi_up);

    // WiFI Check (While Up) every 10s
    TaskMan.add_task("WiFI Status Down Check",
                     RUN_STATE_WIFI_STA_UP,
                     10000,
                     loop_task_check_wifi_down);

    if (gv_device.idle_period_wifi > 0 ||
        gv_device.idle_period_reboot > 0) {
        // Idle Check every 10s if either idle 
        // period enabled
        TaskMan.add_task("Idle Status Check",
                         RUN_STATE_WIFI_STA_UP,
                         10000,
                         loop_task_check_idle_status);
    }
}


// Function: start_wifi_ap_mode
// Sets up the device in AP mode
void start_wifi_ap_mode()
{
    wifi_init();
    log_message("start_wifi_ap_mode()");
    TaskMan.set_run_state(RUN_STATE_WIFI_AP);


    // Activate AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ap_ip,
                      ap_ip,
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(gv_device.hostname);

    // Captive DNS to try and force the client to the
    // landing page
    dns_server.start(DNS_PORT, "*", ap_ip);

    log_message("AP IP:%d.%d.%d.%d",
                ap_ip[0],
                ap_ip[1],
                ap_ip[2],
                ap_ip[3]);


    gv_web_server.on("/", ap_handle_root);
    gv_web_server.onNotFound(ap_handle_root);
    gv_web_server.begin();
    log_message("HTTP server started for AP mode");
}


// Function sta_handle_status
// callback handler for the /status 
void sta_handle_status() {
    log_message("sta_handle_status()");

    // Update timestamp of last call
    last_status = millis();

    // Return current status 
    gv_web_server.send(200, "application/json", get_json_status());
}


// Function sta_handle_reboot
// handles /reboot API call
void sta_handle_reboot() {
    log_message("sta_handle_reboot()");

    log_message("Received reboot command");
    gv_reboot_requested = 1;

    // Return current status 
    gv_web_server.send(200, "application/json", get_json_status());
}


// Function sta_handle_apmode
// handles /apmode API call
void sta_handle_apmode() {
    log_message("sta_handle_apmode()");

    log_message("Received apmode command");
    gv_device.force_apmode_onboot = 1;
    update_config("force_apmode_onboot", NULL, 1, 1);
    gv_reboot_requested = 1;

    // Return current status 
    gv_web_server.send(200, "application/json", get_json_status());
}


// Function sta_handle_reset
// resets device config when /reset API is called
void sta_handle_reset() {
    log_message("sta_handle_reset()");

    log_message("Received reset command");
    reset_config();
    gv_reboot_requested = 1;

    // Return current status 
    gv_web_server.send(200, "application/json", get_json_status());
}


// Function sta_handle_reconfigure
// Sets configured state of device to 0 to 
// invoke a configure directive from a monitoring 
// web server
void sta_handle_reconfigure() {
    log_message("sta_handle_reconfigure()");

    log_message("Received reconfigure command");
    gv_device.configured = 0;

    // Return current status 
    gv_web_server.send(200, "application/json", get_json_status());
}


// Function sta_handle_control
// performs control functions for /control API call
// Parses JSON body and it's "controls" list for actions
// to perform on specified controls
void sta_handle_control() {
    log_message("sta_handle_control()");

    // JSON POST
    // The "plain" argument is where the full POST body will be
    // if present, we work with this and totally ignore the other args
    if (gv_web_server.hasArg("plain")) {
        // POST-based JSON payload
        log_message("found plain argument. trying for JSON POST parse");
        log_message("Body: %s", 
                    gv_web_server.arg("plain").c_str());

        // Try to parse the data as JSON
        DynamicJsonDocument json_post(4096);
        DeserializationError error = deserializeJson(json_post, 
                                                     gv_web_server.arg("plain").c_str());
        if (error) {
            log_message("Failed to decode JSON");
        }
        else {
            log_message("Decoded JSON successfully");

            // Control changes
            JsonArray controls = json_post["controls"];
            if (!controls.isNull()) {
                // iterate controls and perform actions
                for (JsonObject control : controls) {
                    if (!control["name"].isNull()) {
                        // have a valid name field
                        const char* control_name = control["name"];

                        if (!control["state"].isNull()) {
                            // have a state field, treat as switch
                            set_switch_state(find_switch(control_name),
                                             control["state"],
                                             SW_ST_CTXT_NETWORK); 
                        }

                        if (!control["motion_interval"].isNull()) {
                            // have a motion_interval field, treat as switch
                            set_switch_motion_interval(find_switch(control_name),
                                                       control["motion_interval"]);
                        }

                        if (!control["manual_interval"].isNull()) {
                            // have a manual_interval field, treat as switch
                            set_switch_manual_interval(find_switch(control_name),
                                                       control["manual_interval"]);
                        }

                        if (!control["manual_auto_off"].isNull()) {
                            // have a manual_auto_off field, treat as switch
                            set_switch_manual_auto_off(find_switch(control_name),
                                                       control["manual_auto_off"]);
                        }

                        if (!control["program"].isNull()) {
                            // have a program field, treat as RGB or aRGB
                            set_rgb_program(find_rgb(control_name), 
                                            control["program"]);
                            set_argb_program(find_argb(control_name), 
                                             control["program"]);
                        }
                    }
                }
            }
        }
    }

    // Return current status as standard response
    gv_web_server.send(200, "application/json", get_json_status());
}


// Function sta_handle_configure
// Processes /configure API call 
// applying JSON POST contents as the new saved config
void sta_handle_configure() {
    log_message("sta_handle_configure()");

    DynamicJsonDocument json_response(1024);
    char response_str[512];

    // JSON POST
    // The "plain" argument is where the full POST body will be
    // if present, we work with this and totally ignore the other args
    if (!gv_web_server.hasArg("plain")) {
        log_message("No POST body present");
        json_response["error"] = 1;
        json_response["desc"] = "No POST Payload found";
    }
    else {
        // POST present
        log_message("POST Body: %s", 
                    gv_web_server.arg("plain").c_str());

        // Try to parse the body as JSON
        DynamicJsonDocument json_post(4096);
        DeserializationError error = deserializeJson(json_post, 
                                                     gv_web_server.arg("plain").c_str());
        if (error) {
            log_message("Failed to decode JSON payload");
            json_response["error"] = 1;
            json_response["desc"] = "JSON Decode Failed";
        }
        else {
            log_message("Decoded JSON payload successfully");

            // Full Config push
            log_message("Applying config update");

            // Overwrite in-memory config string
            strncpy(gv_config, 
                    gv_web_server.arg("plain").c_str(),
                    MAX_CONFIG_LEN);
            gv_config[MAX_CONFIG_LEN - 1] = '\0';

            // Set/over-ride hostname based on device
            update_config("name", gv_device.hostname, 0, 0);

            // Optional Config fields that we over-ride 
            // from existing values.
            //
            // The idea here is that we can push config but omit Wifi
            // creds. Even Zone is optional here
            if (json_post["zone"].isNull()) {
                update_config("zone", gv_device.zone, 0, 0);
            }
            if (json_post["wifi_ssid"].isNull()) {
                update_config("wifi_ssid", gv_device.wifi_ssid, 0, 0);
            }
            if (json_post["wifi_password"].isNull()) {
                update_config("wifi_password", gv_device.wifi_password, 0, 0);
            }

            // Set configured property now to 1 to signal a confirmed 
            // configured state and last arg of 1 sets this to save to 
            // EEPROM
            update_config("configured", NULL, 1, 1); 

            // Reboot ASAP
            gv_reboot_requested = 1;

            json_response["error"] = 0;
            json_response["desc"] = "Configured Device successfully";
        }
    }

    // Return response
    serializeJsonPretty(json_response, response_str);
    gv_web_server.send(200, "application/json", response_str);
}


// Function: start_wifi_sta_mode
// Configures the device as a WiFI client
void start_wifi_sta_mode()
{
    wifi_init();
    log_message("start_wifi_sta_mode(ssid:%s password:%s)", 
                gv_device.wifi_ssid,
                gv_device.wifi_password);

    // set state to track wifi down
    // will drive main loop to act accordingly
    TaskMan.set_run_state(RUN_STATE_WIFI_STA_DOWN);

    // WIFI
    // Turn off first as it better 
    // handles recovery after WIFI router
    // outages
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.hostname(gv_device.hostname);
    WiFi.begin(gv_device.wifi_ssid,
               gv_device.wifi_password);
}


// Function: start_sta_mode_services
// Run after we confirm WiFI up
// records IP and starts MDNS & DNS-SD
// This provides a safe reassertion of these services
// in case the IP address changed
void start_sta_mode_services()
{
    log_message("start_sta_mode_services()");

    sta_ip = WiFi.localIP();
    log_message("Connected.. IP:%d.%d.%d.%d",
                sta_ip[0],
                sta_ip[1],
                sta_ip[2],
                sta_ip[3]);

    if (gv_device.mdns_enabled) {
        // MDNS & DNS-SD using "JBHASD"
        log_message("Activating MDNS for hostname:%s", gv_device.hostname);
        if (!MDNS.begin(gv_device.hostname)) {
            log_message("Error setting up MDNS responder!");
        }
        else {
            log_message("Activating DNS-SD");
            MDNS.addService("JBHASD", "tcp", WEB_PORT);
        }
    }
    else {
        log_message("MDNS disabled!");
    }

    // Web server handlers
    gv_web_server.onNotFound(sta_handle_status);
    gv_web_server.on("/status", sta_handle_status);
    gv_web_server.on("/reboot", sta_handle_reboot);
    gv_web_server.on("/apmode", sta_handle_apmode);
    gv_web_server.on("/reset", sta_handle_reset);
    gv_web_server.on("/reconfigure", sta_handle_reconfigure);
    gv_web_server.on("/control", sta_handle_control);
    gv_web_server.on("/configure", sta_handle_configure);

    gv_web_server.begin();
    log_message("HTTP server started for client mode");

    start_ota();
    start_telnet();
}


// Function loop_task_webserver
// nudge funtion for ESP8266 web server 
// to handle pending events
void loop_task_webserver(void)
{
    gv_web_server.handleClient();
}

// Function loop_task_dns
// DNS nudge callback to keep DNS operational
void loop_task_dns(void)
{
    dns_server.processNextRequest();
}


// Function loop_task_mdns
// MDNS nudge callback to keep MDNS operational
void loop_task_mdns(void)
{
    MDNS.update();
}

// Function loop_task_wifi_down
// Checks is WiFI is down and acts accordingly
// by restarting STA mode
// Also changes state to indicate Wifi down
// We are also tracking the count of times we detect this down
// state
void loop_task_check_wifi_down(void)
{
    log_message("loop_task_check_wifi_down()");
    if (WiFi.status() != WL_CONNECTED) {
        log_message("WiFI is down");
        signal_wifi_restart_count++;
        TaskMan.set_run_state(RUN_STATE_WIFI_STA_DOWN);
        start_wifi_sta_mode();
    }
}


// Function loop_task_check_wifi_up
// Check is Wifi is up and transitions to a Wifi up 
// state, starting related services.
// Also monitors the actual calls and if it sees 360 calls
// (1 hour), it will reboot
void loop_task_check_wifi_up(void)
{
    uint8_t i;
    uint8_t status;
    static uint16_t check_count = 0;

    // This function gets called every 10 secs
    // So 360 calls is about 1 hour
    uint16_t max_checks_before_reboot = 360; 

    // Restart WiFI every 60 seconds if we continue
    // to remain disconnected
    // that equates to 6 calls
    uint16_t max_checks_before_wifi_restart = 6; 

    log_message("loop_task_check_wifi_up()");

    check_count++;

    status = WiFi.status();
    log_message("WiFI Status: %d", status);

    if (status == WL_CONNECTED) {
        log_message("WiFI is up");
        TaskMan.set_run_state(RUN_STATE_WIFI_STA_UP);

        // quick LED burst
        for (i = 1; i <= 50; i++) {
            toggle_status_led(50);
        }

        // status LED back to correct state
        restore_status_led_state();

        // reset connect count so that
        // we give a reconnect scenario the same max attempts
        // before reboot
        check_count = 0;

        // start additional services for sta
        // mode
        start_sta_mode_services();
    }
    else {
        log_message("WiFI is down");

        // check for max checks and restart
        if (check_count > max_checks_before_reboot) {
            log_message("Exceeded max checks of %d.. rebooting",
                        max_checks_before_reboot);
            gv_reboot_requested = 1;
        }
        else {
            // if not rebooting, then go for simple Wifi Restarts
            // every N calls
            if (check_count % max_checks_before_wifi_restart == 0) {
                log_message("Exceeded %d check interval.. restarting WiFI",
                            max_checks_before_wifi_restart);
                start_wifi_sta_mode();
            }
        }
    }
}

// Function loop_task_status_led
// Toggles status LED based on 
// call frequency 
void loop_task_status_led(void)
{
    // toggle LED with no delay
    // main loop driving the timing for this
    toggle_status_led(0); 
}


// Function loop_task_ap_reboot
// Reboots from AP Mode
// used to timeout the time left in this state
void loop_task_ap_reboot(void)
{
    log_message("Rebooting from AP Mode (timeout)");
    gv_reboot_requested = 1;
}

// Function loop_task_check_idle_status
// Checks for idle status based on calls to /status API
// Can root out inacessible network scenario even if WiFi is up
void loop_task_check_idle_status(void)
{
    uint32_t last_status_secs;
    uint32_t last_wifi_restart_secs;
    log_message("loop_task_check_idle_status()");

    // Determine #secs since last status call
    last_status_secs = (millis() - last_status) / 1000;
    last_wifi_restart_secs = (millis() - last_wifi_restart) / 1000;

    log_message("Configured Idle Status Periods (secs): Reboot:%d WiFi Restart:%d", 
                gv_device.idle_period_reboot, 
                gv_device.idle_period_wifi);
    log_message("Last /status API call was %d seconds ago", last_status_secs);
    log_message("WiFi last restart was %d seconds ago", last_wifi_restart_secs);

    // Reboot scenario
    if (gv_device.idle_period_reboot > 0 &&
        last_status_secs >= gv_device.idle_period_reboot) {
        log_message("Idle period >= %d (Rebooting)", 
                    gv_device.idle_period_reboot);
        gv_reboot_requested = 1;
        return;
    }

    // Restart WiFi scenario
    // bit more complex logic here
    // looking to qualify a WiFi restart if idle >= the defined period
    // but also need to ensure we have not already restarted the WiFi
    // so we are comparing against two intervals.. actual idle status time 
    // and the time since the last restart
    if (gv_device.idle_period_wifi > 0 &&
        last_status_secs >= gv_device.idle_period_wifi &&
        last_wifi_restart_secs >= gv_device.idle_period_wifi) {
        log_message("Idle period >= %d (Restarting WiFi)", 
                    gv_device.idle_period_wifi);
        last_wifi_restart = millis();
        status_wifi_restart_count++;
        start_wifi_sta_mode();
    }
}
