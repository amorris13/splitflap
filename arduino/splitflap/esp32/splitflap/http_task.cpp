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
#include <time.h>
#include <Regexp.h>

#include "../core/arduino_json.h"
#include "geo_distance.h"
#include "secrets.h"

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
#define MESSAGE_CYCLE_INTERVAL_MILLIS (15 * 1000)

// Don't show stale data if it's been too long since successful data load
#define STALE_TIME_MILLIS (20 * 1000)

// Timezone for local time strings; this is Australia/Sydney. See https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define TIMEZONE "AEST-10AEDT,M10.1.0,M4.1.0/3"

// 68 Duncan St Maroubra
#define CURRENT_LAT -33.9429
#define CURRENT_LNG 151.2562

#define MAX_DISTANCE_KM 2.5
#define MAX_ALT_FT 7000

#define LOW_ALT_FT 1000
#define LOW_MAX_DISTANCE_KM 1

FetchResult HTTPTask::fetchData()
{
    uint32_t start = millis();
    HTTPClient http;

    // Construct the http request
    http.begin("http://raspberrypi:8080/data/aircraft.json");
    http.useHTTP10(true);

    // Send the request as a GET
    log_d("Sending adsb request");
    int http_code = http.GET();

    log_d("Finished request in %lu millis.", millis() - start);
    if (http_code > 0)
    {
        log_d("Response code: %d Data length: %d", http_code, http.getSize());

        // The filter: it contains "true" for each value we want to keep
        StaticJsonDocument<200> filter;
        filter["aircraft"][0]["hex"] = true;
        filter["aircraft"][0]["flight"] = true;
        filter["aircraft"][0]["alt_geom"] = true;
        filter["aircraft"][0]["lat"] = true;
        filter["aircraft"][0]["lon"] = true;

        // Parse response
        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

        if (err)
        {
            http.end();
            log_d("Error parsing response! %s", err.c_str());
            return FetchResult::ERROR;
        }

        FetchResult result = handleData(doc);
        http.end();
        return result;
    }
    else
    {
        log_d("Error on HTTP request (%d): %s", http_code, http.errorToString(http_code).c_str());
        http.end();
        return FetchResult::ERROR;
    }
}

static bool isCommercialPlane(String callsign)
{
    MatchState ms;
    ms.Target(const_cast<char *>(callsign.c_str()));
    return ms.Match("%a%a%a[%d+]") == REGEXP_MATCHED;
}

static bool isBetterFlight(double current_distance, String current_callsign, double candidate_distance, String candidate_callsign)
{
    if (!current_callsign)
    {
        return true;
    }
    if (isCommercialPlane(current_callsign) != isCommercialPlane(candidate_callsign))
    {
        return isCommercialPlane(candidate_callsign) > isCommercialPlane(current_callsign);
    }
    return candidate_distance > current_distance;
}

FetchResult HTTPTask::handleData(DynamicJsonDocument json)
{
    char buf[200];

    // Show the data fetch time on the LCD
    time_t now;
    time(&now);
    strftime(buf, sizeof(buf), "Data: %Y-%m-%d %H:%M:%S", localtime(&now));
    display_task_.setMessage(0, String(buf));

    // Extract data from the json response. You could use ArduinoJson, but I find json11 to be much
    // easier to use albeit not optimized for a microcontroller.

    // Extract data:
    JsonArray aircrafts = json["aircraft"].as<JsonArray>();

    double nearest_dist = 10000;
    String nearest_callsign;
    String nearest_hex;

    display_task_.setMessage(2, String("Num planes: ") + String(aircrafts.size(), 10));
    for (JsonObject aircraft : aircrafts)
    {
        String hex = aircraft["hex"];
        if (!aircraft["flight"])
        {
            log_d("Plane %s has no flight number.", hex.c_str());
            continue;
        }

        String callsign = aircraft["flight"];

        double lon = aircraft["lon"];
        double lat = aircraft["lat"];
        double dist = great_circle_distance(CURRENT_LAT, CURRENT_LNG, lat, lon);
        double alt = aircraft["alt_geom"];

        if (dist > MAX_DISTANCE_KM)
        {
            log_d("Plane %s too far away %fkm.", callsign.c_str(), dist);
            continue;
        }

        if (alt > MAX_ALT_FT)
        {
            log_d("Plane %s too high %fft.", callsign.c_str(), alt);
            continue;
        }

        if (alt < LOW_ALT_FT && dist > LOW_MAX_DISTANCE_KM)
        {
            log_d("Plane %s flying low at %fft and too far away %fkm.", callsign.c_str(), alt, dist);
            continue;
        }

        if (isBetterFlight(nearest_dist, nearest_callsign, dist, callsign))
        {
            nearest_dist = dist;
            nearest_callsign = callsign;
            nearest_hex = hex;
        }
    }

    if (nearest_dist > MAX_DISTANCE_KM)
    {
        log_d("No nearby planes");
        return FetchResult::ERROR;
    }

    log_d("Nearest plane %s %s at %f", nearest_hex.c_str(), nearest_callsign.c_str(), nearest_dist);

    if (current_callsign && nearest_callsign == current_callsign)
    {
        log_d("Plane already detected");
        return FetchResult::NO_CHANGE;
    }
    current_callsign = nearest_callsign;

    messages_.clear();
    setMessages(nearest_callsign);
    return FetchResult::UPDATE;
}

void HTTPTask::setMessages(String callsign)
{
    uint32_t start = millis();
    HTTPClient http;

    // Construct the http request
    http.begin("https://api.adsbdb.com/beta/callsign/" + callsign);
    http.useHTTP10(true);

    // Send the request as a GET
    log_d("Sending route request");
    int http_code = http.GET();

    log_d("Finished request in %lu millis.", millis() - start);
    if (http_code > 0)
    {
        log_d("Response code: %d Data length: %d", http_code, http.getSize());

        // The filter: it contains "true" for each value we want to keep
        StaticJsonDocument<200> filter;
        filter["response"]["flightroute"]["callsign_iata"] = true;
        filter["response"]["flightroute"]["origin"]["iata_code"] = true;
        filter["response"]["flightroute"]["destination"]["iata_code"] = true;

        // Parse response
        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

        if (err)
        {
            http.end();
            log_d("Error parsing response! %s", err.c_str());
            return;
        }

        if (!doc["response"]["flightroute"])
        {
            log_d("No flight route for callsign %s", callsign.c_str());
            messages_.push_back(callsign);
            return;
        }

        String callsign_iata = doc["response"]["flightroute"]["callsign_iata"];
        messages_.push_back(callsign_iata ? callsign_iata : callsign);

        String origin = doc["response"]["flightroute"]["origin"]["iata_code"];
        String destination = doc["response"]["flightroute"]["destination"]["iata_code"];

        log_d("Flight route for callsign %s is %s%s", callsign.c_str(), origin.c_str(), destination.c_str());

        http.end();
        messages_.push_back(origin + destination);
    }
    else
    {
        log_d("Error on HTTP request (%d): %s", http_code, http.errorToString(http_code).c_str());
        http.end();
        return;
    }
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

    log_d("Establishing connection to WiFi..");
    snprintf(buf, sizeof(buf), "Wifi connecting to %s", WIFI_SSID);
    display_task_.setMessage(1, String(buf));
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
    }

    log_d("Connected to network %s", WIFI_SSID);

    // Sync SNTP
    sntp_setoperatingmode(SNTP_OPMODE_POLL);

    char server[] = "time.nist.gov"; // sntp_setservername takes a non-const char*, so use a non-const variable to avoid warning
    sntp_setservername(0, server);
    sntp_init();

    log_d("Waiting for NTP time sync...");
    snprintf(buf, sizeof(buf), "Syncing NTP time via %s...", server);
    display_task_.setMessage(1, String(buf));
    time_t now;
    while (time(&now),now < 1625099485) {
        delay(1000);
    }

    // setenv("TZ", TIMEZONE, 1);
    // tzset();
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
            FetchResult fetchResult = fetchData();
            if (fetchResult != FetchResult::ERROR)
            {
                http_last_success_time_ = millis();
                stale = false;
            }
            if (fetchResult == FetchResult::UPDATE)
            {
                update = true;
                current_message_index_ = 0;
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

                log_d("Cycling to next message: %s", message.c_str());

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
