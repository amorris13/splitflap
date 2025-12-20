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

#include "../core/arduino_json.h"
#include "flight_data_provider.h"
#include "secrets.h"
#include "timed_message_provider.h"

// Update data every 5 seconds
#define REQUEST_INTERVAL_MILLIS (5 * 1000)

// Cycle the message that's showing more frequently, every 30 seconds
#define MESSAGE_CYCLE_INTERVAL_MILLIS (30 * 1000)

// Don't show stale data if it's been too long since successful data load
#define STALE_TIME_MILLIS (20 * 1000)

// Timezone for local time strings; this is Australia/Sydney. See
// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define TIMEZONE "AEST-10AEDT,M10.1.0,M4.1.0/3"

HTTPTask::HTTPTask(SplitflapTask& splitflap_task, DisplayTask& display_task,
                   Logger& logger, const uint8_t task_core)
    : Task("HTTP", 8192, 1, task_core),
      splitflap_task_(splitflap_task),
      display_task_(display_task),
      logger_(logger),
      wifi_client_() {
  message_providers_.push_back(new TimedMessageProvider(display_task, logger));
  message_providers_.push_back(new FlightDataProvider(display_task, logger));
}

HTTPTask::~HTTPTask() {
  for (MessageProvider* provider : message_providers_) {
    delete provider;
  }
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

  char server[] =
      "time.nist.gov";  // sntp_setservername takes a non-const char*, so use a
                        // non-const variable to avoid warning
  sntp_setservername(0, server);
  sntp_init();

  log_d("Waiting for NTP time sync...");
  snprintf(buf, sizeof(buf), "Syncing NTP time via %s...", server);
  display_task_.setMessage(1, String(buf));
  time_t now;
  while (time(&now), now < 1625099485) {
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
  while (1) {
    long now_millis = millis();

    bool update = false;

    // a. Fetch data
    if (http_last_request_time_ == 0 ||
        now_millis - http_last_request_time_ > REQUEST_INTERVAL_MILLIS) {
      for (MessageProvider* provider : message_providers_) {
        FetchResult fetchResult = provider->fetchData();
        http_last_request_time_ = millis();

        if (!provider->getMessages().empty()) {
          if (fetchResult == FetchResult::UPDATE) {
            messages_ = provider->getMessages();
            http_last_success_time_ = millis();
            stale = false;
            update = true;
            current_message_index_ = 0;
          } else if (fetchResult == FetchResult::NO_CHANGE) {
            http_last_success_time_ = millis();
            stale = false;
            update = false;
          }
          // This provider is active, so we don't want to fall back to the next
          // one
          break;
        }
      }
    }

    // b. Stale data check
    if (!stale && http_last_success_time_ > 0 &&
        millis() - http_last_success_time_ > STALE_TIME_MILLIS) {
      stale = true;
      messages_.clear();
      messages_.push_back("      ");
      update = true;
      current_message_index_ = 0;  // Point to the blank message
    }

    if (update || now_millis - last_message_change_time_ >
                      MESSAGE_CYCLE_INTERVAL_MILLIS) {
      if (current_message_index_ >= messages_.size()) {
        current_message_index_ = 0;
      }

      if (messages_.size() > 0) {
        String message = messages_[current_message_index_];

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
