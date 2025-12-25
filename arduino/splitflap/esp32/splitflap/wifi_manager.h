/*
   Copyright 2021 Scott Bezek and the splitflap contributors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law of an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#pragma once

#include <Arduino.hh>
#include <WiFi.h>

#include "../core/logger.h"
#include "display_task.h"

class WiFiManager {
    public:
        WiFiManager(DisplayTask& display_task, Logger& logger);
        bool connect();

    private:
        DisplayTask& display_task_;
        Logger& logger_;
};
