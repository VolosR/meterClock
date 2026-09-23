#include <stdio.h>
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "lcd_touch.h"
#include "esp_log.h"
#include "./src/port_bsp/i2c_bsp.h"

LcdTouchPanel::LcdTouchPanel(I2cMasterBus &i2cbus, int dev_addr, int touch_rst_pin, int touch_int_pin)
  : i2cbus_(i2cbus) {
  i2c_master_bus_handle_t BusHandle = i2cbus_.Get_I2cBusHandle();
  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.scl_speed_hz = 400000;
  dev_cfg.device_address = dev_addr;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(BusHandle, &dev_cfg, &touch_dev_handle));

  if (touch_rst_pin != GPIO_NUM_NC) {
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = (0x1ULL << touch_rst_pin);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;

    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    touch_rst_pin_ = touch_rst_pin;

    ResetTouch();
  }
}

LcdTouchPanel::~LcdTouchPanel() {
}

void LcdTouchPanel::ResetTouch() {
  if (touch_rst_pin_ != GPIO_NUM_NC) {
    gpio_set_level((gpio_num_t)touch_rst_pin_, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level((gpio_num_t)touch_rst_pin_, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level((gpio_num_t)touch_rst_pin_, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

uint8_t LcdTouchPanel::GetCoords(uint16_t *x, uint16_t *y) {
  uint8_t reg[2] = { 0xD0, 0x00 };
  uint8_t data[10] = { 0 };
  i2cbus_.i2c_master_write_read_dev(touch_dev_handle,reg,2,data,10);
  if (data[6] != 0xAB) {
    return false;
  }
  uint8_t points = data[5] & 0x7F;
  if (points == 0) {
    return false;
  }
  uint8_t *p = &data[0];
  uint8_t status = p[0] & 0x0F;
  if (status != 0x06) {
    return false;
  }
  *y = ((uint16_t)p[1] << 4) | (p[3] >> 4);
  *x = BSP_LCD_H_RES - ((uint16_t)p[2] << 4) | (p[3] & 0x0F);
  //Serial.printf("X=%d Y=%d\n", *x, *y);
  return true;
}
