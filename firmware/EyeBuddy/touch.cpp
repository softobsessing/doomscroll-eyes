#include "touch.h"
#include <Arduino.h>
#include <Wire.h>
#include "lcd_config.h"

static bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(I2C_ADDR_FT3168);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)I2C_ADDR_FT3168, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

void touch_begin() {
  Wire.begin(EXAMPLE_PIN_NUM_TOUCH_SDA, EXAMPLE_PIN_NUM_TOUCH_SCL, 300000);
  Wire.beginTransmission(I2C_ADDR_FT3168);   // register 0 = 0: normal operating mode
  Wire.write(0x00);
  Wire.write(0x00);
  Wire.endTransmission();
}

bool touch_read(uint16_t* x, uint16_t* y) {
  uint8_t b[5];   // 0x02 = number of touches, 0x03..0x06 = X/Y of the first touch
  if (!readRegs(0x02, b, 5) || (b[0] & 0x0F) == 0) return false;
  *x = ((b[1] & 0x0F) << 8) | b[2];
  *y = ((b[3] & 0x0F) << 8) | b[4];
  if (*x >= EXAMPLE_LCD_H_RES) *x = EXAMPLE_LCD_H_RES - 1;
  if (*y >= EXAMPLE_LCD_V_RES) *y = EXAMPLE_LCD_V_RES - 1;
  return true;
}
