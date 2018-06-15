//
// Created by Bradley Smith on 6/12/18.
//

#include "esp32_i2c.h"

void delay_ms(int ms) {
    if (ms >= 0) {
        vTaskDelay(ms / portTICK_PERIOD_MS);
    }
}