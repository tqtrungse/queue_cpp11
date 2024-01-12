//
// Created by Trung Tran on 1/12/2024.
//

#ifndef QUEUE_CACHE_PADDED_H
#define QUEUE_CACHE_PADDED_H

#include <cstdint>

#if defined(ESP32)
    #define CACHE_PADDED 32 // https://esp32.com/viewtopic.php?t=8492#:~:text=The%20cache%20line%20size%20is%2032%20bytes.
#elif defined(ARDUINO_PORTENTA_H7_M7)
    #define CACHE_PADDED 32 // https://forum.arduino.cc/t/data-caching-for-multicore-shared-data/1046357/4
#else
    #define CACHE_PADDED sizeof(uint32_t)
#endif

#endif //QUEUE_CACHE_PADDED_H
