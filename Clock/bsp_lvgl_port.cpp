#include "bsp_lvgl_port.h"
#include "lvgl.h"
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include "src/externLib/esp_lcd_sh8601.h"
#include "user_config.h"
#include "lcd_touch.h"
#include "./src/port_bsp/axp2101_bsp.h"

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;

static lv_disp_draw_buf_t draw_buf; /*LVGL 缓冲区*/
static lv_disp_drv_t disp_drv;
static lv_disp_t *disp = NULL;
static lv_indev_drv_t indev_drv;
static SemaphoreHandle_t lvgl_mux = NULL;
LcdTouchPanel *touch_dev = NULL;

static uint8_t brightness_ = 100;

#define LCD_BIT_PER_PIXEL (16)

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
  { 0x11, (uint8_t[]){ 0x00 }, 0, 600 },
  { 0xFE, (uint8_t[]){ 0x20 }, 1, 0 },
  { 0x19, (uint8_t[]){ 0x10 }, 1, 0 },
  { 0x1C, (uint8_t[]){ 0xA0 }, 1, 0 },
  { 0xFE, (uint8_t[]){ 0x00 }, 1, 0 },
  { 0xC4, (uint8_t[]){ 0x80 }, 1, 0 },
  { 0x3A, (uint8_t[]){ 0x55 }, 1, 0 },
  { 0x35, (uint8_t[]){ 0x00 }, 1, 0 },
  { 0x36, (uint8_t[]){ 0x30 }, 1, 0 },
  { 0x53, (uint8_t[]){ 0x20 }, 1, 0 },
  { 0x51, (uint8_t[]){ 0xFF }, 1, 0 },
  { 0x63, (uint8_t[]){ 0xFF }, 1, 0 },
  { 0x2A, (uint8_t[]){ 0x00, 0x00, 0x01, 0xDF }, 4, 0 },
  { 0x2B, (uint8_t[]){ 0x00, 0x00, 0x01, 0xDF }, 4, 0 },
  { 0x29, (uint8_t[]){ 0x00 }, 0, 100 },
};

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  //esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
  const int offsetx1 = area->x1;
  const int offsetx2 = area->x2;
  const int offsety1 = area->y1;
  const int offsety2 = area->y2;

  esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_p);
  lv_disp_flush_ready(disp);
}

void my_disp_rounder(struct _lv_disp_drv_t *disp_drv, lv_area_t *area) {
  uint16_t x1 = area->x1;
  uint16_t x2 = area->x2;

  uint16_t y1 = area->y1;
  uint16_t y2 = area->y2;

  area->x1 = (x1 >> 1) << 1;
  area->y1 = (y1 >> 1) << 1;

  area->x2 = ((x2 >> 1) << 1) + 1;
  area->y2 = ((y2 >> 1) << 1) + 1;
}

void increase_lvgl_tick(void *arg) {
  lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

bool bsp_lvgl_lock(int timeout_ms) {
  assert(lvgl_mux && "bsp_display_start must be called first");

  const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

void bsp_lvgl_unlock(void) {
  assert(lvgl_mux && "bsp_display_start must be called first");
  xSemaphoreGive(lvgl_mux);
}

static void lvgl_port_task(void *arg) {
  uint32_t task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
  while (1) {
    if (bsp_lvgl_lock(0)) {
      task_delay_ms = lv_timer_handler();
      bsp_lvgl_unlock();
    }
    if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS) {
      task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    } else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS) {
      task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
    }
    vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
  }
}

static void my_touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  uint16_t x = 0x00;
  uint16_t y = 0x00;
  if (touch_dev->GetCoords(&x, &y)) {
    data->point.x = x;
    data->point.y = y;
    if (data->point.x > BSP_LCD_H_RES)
      data->point.x = BSP_LCD_H_RES;
    if (data->point.y > BSP_LCD_V_RES)
      data->point.y = BSP_LCD_V_RES;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static int DisplayPort_DispReset(void) {
  Axp2101_SetAldo3(1);
  vTaskDelay(pdMS_TO_TICKS(100));
  Axp2101_SetAldo3(0);
  vTaskDelay(pdMS_TO_TICKS(100));
  Axp2101_SetAldo3(1);
  vTaskDelay(pdMS_TO_TICKS(100));
  return ESP_OK;
}

void bsp_lvgl_init(I2cMasterBus &i2cbus) {
  touch_dev = new LcdTouchPanel(i2cbus, 0x5A);
  lvgl_mux = xSemaphoreCreateMutex();
  assert(lvgl_mux);
  /*lcd init*/
  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = BSP_LCD_PCLK;
  buscfg.data0_io_num = BSP_LCD_DATA0;
  buscfg.data1_io_num = BSP_LCD_DATA1;
  buscfg.data2_io_num = BSP_LCD_DATA2;
  buscfg.data3_io_num = BSP_LCD_DATA3;
  buscfg.max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * LCD_BIT_PER_PIXEL / 8;
  ESP_ERROR_CHECK(spi_bus_initialize((spi_host_device_t)BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = BSP_LCD_CS;
  io_config.dc_gpio_num = -1;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 40 * 1000 * 1000;
  io_config.trans_queue_depth = 2;
  io_config.on_color_trans_done = NULL;
  io_config.user_ctx = NULL;
  io_config.lcd_cmd_bits = 32;
  io_config.lcd_param_bits = 8;
  io_config.flags.quad_mode = true;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, &io_handle));

  sh8601_vendor_config_t vendor_config = {};
  vendor_config.init_cmds = lcd_init_cmds;
  vendor_config.init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);
  vendor_config.flags.use_qspi_interface = 1;

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = BSP_LCD_RST;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.bits_per_pixel = LCD_BIT_PER_PIXEL;
  panel_config.vendor_config = &vendor_config;

  ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
  ESP_ERROR_CHECK(DisplayPort_DispReset());
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  uint16_t *buf = (uint16_t *)heap_caps_malloc(BSP_LCD_H_RES * 50 * 2, MALLOC_CAP_DMA);
  assert(buf);
  for (int i = 0; i < BSP_LCD_H_RES * 50; i++) {
    buf[i] = 0xffff;
  }
  esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, BSP_LCD_H_RES, 50, buf);
  /*LVGL INIT*/
  lv_init();
  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(BSP_LCD_H_RES * 50 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(BSP_LCD_H_RES * 50 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  assert(buf1);
  assert(buf2);
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, BSP_LCD_H_RES * 50);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = BSP_LCD_H_RES;
  disp_drv.ver_res = BSP_LCD_V_RES;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.rounder_cb = my_disp_rounder;
  disp_drv.draw_buf = &draw_buf;
  disp = lv_disp_drv_register(&disp_drv);
  assert(disp);

  /*touch init*/
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);


  esp_timer_create_args_t lvgl_tick_timer_args = {};
  lvgl_tick_timer_args.callback = &increase_lvgl_tick;
  lvgl_tick_timer_args.name = "lvgl_tick";
  esp_timer_handle_t lvgl_tick_timer = NULL;

  ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

  xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
}

void Lcd_SetBacklight(uint8_t brightness) {
  if (brightness > 100) brightness = 100;
  brightness_ = brightness;
  uint8_t bl_val = (uint8_t)((brightness_ * 255) / 100);
  uint32_t CMD = 0x51;
  CMD &= 0xFF;
  CMD <<= 8;
  CMD |= 0x02 << 24;
  esp_lcd_panel_io_tx_param(io_handle, CMD, &bl_val, 1);
}
