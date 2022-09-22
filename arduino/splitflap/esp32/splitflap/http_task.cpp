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
#if HTTP
#include "http_task.h"

#include <HTTPClient.h>
#include <lwip/apps/sntp.h>
#include <json11.hpp>
#include <time.h>

#include "geo_distance.h"
#include "icao_to_iata.h"
#include "secrets.h"

using namespace json11;

// About this example:
// - Fetches current weather data for an area in San Francisco (updating infrequently)
// - Cycles between showing the temperature and the wind speed on the split-flaps (cycles frequently)
//
// Make sure to set up secrets.h - see secrets.h.example for more.
//
// What this example demonstrates:
// - a simple JSON GET request (see fetchData)
// - json response parsing using json11 (see handleData)
// - cycling through messages at a different interval than data is loaded (see run)

// Update data every 5 seconds
#define REQUEST_INTERVAL_MILLIS (5 * 1000)

// Cycle the message that's showing more frequently, every 30 seconds (exaggerated for example purposes)
#define MESSAGE_CYCLE_INTERVAL_MILLIS (30 * 1000)

// Don't show stale data if it's been too long since successful data load
#define STALE_TIME_MILLIS (30 * 1000)

// Timezone for local time strings; this is Australia/Sydney. See https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define TIMEZONE "AEST-10AEDT,M10.1.0,M4.1.0/3"

// 68 Duncan St Maroubra
#define CURRENT_LAT -33.9429
#define CURRENT_LNG 151.2562

#define MAX_DISTANCE_KM 2
#define MAX_ALT_FT 5000

bool HTTPTask::fetchData()
{
    char buf[200];
    uint32_t start = millis();
    HTTPClient http;

    // Construct the http request
    http.begin("http://raspberrypi:8080/data/aircraft.json");

    // Send the request as a GET
    logger_.log("Sending request");
    int http_code = http.GET();

    snprintf(buf, sizeof(buf), "Finished request in %lu millis.", millis() - start);
    logger_.log(buf);
    if (http_code > 0) {
        String data = http.getString();
        http.end();

        snprintf(buf, sizeof(buf), "Response code: %d Data length: %d", http_code, data.length());
        logger_.log(buf);

        std::string err;
        Json json = Json::parse(data.c_str(), err);

        if (err.empty()) {
            return handleData(json);
        } else {
            snprintf(buf, sizeof(buf), "Error parsing response! %s", err.c_str());
            logger_.log(buf);
            return false;
        }
    } else {
        snprintf(buf, sizeof(buf), "Error on HTTP request (%d): %s", http_code, http.errorToString(http_code).c_str());
        logger_.log(buf);
        http.end();
        return false;
    }
}

bool HTTPTask::handleData(Json json) {
    char buf[200];

    // Show the data fetch time on the LCD
    time_t now;
    time(&now);
    strftime(buf, sizeof(buf), "Data: %Y-%m-%d %H:%M:%S", localtime(&now));
    display_task_.setMessage(0, String(buf));

    // Extract data from the json response. You could use ArduinoJson, but I find json11 to be much
    // easier to use albeit not optimized for a microcontroller.

    // Validate json structure and extract data:
    auto aircrafts = json["aircraft"];
    if (!aircrafts.is_array())
    {
        logger_.log("Parse error: aircraft");
        return false;
    }
    auto aircraft_array = aircrafts.array_items();

    double nearest_dist = 10000;
    Json nearest_aircraft;

    display_task_.setMessage(2, String("Num planes: ") + String(aircraft_array.size(), 10));

    for (uint8_t i = 0; i < aircraft_array.size(); i++)
    {
        Json aircraft = aircraft_array[i];

        if (aircraft["flight"].is_null())
        {
            snprintf(buf, sizeof(buf), "Plane %s has no flight number.", aircraft["hex"].string_value().c_str());
            logger_.log(buf);
            continue;
        }

        std::string callsign = aircraft["flight"].string_value();

        double lon = aircraft["lon"].number_value();
        double lat = aircraft["lat"].number_value();
        double dist = great_circle_distance(CURRENT_LAT, CURRENT_LNG, lat, lon);

        if (dist > MAX_DISTANCE_KM)
        {
            snprintf(buf, sizeof(buf), "Plane %s too far away %fkm.", callsign.c_str(), dist);
            logger_.log(buf);
            continue;
        }

        double alt = aircraft["alt_geom"].number_value();
        if (alt > MAX_ALT_FT)
        {
            snprintf(buf, sizeof(buf), "Plane %s too high %fft.", callsign.c_str(), alt);
            logger_.log(buf);
            continue;
        }

        if (dist < nearest_dist)
        {
            nearest_dist = dist;
            nearest_aircraft = aircraft;
        }
    }

    if (nearest_dist > MAX_DISTANCE_KM)
    {
        logger_.log("No nearby planes");
        return false;
    }

    snprintf(buf, sizeof(buf), "Nearest plane %s %s at %f", nearest_aircraft["hex"].string_value().c_str(), nearest_aircraft["flight"].string_value().c_str(), nearest_dist);
    logger_.log(buf);

    std::string iataFlight = icaoToIataFlight(nearest_aircraft["flight"].string_value());
    // TODO: Request further information about the flight (origin/destination, IATA flight number, aircraft).

    // Construct the messages to display
    messages_.clear();

    snprintf(buf, sizeof(buf), "%s", iataFlight.c_str());
    messages_.push_back(String(buf));

    return true;
}

HTTPTask::HTTPTask(SplitflapTask& splitflap_task, DisplayTask& display_task, Logger& logger, const uint8_t task_core) :
        Task("HTTP", 8192, 1, task_core),
        splitflap_task_(splitflap_task),
        display_task_(display_task),
        logger_(logger),
        wifi_client_() {
}

void HTTPTask::connectWifi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    char buf[256];

    logger_.log("Establishing connection to WiFi..");
    snprintf(buf, sizeof(buf), "Wifi connecting to %s", WIFI_SSID);
    display_task_.setMessage(1, String(buf));
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
    }

    snprintf(buf, sizeof(buf), "Connected to network %s", WIFI_SSID);
    logger_.log(buf);

    // Sync SNTP
    sntp_setoperatingmode(SNTP_OPMODE_POLL);

    char server[] = "time.nist.gov"; // sntp_setservername takes a non-const char*, so use a non-const variable to avoid warning
    sntp_setservername(0, server);
    sntp_init();

    logger_.log("Waiting for NTP time sync...");
    snprintf(buf, sizeof(buf), "Syncing NTP time via %s...", server);
    display_task_.setMessage(1, String(buf));
    time_t now;
    while (time(&now),now < 1625099485) {
        delay(1000);
    }

    setenv("TZ", TIMEZONE, 1);
    tzset();
    strftime(buf, sizeof(buf), "Got time: %Y-%m-%d %H:%M:%S", localtime(&now));
    logger_.log(buf);
}

void HTTPTask::run() {
    char buf[max(NUM_MODULES + 1, 200)];

    connectWifi();

    bool stale = false;
    while(1) {
        long now = millis();

        bool update = false;
        if (http_last_request_time_ == 0 || now - http_last_request_time_ > REQUEST_INTERVAL_MILLIS) {
            if (fetchData()) {
                http_last_success_time_ = millis();
                stale = false;
                update = true;
            }
            http_last_request_time_ = millis();
        }

        if (!stale && http_last_success_time_ > 0 && millis() - http_last_success_time_ > STALE_TIME_MILLIS) {
            stale = true;
            messages_.clear();
            messages_.push_back("      ");
            update = true;
        }

        if (update || now - last_message_change_time_ > MESSAGE_CYCLE_INTERVAL_MILLIS) {
            if (current_message_index_ >= messages_.size()) {
                current_message_index_ = 0;
            }

            if (messages_.size() > 0) {
                String message = messages_[current_message_index_].c_str();

                snprintf(buf, sizeof(buf), "Cycling to next message: %s", message.c_str());
                logger_.log(buf);

                // Pad message for display
                size_t len = strlcpy(buf, message.c_str(), sizeof(buf));
                memset(buf + len, ' ', sizeof(buf) - len);

                splitflap_task_.showString(buf, NUM_MODULES, false);
            }

            current_message_index_++;
            last_message_change_time_ = millis();
        }

        String wifi_status;
        switch (WiFi.status()) {
            case WL_IDLE_STATUS:
                wifi_status = "Idle";
                break;
            case WL_NO_SSID_AVAIL:
                wifi_status = "No SSID";
                break;
            case WL_CONNECTED:
                wifi_status = String(WIFI_SSID) + " " + WiFi.localIP().toString();
                break;
            case WL_CONNECT_FAILED:
                wifi_status = "Connection failed";
                break;
            case WL_CONNECTION_LOST:
                wifi_status = "Connection lost";
                break;
            case WL_DISCONNECTED:
                wifi_status = "Disconnected";
                break;
            default:
                wifi_status = "Unknown";
                break;
        }
        display_task_.setMessage(1, String("Wifi: ") + wifi_status);

        delay(1000);
    }
}
#endif
