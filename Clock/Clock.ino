#include <Arduino.h>
#include "ui.h"
#include "lvgl.h"
#include "demos/lv_demos.h"
#include "bsp_lvgl_port.h"
#include "./src/port_bsp/i2c_bsp.h"
#include "./src/port_bsp/axp2101_bsp.h"
#include "pcf85063a.h"
#include <EEPROM.h>

#define EEPROM_SIZE 1
pcf85063a_datetime_t datatime = {};

I2cMasterBus I2cMasterBus_(GPIO_NUM_7, GPIO_NUM_8, I2C_NUM_0);
static uint8_t i2cPMICAddress;
static pcf85063a_dev_t pcf85063;
static char LvglDataBuff[40] = { "" };
int secBuf=78;

unsigned long lastUpdate = 0;
unsigned long lastUpdateRTC = 0;
unsigned long lastUpdateLINE = 0;

uint8_t back = 50;  // backlight
int deb=0;

bool timeFormat=0;  //0 = 24h 1= 12H
int minn=0;
int hrss=2;
bool change=0;

int linePos=-19;

int smallLinePosY[8]={-98,-90,-82,-74,-66,-58,-50,-42}; //-36
int smallLinePosX[8]={178,180,178,180,178,180,178,180};

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("start");

     EEPROM.begin(EEPROM_SIZE);
     timeFormat = EEPROM.read(0);
     Serial.printf("timeFormat = %d\n", timeFormat);

  pinMode(10, INPUT_PULLUP);
  randomSeed(esp_random());
  

  Custom_PmicPortInit(&I2cMasterBus_, 0x34);

  bsp_lvgl_init(I2cMasterBus_);
  if (bsp_lvgl_lock(0)) {
    Serial.println("start ui");
    ui_init();
    bsp_lvgl_unlock();
  }

    if (bsp_lvgl_lock(0)) {
    Lcd_SetBacklight(back);
    bsp_lvgl_unlock();
  }

  esp_err_t ret = pcf85063a_init(&pcf85063, I2cMasterBus_.Get_I2cBusHandle(), PCF85063A_ADDRESS);
  if (ret != ESP_OK) {
    ESP_LOGE("pcf85063", "Failed to initialize PCF85063 (error: %d)", ret);
  } else {
    pcf85063a_datetime_t datatime = {
      .year = 2026,
      .month = 1,
      .day = 1,
      .hour = 18,
      .min = 34,
      .sec = 0
    };
    //pcf85063a_set_time_date(&pcf85063, datatime);
  }
}

void setTimeFormat(lv_event_t * e)  //24h or 12 h
{
timeFormat=lv_obj_has_state(ui_Switch1, LV_STATE_CHECKED);
EEPROM.write(0, timeFormat);
EEPROM.commit();
Serial.printf("timeFormat = %d\n", timeFormat);
change=1;
}

void timeSetF(lv_event_t * e)
{
      lv_obj_t *btn = lv_event_get_target(e);

    if (btn == ui_hrUp) {
        hrss++;
        if(hrss>23)
          {hrss=0;}
        change=1;
    }
    if (btn == ui_hrDown) {
        hrss--;
        if(hrss<0)
          {hrss=23;}
        change=1;
    }


      if (btn == ui_minUp) {
        minn++;
        if(minn>59)
           {minn=0;}
        change=1;
    }
    if (btn == ui_minDown) {
        minn--;
        if(minn<0)
          {minn=59;}
        change=1;
    }

    if (btn == ui_okBut)
    {
      pcf85063a_datetime_t datatime = {
      .year = 2026,
      .month = 1,
      .day = 1,
      .hour = hrss,
      .min = minn,
      .sec = 0
    };
    pcf85063a_set_time_date(&pcf85063, datatime);
    lv_scr_load(ui_Screen1);
    }
}

void setTimeScreen(lv_event_t * e)
{
  pcf85063a_datetime_t datatime = {};
  pcf85063a_get_time_date(&pcf85063, &datatime);
  hrss=datatime.hour;
  minn=datatime.min;

  lv_scr_load(ui_Screen2);
  change=1;
}


void loop() {

   lv_obj_t *current;

if (bsp_lvgl_lock(10)) {
    current = lv_scr_act();
    bsp_lvgl_unlock();
} else {
    return;
}

    if (current == ui_Screen1) {

    if(digitalRead(10)==0)
    {
        if(deb==0)
        {
            deb=1;
            back=back+20;
            if(back>100) back=20;
            Lcd_SetBacklight(back);
        }
    }else deb=0;


    if (millis() - lastUpdateLINE >= 6)
    {
      lastUpdateLINE=millis();
    linePos++;
    if(linePos>400) linePos=-100;
    if(linePos<131)
    if (bsp_lvgl_lock(10)) {
    lv_obj_set_pos(ui_lineRed, linePos, 44);
    bsp_lvgl_unlock(); }
    }

  /*
  int smallLinePosY[8]={-98,-90,-82,-74,-66,-58,-50,-42}; //-36
int smallLinePosX[8]={178,180,178,180,178,180,178,180};
  */
     if (millis() - lastUpdate >= 60)
    {
        lastUpdate = millis();

    for(int i=0;i<8;i++)
    {
      smallLinePosY[i]++;
      if(smallLinePosY[i]>-36)
      smallLinePosY[i]=-98;
    }
    
    if (bsp_lvgl_lock(10)) {
    lv_obj_set_pos(ui_line1, smallLinePosX[0], smallLinePosY[0]);
    lv_obj_set_pos(ui_line2, smallLinePosX[1], smallLinePosY[1]);
    lv_obj_set_pos(ui_line3, smallLinePosX[2], smallLinePosY[2]);
    lv_obj_set_pos(ui_line4, smallLinePosX[3], smallLinePosY[3]);
    lv_obj_set_pos(ui_line5, smallLinePosX[4], smallLinePosY[4]);
    lv_obj_set_pos(ui_line6, smallLinePosX[5], smallLinePosY[5]);
    lv_obj_set_pos(ui_line7, smallLinePosX[6], smallLinePosY[6]);
    lv_obj_set_pos(ui_line8, smallLinePosX[7], smallLinePosY[7]);
    bsp_lvgl_unlock(); }
    }
    
   

   if (millis() - lastUpdateRTC >= 100)
    {
     lastUpdateRTC = millis();  
    
    pcf85063a_get_time_date(&pcf85063, &datatime);
    pcf85063a_datetime_to_str(LvglDataBuff, datatime);
    }

    if(datatime.sec!=secBuf)
    {

    int displayHour = datatime.hour;

        if (timeFormat == 1) {
            displayHour = datatime.hour % 12;

            if (displayHour == 0) {
                displayHour = 12;
            }
        }

    snprintf(LvglDataBuff, sizeof(LvglDataBuff), "%02d%02d%02d",
    displayHour, datatime.min, datatime.sec);
    Serial.printf("rtc:%s\n", LvglDataBuff);
    secBuf=datatime.sec;
    String timeString = String(LvglDataBuff);
     
      if (bsp_lvgl_lock(10)) {
      lv_label_set_text(ui_dig1,timeString.substring(0,1).c_str());
      lv_label_set_text(ui_dig2,timeString.substring(1,2).c_str());
      lv_label_set_text(ui_dig3,timeString.substring(2,3).c_str());
      lv_label_set_text(ui_dig4,timeString.substring(3,4).c_str());
      lv_label_set_text(ui_dig5,timeString.substring(4,5).c_str());
      lv_label_set_text(ui_dig6,timeString.substring(5,6).c_str());
      bsp_lvgl_unlock(); }
    }
  }

  if (current == ui_Screen2) {
    if(change)
    {

       int displayHour = hrss;

        if (timeFormat == 1) {
            displayHour = hrss % 12;

            if (displayHour == 0) {
                displayHour = 12;
            }
        }
 
     char buf[3];
     snprintf(buf, sizeof(buf), "%02d", displayHour);

     char buf2[3];
     snprintf(buf2, sizeof(buf2), "%02d", minn);

    lv_label_set_text(ui_setHr,buf);
    lv_label_set_text(ui_setMin,buf2);
    change=0;
    }
  }
}
