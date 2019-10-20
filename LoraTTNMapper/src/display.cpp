#include "globals.h"
#include "display.h"

HAS_DISPLAY u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/SCL, /* data=*/SDA); // ESP32 Thing, HW I2C with pin remapping
U8G2LOG u8g2log;
uint8_t u8log_buffer[U8LOG_WIDTH * U8LOG_HEIGHT];

void log_display(String s)
{
  Serial.println(s);
  if (runmode < 1)
  {
    u8g2log.print(s);
    u8g2log.print("\n");
  }
}

void setup_display(void)
{
  u8g2.begin();
  u8g2.setFont(u8g2_font_profont11_mf);                         // set the font for the terminal window
  u8g2log.begin(u8g2, U8LOG_WIDTH, U8LOG_HEIGHT, u8log_buffer); // connect to u8g2, assign buffer
  u8g2log.setLineHeightOffset(0);                               // set extra space between lines in pixel, this can be negative
  u8g2log.setRedrawMode(0);                                     // 0: Update screen with newline, 1: Update screen for every char
  u8g2.enableUTF8Print();
  log_display("SAP GTT");
  log_display("TTN-ABP-Mapper");
}

void drawSymbol(u8g2_uint_t x, u8g2_uint_t y, uint8_t symbol)
{
  // fonts used:
  // u8g2_font_open_iconic_embedded_6x_t
  // u8g2_font_open_iconic_weather_6x_t
  // encoding values, see: https://github.com/olikraus/u8g2/wiki/fntgrpiconic

  switch (symbol)
  {
  case SUN:
    u8g2.setFont(u8g2_font_open_iconic_weather_4x_t);
    u8g2.drawGlyph(x, y, 69);
    break;
  case SUN_CLOUD:
    u8g2.setFont(u8g2_font_open_iconic_weather_4x_t);
    u8g2.drawGlyph(x, y, 65);
    break;
  case CLOUD:
    u8g2.setFont(u8g2_font_open_iconic_weather_4x_t);
    u8g2.drawGlyph(x, y, 64);
    break;
  case RAIN:
    u8g2.setFont(u8g2_font_open_iconic_weather_4x_t);
    u8g2.drawGlyph(x, y, 67);
    break;
  case THUNDER:
    u8g2.setFont(u8g2_font_open_iconic_embedded_4x_t);
    u8g2.drawGlyph(x, y, 67);
    break;
  case SLEEP:
    u8g2.setFont(u8g2_font_open_iconic_all_4x_t);
    u8g2.drawGlyph(x, y, 72);
    break;
  case ICON_NOTES:
    u8g2.setFont(u8g2_font_open_iconic_all_4x_t);
    u8g2.drawGlyph(x, y, 225);
    break;
  }
}

void showPage(int page)
{
  // block i2c bus access
  if (!I2C_MUTEX_LOCK())
    ESP_LOGV(TAG, "[%0.3f] i2c mutex lock failed", millis() / 1000.0);
  else
  {

    u8g2.clearBuffer();
    uint8_t icon = 0;

    u8g2.setFont(u8g2_font_ncenB12_tr);
    u8g2.drawStr(1, 15, "   SAP GTT  ");

    switch (page)
    {
    case PAGE_VALUES:

      u8g2.setFont(u8g2_font_profont11_mf);
      u8g2.setCursor(1, 30);
      u8g2.printf("Sats:%.2d", gps.tGps.satellites.value());
      u8g2.setCursor(64, 30);
      u8g2.printf("%02d:%02d:%02d", gps.tGps.time.hour(), gps.tGps.time.minute(), gps.tGps.time.second());

      u8g2.setCursor(1, 40);
      u8g2.printf("Alt:%.4g", gps.tGps.altitude.meters());
      u8g2.setCursor(1, 50);
      u8g2.printf("Len:%.2d", dataBuffer.data.lmic.dataLen);
      u8g2.setCursor(64, 50);
      u8g2.printf("TX:%.3d", dataBuffer.data.txCounter);
      u8g2.setCursor(1, 50);
      u8g2.printf("Sleep:%.2d", dataBuffer.data.sleepCounter);

#if (defined BAT_MEASURE_ADC || defined HAS_PMU)
      u8g2.setCursor(1, 60);
      //u8g2.printf("%.2fV", dataBuffer.data.bat_voltage / 1000.0);
#endif
      break;

    case PAGE_SLEEP:
      u8g2.setFont(u8g2_font_profont11_mf);
      //u8g2.setCursor(1, 30);
      //u8g2.printf("Sleeping until:%.2d", gps.tGps.satellites.value());

      if (dataBuffer.data.sleepCounter <= 0)
      {
        drawSymbol(48, 50, RAIN);
      }

      if (dataBuffer.data.txCounter >= SLEEP_AFTER_N_TX_COUNT)
      {
        drawSymbol(1, 48, SUN);
      }

      u8g2.setFont(u8g2_font_profont11_mf);
      u8g2.setCursor(1, 52);
      u8g2.printf("GPS: off");
      u8g2.setCursor(1, 64);
      u8g2.printf("Sleeping for %.2d min", TIME_TO_SLEEP);
      break;
    }

    u8g2.sendBuffer();
    I2C_MUTEX_UNLOCK(); // release i2c bus access
  }
}


DataBuffer::DataBuffer()
{
}

void DataBuffer::set(deviceStatus_t input)
{
  data = input;
}

void DataBuffer::get()
{
}

DataBuffer dataBuffer;
deviceStatus_t sensorValues;