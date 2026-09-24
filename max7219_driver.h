#ifndef MAX7219_DRIVER_H
#define MAX7219_DRIVER_H

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <string.h>

#include "font.h"

#define RETURN_ON_ERR(expr)                                                    \
  do {                                                                         \
    esp_err_t err_ = (expr);                                                   \
    if (err_ != ESP_OK)                                                        \
      return err_;                                                             \
  } while (0)

#define DSP_HEIGHT 8
#define DSP_WIDTH 8

char *max7219_tag = "MAX7219 DRIVER";

typedef enum {
  MAX7219_REG_NOOP = 0x0, // NOOP
  /*  DATA REGISTERS  */
  MAX7219_REG_ROW_0 = 0x1,
  MAX7219_REG_ROW_1 = 0x2,
  MAX7219_REG_ROW_2 = 0x3,
  MAX7219_REG_ROW_3 = 0x4,
  MAX7219_REG_ROW_4 = 0x5,
  MAX7219_REG_ROW_5 = 0x6,
  MAX7219_REG_ROW_6 = 0x7,
  MAX7219_REG_ROW_7 = 0x8,
  /*  END */
  MAX7219_REG_DECODE_MODE = 0x9, // decode mode (0x00 raw, 0xFF BCD)
  MAX7219_REG_INTENSITY = 0xA,   // brightness, 0x00-0xFF
  MAX7219_REG_SCAN_LIMIT = 0xB,  // scan limit, 0x07 should be fine
  MAX7219_REG_SHUTDOWN = 0xC, // shutdown mode, (0x00 shutdown, 0x01 normal op)
  MAX7219_REG_DISPLAY_TEST = 0xF // test mode (0x00 normal op, 0x01 test mode)
} max7219_regs;

typedef struct max7219_t {
  spi_host_device_t host; // driver manages host
  spi_device_handle_t spi_handle;
  gpio_num_t cs_pin;

  uint8_t dsp_count;
  uint8_t (*fb)[8];     // current frame buffer
  uint16_t *trans_data; // array of dsp_coutn elements
} max7219_t;

esp_err_t max7219_init(max7219_t *drv, gpio_num_t din, gpio_num_t clk,
                       gpio_num_t cs, spi_host_device_t host,
                       uint8_t dsp_count);

// void max7219_flush(max7219_t *drv) {
//   gpio_set_level(drv->cs_pin, 0);
//   gpio_set_level(drv->cs_pin, 1);
// }

// transmit data currently in hold
esp_err_t max7219_transmit(max7219_t *drv);

// prepare all displays for work
esp_err_t max7219_set_default(max7219_t *drv);

esp_err_t max7219_clear_displays(max7219_t *drv);

// single write data to given address, does not care where it lands, just sends
// doesnt flush
// endiannes funkyness goes in these
esp_err_t max7219_write(max7219_t *drv, uint8_t data, max7219_regs addr);

esp_err_t max7219_write_all(max7219_t *drv, uint8_t data, max7219_regs addr);
esp_err_t max7219_write_dsp(max7219_t *drv, uint8_t data, max7219_regs addr,
                            uint8_t dsp);

// display data currently in frame buffer
esp_err_t max7219_flush_fb(max7219_t *drv);

// free allocated memory, remove device from bus and free bus itself
void max7219_cleanup(max7219_t *drv);

esp_err_t fb_set_pixel(max7219_t *drv, uint8_t x, uint8_t y, uint8_t state);

// font related
esp_err_t get_bitmap(const font_t *font, unsigned char c, const uint8_t **res);

// starts drawing given character at x_pos column.
// returns invalid_arg in case char wont feet display, char not in font arr
esp_err_t fb_draw_char(max7219_t *drv, const font_t *font, unsigned char c,
                       uint8_t x_pos);

// calls fb_draw char with correct position, where distance is pixel between
// letters returns invalid_arg in case text does not fit
esp_err_t fb_draw_text(max7219_t *drv, const font_t *font, char *text,
                       uint8_t x_pos, uint8_t distance);

esp_err_t fb_draw_text_center(max7219_t *drv, const font_t *font, char *text,
                              uint8_t distance);

void app_main();

#endif
