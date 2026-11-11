// FT3168 touch controller (I2C 0x38), via Arduino's Wire so it doesn't clash with the new IDF I2C driver.
#pragma once
#include <stdint.h>

void touch_begin();
bool touch_read(uint16_t* x, uint16_t* y);   // physical panel coords, true while a finger is down
