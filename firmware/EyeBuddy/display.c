// Panel bring-up copied from Waveshare's lcd_bsp.c (same init commands), minus LVGL.
#include "display.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lcd_config.h"
#include "read_lcd_id_bsp.h"

#define LCD_HOST SPI2_HOST
#define SH8601_ID 0x86

static const sh8601_lcd_init_cmd_t sh8601_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
};
static const sh8601_lcd_init_cmd_t co5300_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 80},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 1},
    {0x63, (uint8_t[]){0xFF}, 1, 1},
    {0x51, (uint8_t[]){0x00}, 1, 1},
    {0x29, (uint8_t[]){0x00}, 0, 10},
};

static esp_lcd_panel_io_handle_t io_handle;
static esp_lcd_panel_handle_t panel_handle;
static SemaphoreHandle_t idle_sem;   // "no colour transfer in flight"
static int x_offset;                 // CO5300 panels are shifted 6 px

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t* edata, void* ctx) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(idle_sem, &woken);
  return woken == pdTRUE;
}

void display_init(void) {
  uint8_t id = read_lcd_id();
  x_offset = (id == SH8601_ID) ? 0 : 6;

  const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
      EXAMPLE_PIN_NUM_LCD_PCLK, EXAMPLE_PIN_NUM_LCD_DATA0, EXAMPLE_PIN_NUM_LCD_DATA1, EXAMPLE_PIN_NUM_LCD_DATA2,
      EXAMPLE_PIN_NUM_LCD_DATA3, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
  ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

  idle_sem = xSemaphoreCreateBinary();
  xSemaphoreGive(idle_sem);
  const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_CS, on_trans_done, NULL);
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

  sh8601_vendor_config_t vendor_config = {
      .init_cmds = (id == SH8601_ID) ? sh8601_init_cmds : co5300_init_cmds,
      .init_cmds_size = (id == SH8601_ID) ? sizeof(sh8601_init_cmds) / sizeof(sh8601_init_cmds[0])
                                          : sizeof(co5300_init_cmds) / sizeof(co5300_init_cmds[0]),
      .flags = {.use_qspi_interface = 1},
  };
  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = LCD_BIT_PER_PIXEL,
      .vendor_config = &vendor_config,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_disp_on_off(panel_handle, true));
}

void display_brightness(uint8_t level) {
  int cmd = (0x02 << 24) | (0x51 << 8);   // QSPI "write command" opcode + brightness register
  esp_lcd_panel_io_tx_param(io_handle, cmd, &level, 1);
}

void display_draw(int x0, int y0, int x1, int y1, const uint16_t* pixels) {
  xSemaphoreTake(idle_sem, portMAX_DELAY);
  esp_lcd_panel_draw_bitmap(panel_handle, x0 + x_offset, y0, x1 + 1 + x_offset, y1 + 1, pixels);
}

void display_wait(void) {
  xSemaphoreTake(idle_sem, portMAX_DELAY);
  xSemaphoreGive(idle_sem);
}
