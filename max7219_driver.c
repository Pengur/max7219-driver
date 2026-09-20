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
  if (err_code != ESP_OK)
    return err_code;
  gpio_set_level(drv->cs_pin, 1);

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

  for (int i = 0; i < DSP_HEIGHT; i++) {
    max7219_write_all(&driver, i, MAX7219_REG_ROW_0 + i);
  }

  gpio_set_level(driver.cs_pin, 1);
  gpio_set_level(driver.cs_pin, 0);

  vTaskDelay(10000 / portTICK_PERIOD_MS);
  ESP_LOGI(max7219_tag, "cleaning up now...");

  max7219_cleanup(&driver);
  esp_restart();
}
