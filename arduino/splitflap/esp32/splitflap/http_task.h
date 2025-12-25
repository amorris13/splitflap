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
#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "../core/arduino_json.h"
#include "../core/logger.h"
#include "../core/splitflap_task.h"
#include "../core/task.h"

#include "display_task.h"
#include "wifi_manager.h"
#include "message_provider.h"

class HTTPTask : public Task<HTTPTask> {
    friend class Task<HTTPTask>; // Allow base Task to invoke protected run()

    public:
        HTTPTask(SplitflapTask& splitflap_task, DisplayTask& display_task, WiFiManager& wifi_manager, Logger& logger, const uint8_t task_core);
        ~HTTPTask();

    protected:
        void run();

    private:
        SplitflapTask& splitflap_task_;
        DisplayTask& display_task_;
        WiFiManager& wifi_manager_;
        Logger& logger_;

        std::vector<MessageProvider*> message_providers_;

        uint32_t http_last_request_time_ = 0;
        uint32_t http_last_success_time_ = 0;

        std::vector<String> messages_ = {};
        uint8_t current_message_index_ = 0;
        uint32_t last_message_change_time_ = 0;
};
