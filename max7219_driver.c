#include "max7219_driver.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/idf_additions.h"
#include "portmacro.h"

esp_err_t max7219_init(max7219_t *drv, gpio_num_t din, gpio_num_t clk,
                       gpio_num_t cs, spi_host_device_t host,
                       uint8_t dsp_count) {
  if (!drv)
    return ESP_ERR_INVALID_ARG;

  drv->host = host;
  drv->dsp_count = dsp_count;
  drv->fb = calloc(dsp_count, sizeof *drv->fb);
  if (drv->fb == NULL) {
    ESP_LOGE(max7219_tag, "Failed to allocate frame buffer memory, exiting");
    return ESP_ERR_NO_MEM;
  }

  drv->trans_data = calloc(drv->dsp_count, sizeof(uint16_t));
  if (drv->trans_data == NULL) {
    free(drv->fb);
    ESP_LOGE(max7219_tag,
             "Failed to allocate memory for transfer data, exiting...");
    return ESP_ERR_NO_MEM;
  }

  // init pins
  gpio_reset_pin(din);
  gpio_reset_pin(clk);
  gpio_reset_pin(cs);

  gpio_set_direction(din, GPIO_MODE_OUTPUT);
  gpio_set_direction(clk, GPIO_MODE_OUTPUT);
  gpio_set_direction(cs, GPIO_MODE_OUTPUT);

  gpio_set_level(din, 0);
  gpio_set_level(clk, 0);
  gpio_set_level(cs, 1);

  drv->cs_pin = cs;

  esp_err_t err_code = ESP_OK;

  // bus bus setup
  spi_bus_config_t bus_conf = {
      .mosi_io_num = din,
      .miso_io_num = -1,
      .sclk_io_num = clk,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };
  err_code = spi_bus_initialize(drv->host, &bus_conf, SPI_DMA_DISABLED);
  if (err_code != ESP_OK) {
    free(drv->fb);
    free(drv->trans_data);
    return err_code;
  }

  // device config
  spi_device_interface_config_t dev_conf = {.clock_speed_hz = 1000000,
                                            .queue_size = 1,
                                            .spics_io_num = -1,
                                            .mode = 0};

  err_code = spi_bus_add_device(host, &dev_conf, &drv->spi_handle);
  if (err_code != ESP_OK) {
    free(drv->fb);
    free(drv->trans_data);
    spi_bus_free(drv->host);
    return err_code;
  }

  return ESP_OK;
}

void max7219_cleanup(max7219_t *drv) {
  if (!drv)
    return;

  if (drv->fb != NULL)
    free(drv->fb);
  if (drv->trans_data != NULL)
    free(drv->trans_data);

  esp_err_t err_code = spi_bus_remove_device(drv->spi_handle);
  if (err_code != ESP_OK) {
    ESP_LOGE(max7219_tag, "Error (%s) while removing device from bus",
             esp_err_to_name(err_code));
    return;
  }

  err_code = spi_bus_free(drv->host);
  if (err_code != ESP_OK) {
    ESP_LOGE(max7219_tag, "Error (%s) while freeing bus",
             esp_err_to_name(err_code));
  }
}

esp_err_t max7219_transmit(max7219_t *drv) {
  if (!drv)
    return ESP_ERR_INVALID_ARG;

  spi_transaction_t trans = {.length = 16 * drv->dsp_count,
                             .tx_buffer = drv->trans_data};

  gpio_set_level(drv->cs_pin, 0);
  esp_err_t err_code = spi_device_transmit(drv->spi_handle, &trans);
  gpio_set_level(drv->cs_pin, 1);
  if (err_code != ESP_OK)
    return err_code;

  return ESP_OK;
}

esp_err_t max7219_write(max7219_t *drv, uint8_t data, max7219_regs addr) {
  if (!drv)
    return ESP_ERR_INVALID_ARG;

  uint16_t payload = ((uint16_t)data << 8) | addr;
  spi_transaction_t trans = {.length = 16, .tx_buffer = &payload};

  return spi_device_transmit(drv->spi_handle, &trans);
}

esp_err_t max7219_write_all(max7219_t *drv, uint8_t data, max7219_regs addr) {
  if (!drv)
    return ESP_ERR_INVALID_ARG;
  for (int i = 0; i < drv->dsp_count; i++) {
    drv->trans_data[i] = ((uint16_t)data << 8) | addr;
  }

  return max7219_transmit(drv);
}

esp_err_t max7219_write_dsp(max7219_t *drv, uint8_t data, max7219_regs addr,
                            uint8_t dsp) {
  if (!drv || dsp >= drv->dsp_count)
    return ESP_ERR_INVALID_ARG;

  memset(drv->trans_data, 0, sizeof(uint16_t) * drv->dsp_count);
  drv->trans_data[dsp] = ((uint16_t)data << 8) | addr;

  return max7219_transmit(drv);
}

esp_err_t max7219_clear_displays(max7219_t *drv) {
  if (!drv)
    return ESP_ERR_INVALID_ARG;

  esp_err_t err_code = ESP_OK;
  for (int i = 0; i < DSP_HEIGHT; i++) {
    err_code = max7219_write_all(drv, 0, MAX7219_REG_ROW_0 + i);
    if (err_code != ESP_OK)
      return err_code;
  }

  return ESP_OK;
}

esp_err_t max7219_set_default(max7219_t *drv) {
  if (!drv)
    return ESP_ERR_INVALID_ARG;

  // header files contains explanations of each
  RETURN_ON_ERR(max7219_write_all(drv, 0x00, MAX7219_REG_SHUTDOWN));
  RETURN_ON_ERR(max7219_write_all(drv, 0x00, MAX7219_REG_DECODE_MODE));
  RETURN_ON_ERR(max7219_write_all(drv, 0x01, MAX7219_REG_INTENSITY));
  RETURN_ON_ERR(max7219_write_all(drv, 0x07, MAX7219_REG_SCAN_LIMIT));
  RETURN_ON_ERR(max7219_write_all(drv, 0x00, MAX7219_REG_DISPLAY_TEST));
  RETURN_ON_ERR(max7219_write_all(drv, 0x01, MAX7219_REG_SHUTDOWN));

  return ESP_OK;
}

esp_err_t max7219_flush_fb(max7219_t *drv) {
  if (!drv)
    return ESP_ERR_INVALID_ARG;

  esp_err_t err_code = ESP_OK;
  for (size_t i = 0; i < DSP_HEIGHT; i++) {
    for (size_t j = 0; j < drv->dsp_count; j++) {
      drv->trans_data[j] =
          ((uint16_t)drv->fb[j][i] << 8) | (MAX7219_REG_ROW_0 + i);
    }

    err_code = max7219_transmit(drv);
    if (err_code != ESP_OK)
      return err_code;
  }
  return ESP_OK;
}

esp_err_t fb_set_pixel(max7219_t *drv, uint8_t x, uint8_t y, uint8_t state) {
  if (!drv)
    return ESP_ERR_INVALID_ARG;

  if (x > DSP_WIDTH * drv->dsp_count - 1 || y >= 8)
    return ESP_ERR_INVALID_ARG;

  uint8_t *row = &drv->fb[x / DSP_WIDTH][y];
  if (state == 1)
    *row |= ((uint8_t)1 << (7 - (x % DSP_WIDTH)));
  else if (state == 0)
    *row &= ~((uint8_t)1 << (7 - (x % DSP_WIDTH)));
  else
    return ESP_ERR_INVALID_ARG;

  return ESP_OK;
}

esp_err_t get_bitmap(const font_t *font, unsigned char c, const uint8_t **res) {
  if (font == NULL || res == NULL)
    return ESP_ERR_INVALID_ARG;
  *res = NULL;
  if (c < font->first_char || c > font->last_char)
    return ESP_ERR_INVALID_ARG;

  *res = font->font_array + ((uint8_t)c - font->first_char) * font->width;
  return ESP_OK;
}

esp_err_t fb_draw_char(max7219_t *drv, const font_t *font, unsigned char c,
                       uint8_t x_pos) {
  if (!drv || !font)
    return ESP_ERR_INVALID_ARG;
  if (c < font->first_char || c > font->last_char)
    return ESP_ERR_INVALID_ARG;

  if (x_pos + 4 >= drv->dsp_count * DSP_WIDTH)
    return ESP_ERR_INVALID_ARG;

  esp_err_t err_code = ESP_OK;
  const uint8_t *bitmap = NULL;

  err_code = get_bitmap(font, c, &bitmap);
  if (err_code != ESP_OK)
    return err_code;

  for (size_t column = 0; column < font->width; column++) {
    for (size_t row = 0; row < DSP_HEIGHT - 1;
         row++) { // -1 to skip meaningless bit
      err_code = fb_set_pixel(drv, column + x_pos,
                              row + 1, // writte without space at the bottom
                              !!(bitmap[column] & ((uint8_t)1 << (row))));
      if (err_code != ESP_OK) {
        return err_code;
      }
    }
  }

  return ESP_OK;
}

esp_err_t fb_draw_text(max7219_t *drv, const font_t *font, char *text,
                       uint8_t x_pos, uint8_t distance) {
  if (!drv || !font || !text)
    return ESP_ERR_INVALID_ARG;
  if (strlen(text) < 1)
    return ESP_ERR_INVALID_ARG;

  // text can fit on display
  // s_len * letter_width + s_len * (letters_distance - 1)
  // -1 as there is no need for break after last letter
  uint8_t len = strlen(text) * (font->width + distance) - 1;
  if (len + x_pos >= drv->dsp_count * 8)
    return ESP_ERR_INVALID_ARG;

  esp_err_t err_code = ESP_OK;
  while (*text != '\0') {
    err_code = fb_draw_char(drv, font, *text, x_pos);
    if (err_code != ESP_OK)
      return err_code;

    text += 1;
    x_pos += font->width + distance;
  }

  return ESP_OK;
}

esp_err_t fb_draw_text_center(max7219_t *drv, const font_t *font, char *text,
                              uint8_t distance) {
  if (!drv || !font || !text)
    return ESP_ERR_INVALID_ARG;

  uint8_t space_left =
      drv->dsp_count * 8 - (strlen(text) * (font->width + distance) - 1);

  ESP_LOGI(max7219_tag, "%u cols left for text %s and distance %u", space_left,
           text, distance);
  return fb_draw_text(drv, font, text, space_left / 2, distance);
}

void app_main(void) {
  max7219_t driver = {0};
  esp_err_t err_code = max7219_init(&driver, 2, 16, 4, SPI2_HOST, 4);
  if (err_code != ESP_OK) {
    printf("Error (%s) while initializing max7219_driver\n",
           esp_err_to_name(err_code));
    while (1)
      vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  ESP_LOGI(max7219_tag, "driver initialized");

  if (max7219_set_default(&driver) != ESP_OK)
    ESP_LOGE(max7219_tag, "Error on setting default settings");

  if (max7219_clear_displays(&driver) != ESP_OK)
    ESP_LOGE(max7219_tag, "Error while clearing screen");

  // clear fb
  memset(driver.fb, 0, sizeof(driver.fb[0][0]) * driver.dsp_count * 8);

  if (fb_draw_text_center(&driver, &font_def, "TEST5", 1) != ESP_OK)
    ESP_LOGE(max7219_tag, "Error while writting text");

  if (max7219_flush_fb(&driver) != ESP_OK)
    ESP_LOGE(max7219_tag, "Error while flushing fb");

  vTaskDelay(10000 / portTICK_PERIOD_MS);
  ESP_LOGI(max7219_tag, "cleaning up now...");

  max7219_cleanup(&driver);
  esp_restart();
}
