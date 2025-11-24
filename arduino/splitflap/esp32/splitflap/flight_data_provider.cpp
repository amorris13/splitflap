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
#include "flight_data_provider.h"

#include <HTTPClient.h>
#include <time.h>
#include <Regexp.h>

#include "../core/arduino_json.h"
#include "geo_distance.h"

// 68 Duncan St Maroubra
#define CURRENT_LAT -33.9429
#define CURRENT_LNG 151.2562

#define MAX_DISTANCE_KM 2.5
#define MAX_ALT_FT 7000

#define LOW_ALT_FT 1000
#define LOW_MAX_DISTANCE_KM 1

FlightDataProvider::FlightDataProvider(DisplayTask& display_task, Logger& logger) :
    display_task_(display_task),
    logger_(logger) {
}

FetchResult FlightDataProvider::fetchData()
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
    return candidate_distance < current_distance;
}

FetchResult FlightDataProvider::handleData(DynamicJsonDocument json)
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
        if (messages_.empty()) {
            return FetchResult::NO_CHANGE;
        } else {
            messages_.clear();
            return FetchResult::UPDATE;
        }
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

void FlightDataProvider::setMessages(String callsign)
{
    uint32_t start = millis();
    HTTPClient http;

    // Construct the http request
    http.begin("https://api.adsbdb.com/v0/callsign/" + callsign);
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

const std::vector<String>& FlightDataProvider::getMessages() {
    return messages_;
}
