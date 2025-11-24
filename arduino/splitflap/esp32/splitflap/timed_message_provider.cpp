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
#include "timed_message_provider.h"

static const SpecialMessage special_messages[] = {
    {1769107200, 60, {"HELLO  "}},
    {1769107260, 120, {"WORLD  ", "FOOBAR "}},
};

TimedMessageProvider::TimedMessageProvider() {
}

FetchResult TimedMessageProvider::fetchData() {
    time_t now_time;
    time(&now_time);

    for (const auto& sm : special_messages) {
        if (now_time >= sm.start_time && now_time < sm.start_time + sm.duration_seconds) {
            if (current_messages_ != sm.messages) {
                current_messages_ = sm.messages;
                return FetchResult::UPDATE;
            }
            return FetchResult::NO_CHANGE;
        }
    }
    if (!current_messages_.empty()) {
        current_messages_.clear();
        return FetchResult::UPDATE;
    }
    return FetchResult::NO_CHANGE;
}

const std::vector<String>& TimedMessageProvider::getMessages() {
    return current_messages_;
}
