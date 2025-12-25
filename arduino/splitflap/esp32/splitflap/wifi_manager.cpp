/*
   Copyright 2021 Scott Bezek and the splitflap contributors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "wifi_manager.h"
#include "secrets.h"

#include <lwip/apps/sntp.h>
#include <time.h>

// How long to wait for a WiFi connection before trying the next network
#define WIFI_TIMEOUT_MILLIS 15000

WiFiManager::WiFiManager(DisplayTask& display_task, Logger& logger) :
    display_task_(display_task),
    logger_(logger) {
}

bool WiFiManager::connect() {
    char buf[256];

    for (const auto& config : wifi_configs) {
        log_d("Trying to connect to %s", config.ssid);
        display_task_.setMessage(1, "Connecting to " + String(config.ssid));
        WiFi.begin(config.ssid, config.password);

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > WIFI_TIMEOUT_MILLIS) {
                log_d("Connection timed out.");
                break;
            }
            delay(1000);
            log_d("Waiting for connection...");
        }

        if (WiFi.status() == WL_CONNECTED) {
            snprintf(buf, sizeof(buf), "Connected to network %s", config.ssid);
            logger_.log(buf);
            display_task_.setMessage(1, "Connected to " + String(config.ssid));

            // Sync SNTP
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            char server[] = "time.nist.gov";
            sntp_setservername(0, server);
            sntp_init();

            log_d("Waiting for NTP time sync...");
            snprintf(buf, sizeof(buf), "Syncing NTP time via %s...", server);
            display_task_.setMessage(1, String(buf));
            time_t now;
            while (time(&now), now < 1625099485) {
                delay(1000);
            }

            strftime(buf, sizeof(buf), "Got time: %Y-%m-%d %H:%M:%S", localtime(&now));
            logger_.log(buf);

            return true;
        }
    }

    logger_.log("Could not connect to any WiFi network.");
    display_task_.setMessage(1, "No WiFi connection.");
    return false;
}
