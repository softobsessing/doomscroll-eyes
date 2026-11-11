// Pins of the Waveshare ESP32-S3-Touch-AMOLED-1.43 (from Waveshare's demo).
#ifndef LCD_CONFIG_H
#define LCD_CONFIG_H

#define EXAMPLE_LCD_H_RES              466
#define EXAMPLE_LCD_V_RES              466
#define LCD_BIT_PER_PIXEL              16

#define EXAMPLE_PIN_NUM_LCD_CS            9
#define EXAMPLE_PIN_NUM_LCD_PCLK          10
#define EXAMPLE_PIN_NUM_LCD_DATA0         11
#define EXAMPLE_PIN_NUM_LCD_DATA1         12
#define EXAMPLE_PIN_NUM_LCD_DATA2         13
#define EXAMPLE_PIN_NUM_LCD_DATA3         14
#define EXAMPLE_PIN_NUM_LCD_RST           21

#define I2C_ADDR_FT3168 0x38
#define EXAMPLE_PIN_NUM_TOUCH_SCL 48
#define EXAMPLE_PIN_NUM_TOUCH_SDA 47

#endif
