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
#if MQTT
#include "mqtt_task.h"
#include "secrets.h"


MQTTTask::MQTTTask(SplitflapTask& splitflap_task, WiFiManager& wifi_manager, Logger& logger, const uint8_t task_core) :
        Task("MQTT", 8192, 1, task_core),
        splitflap_task_(splitflap_task),
        wifi_manager_(wifi_manager),
        logger_(logger),
        wifi_client_(),
        mqtt_client_(wifi_client_) {
    auto callback = [this](char *topic, byte *payload, unsigned int length) { mqttCallback(topic, payload, length); };
    mqtt_client_.setCallback(callback);
}

void MQTTTask::mqttCallback(char *topic, byte *payload, unsigned int length) {
    char buf[256];
    snprintf(buf, sizeof(buf), "Received mqtt callback for topic %s, length %u", topic, length);
    logger_.log(buf);
    splitflap_task_.showString((const char *)payload, length);
}

void MQTTTask::connectMQTT() {
    char buf[256];
    mqtt_client_.setServer(MQTT_SERVER, 1883);
    logger_.log("Attempting MQTT connection...");
    if (mqtt_client_.connect(HOSTNAME "-" MQTT_USER, MQTT_USER, MQTT_PASSWORD)) {
        logger_.log("MQTT connected");
        mqtt_client_.subscribe(MQTT_COMMAND_TOPIC);
    } else {
        snprintf(buf, sizeof(buf), "MQTT failed rc=%d will try again in 5 seconds", mqtt_client_.state());
        logger_.log(buf);
    }
}

void MQTTTask::run() {
    if (!wifi_manager_.connect()) {
        // Loop forever, we can't do anything without WiFi
        while(1) {
            delay(1000);
        }
    }
    connectMQTT();

    while(1) {
        long now = millis();
        if (!mqtt_client_.connected() && (now - mqtt_last_connect_time_) > 5000) {
            logger_.log("Reconnecting MQTT");
            mqtt_last_connect_time_ = now;
            connectMQTT();
        }
        mqtt_client_.loop();
        delay(1);
    }
}
#endif
