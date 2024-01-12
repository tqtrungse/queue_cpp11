//
// Created by Trung Tran on 1/12/2024.
//

#include <FreeRTOS.h>
#include <Arduino.h>
#include "queue.hpp"

t2::queue<int32_t> q{1024};
int32_t count{1};

TaskHandle_t core2Task;  // Task handle for the second core

void core2_function(void* parameter) {
    auto i = 1024;
    while (i > 0) {
        if (q.try_pop() != 0) {
            i--;
        }
    }
    vTaskDelete(&core2Task);
}

void setup() {
    Serial.begin(115200);

    // Create a task that will run on Core 2
    xTaskCreatePinnedToCore(
            core2_function,    // Function to be called
            "Core2Task",       // Task name
            10000,             // Stack size
            NULL,              // Parameter
            1,                 // Priority
            &core2Task,        // Task handle
            1                  // Core number (0 or 1)
    );
}

void loop() {
    // Your main loop code running on Core 1
    q.try_push(count);
    count++;
}