#ifndef BSP_LVGL_PORT_H
#define BSP_LVGL_PORT_H

#include "./src/port_bsp/i2c_bsp.h"

bool bsp_lvgl_lock(int timeout_ms);
void bsp_lvgl_unlock(void);
void bsp_lvgl_init(I2cMasterBus& i2cbus);

void Lcd_SetBacklight(uint8_t brightness);

#endif // !USER_CONFIG_H