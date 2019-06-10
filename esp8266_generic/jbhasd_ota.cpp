#include "HandyTaskMan.h"
#include "jbhasd_types.h"

// Function: start_ota()
// Enables OTA service for firmware
// flashing OTA
void start_ota()
{
    static uint8_t already_setup = 0;

    // Only do this once
    // lost wifi conections will re-run start_wifi_sta_mode() so
    // we dont want this function called over and over
    if (already_setup) {
        log_message("OTA already started");
        return;
    }

    if (!gv_device.ota_enabled) {
        log_message("OTA mode not enabled.. returning");
        return;
    }

    // mark as setup, preventing multiple calls
    already_setup = 1;

    // Set hostname
    ArduinoOTA.setHostname(gv_device.hostname);

    // No authentication by default
    // ArduinoOTA.setPassword((const char *)"123");

    ArduinoOTA.onStart([]() {
                       log_message("OTA Start");

                       // Change mode to lock in OTA behaviour
                       TaskMan.set_run_state(RUN_STATE_WIFI_OTA);
                       });

    ArduinoOTA.onEnd([]() {
                     log_message("OTA End");
                     });

    ArduinoOTA.onProgress([](uint32_t progress,
                             uint32_t total) {
                          log_message("OTA Progress: %d/%d (%02u%%)", 
                                      progress, 
                                      total, 
                                      (progress / (total / 100)));
                          });

    ArduinoOTA.onError([](ota_error_t error) {
                       log_message("Error[%u]:", error);
                       if (error == OTA_AUTH_ERROR) log_message("Auth Failed");
                       else if (error == OTA_BEGIN_ERROR) log_message("Begin Failed");
                       else if (error == OTA_CONNECT_ERROR) log_message("Connect Failed");
                       else if (error == OTA_RECEIVE_ERROR) log_message("Receive Failed");
                       else if (error == OTA_END_ERROR) log_message("End Failed");
                       });

    ArduinoOTA.begin();

    // OTA (STA) every 1s
    TaskMan.add_task("OTA",
                     RUN_STATE_WIFI_STA_UP | RUN_STATE_WIFI_OTA,
                     1000,
                     loop_task_ota);

    log_message("OTA service started");
}

// Function loop_task_ota
// Handles loop activity for OTA
void loop_task_ota(void)
{
    ArduinoOTA.handle();
}


