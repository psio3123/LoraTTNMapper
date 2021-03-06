// T-Beam specific hardware
#undef BUILTIN_LED
#define BUILTIN_LED 14
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */

#define displayRefreshIntervall 1    // every x second
#define sendMessagesIntervall 60     // every x seconds
#define sendCayenneIntervall 30      // every x seconds
#define LORAsendMessagesIntervall 40 // every x seconds

#define SEALEVELPRESSURE_HPA (1013.25)

#include "globals.h"

//--------------------------------------------------------------------------
// OTA Settings
//--------------------------------------------------------------------------
#include "SecureOTA.h"
const uint16_t OTA_CHECK_INTERVAL = 3000; // ms
uint32_t _lastOTACheck = 0;
bool wifi_connected = false;

//--------------------------------------------------------------------------
// Wifi Settings
//--------------------------------------------------------------------------

//const char ssid[] = "MrFlexi";
//const char wifiPassword[] = "Linde-123";
//WiFiClient wifiClient;

//--------------------------------------------------------------------------
// Lora Helper
//--------------------------------------------------------------------------
const char *getSfName(rps_t rps)
{
  const char *const t[] = {"FSK", "SF7", "SF8", "SF9",
                           "SF10", "SF11", "SF12", "SF?"};
  return t[getSf(rps)];
}

const char *getBwName(rps_t rps)
{
  const char *const t[] = {"BW125", "BW250", "BW500", "BW?"};
  return t[getBw(rps)];
}

const char *getCrName(rps_t rps)
{
  const char *const t[] = {"CR 4/5", "CR 4/6", "CR 4/7", "CR 4/8"};
  return t[getCr(rps)];
}

//--------------------------------------------------------------------------
// Initialize globals
//--------------------------------------------------------------------------
PayloadConvert payload(PAYLOAD_BUFFER_SIZE);

SemaphoreHandle_t I2Caccess;

uint8_t msgWaiting = 0;

RTC_DATA_ATTR int bootCount = 0;
touch_pad_t touchPin;

//--------------------------------------------------------------------------
// Tasks/Ticker
//--------------------------------------------------------------------------

TaskHandle_t irqHandlerTask = NULL;
TaskHandle_t moveDisplayHandlerTask = NULL;
TaskHandle_t t_cyclic_HandlerTask = NULL;


Ticker sleepTicker;
Ticker displayTicker;
Ticker displayMoveTicker;
Ticker sendMessageTicker;
Ticker sendCayenneTicker;
Ticker LORAsendMessageTicker;

//--------------------------------------------------------------------------
// Sensors
//--------------------------------------------------------------------------
#if (USE_BME)
Adafruit_BME280 bme; // I2C   PIN 21 + 22
#endif

//--------------------------------------------------------------------------
// Cayenne MyDevices Integration
//--------------------------------------------------------------------------

#if (USE_CAYENNE)
#define CAYENNE_PRINT Serial

#include <CayenneMQTTESP32.h>
char username[] = "ecddac20-a0eb-11e9-94e9-493d67fd755e";
char password[] = "0010d05f8ccd918d0f8a45451950f8b80200e594";
char clientID[] = "44257070-b074-11e9-80af-177b80d8d7b2"; // DE001-Balkon

CAYENNE_CONNECTED()
{
  log_display("Cayenne connected...");
}

CAYENNE_DISCONNECTED()
{
  log_display("Cayenne connection lost...");
  bool disconnected = true;
  while (disconnected)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      log_display("Wifi is back...");
      disconnected = false;
    }
    else
    {
      log_display("No wifi...");
    }
    delay(2000);
  }
}

CAYENNE_OUT_DEFAULT()
{
}

void Cayenne_send(void)
{

  log_display("Cayenne send");

  Cayenne.celsiusWrite(1, dataBuffer.data.temperature);
  Cayenne.virtualWrite(2, dataBuffer.data.humidity, "rel_hum", "p");

  Cayenne.virtualWrite(10, dataBuffer.data.panel_voltage, "voltage", "Volts");
  Cayenne.virtualWrite(12, dataBuffer.data.panel_current, "current", "Milliampere");

  Cayenne.virtualWrite(20, dataBuffer.data.bus_voltage, "voltage", "Volts");
  Cayenne.virtualWrite(21, dataBuffer.data.bus_current, "current", "Milliampere");

  Cayenne.virtualWrite(30, dataBuffer.data.bat_voltage, "voltage", "Volts");
  Cayenne.virtualWrite(31, dataBuffer.data.bat_charge_current, "current", "Milliampere");
  Cayenne.virtualWrite(33, dataBuffer.data.bat_discharge_current, "current", "Milliampere");
}

// Default function for processing actuator commands from the Cayenne Dashboard.
// You can also use functions for specific channels, e.g CAYENNE_IN(1) for channel 1 commands.
CAYENNE_IN_DEFAULT()
{
  log_display("Cayenne data received");
  CAYENNE_LOG("Channel %u, value %s", request.channel, getValue.asString());
  //Process message here. If there is an error set an error message using getValue.setError(), e.g getValue.setError("Error message");
}

#endif

//--------------------------------------------------------------------------
// Store preferences in NVS Flash
//--------------------------------------------------------------------------
//Preferences preferences;
char lastword[10];

unsigned long uptime_seconds_old;
unsigned long uptime_seconds_new;
unsigned long uptime_seconds_actual;

String stringOne = "";

static const char TAG[] = __FILE__;

#if (HAS_INA)
void print_ina()
{
  Serial.println("");
  float shuntvoltage1 = 0;
  float busvoltage1 = 0;
  float current_mA1 = 0;
  float loadvoltage1 = 0;

  busvoltage1 = ina3221.getBusVoltage_V(1);
  shuntvoltage1 = ina3221.getShuntVoltage_mV(1);
  current_mA1 = -ina3221.getCurrent_mA(1); // minus is to get the "sense" right.   - means the battery is charging, + that it is discharging
  loadvoltage1 = busvoltage1 + (shuntvoltage1 / 1000);

  Serial.print("Bus Voltage:");
  Serial.print(busvoltage1);
  Serial.println(" V");
  Serial.print("Shunt Voltage:");
  Serial.print(shuntvoltage1);
  Serial.println(" mV");
  Serial.print("Battery Load Voltage:");
  Serial.print(loadvoltage1);
  Serial.println(" V");
  Serial.print("Battery Current 1:");
  Serial.print(current_mA1);
  Serial.println(" mA");
  Serial.println("");
}
#endif

void touch_callback()
{
  //placeholder callback function
}

void display_chip_info()
{
  // print chip information on startup if in verbose mode after coldstart

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  ESP_LOGI(TAG,
           "This is ESP32 chip with %d CPU cores, WiFi%s%s, silicon revision "
           "%d, %dMB %s Flash",
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
           chip_info.revision, spi_flash_get_chip_size() / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded"
                                                         : "external");
  ESP_LOGI(TAG, "Internal Total heap %d, internal Free Heap %d",
           ESP.getHeapSize(), ESP.getFreeHeap());

#if (BOARD_HAS_PSRAM)
  ESP_LOGI(TAG, "SPIRam Total heap %d, SPIRam Free Heap %d",
           ESP.getPsramSize(), ESP.getFreePsram());

#endif

  ESP_LOGI(TAG, "ChipRevision %d, Cpu Freq %d, SDK Version %s",
           ESP.getChipRevision(), ESP.getCpuFreqMHz(), ESP.getSdkVersion());
  ESP_LOGI(TAG, "Flash Size %d, Flash Speed %d", ESP.getFlashChipSize(),
           ESP.getFlashChipSpeed());

#if (HAS_LORA)
  ESP_LOGI(TAG, "IBM LMIC version %d.%d.%d", LMIC_VERSION_MAJOR,
           LMIC_VERSION_MINOR, LMIC_VERSION_BUILD);
  ESP_LOGI(TAG, "Arduino LMIC version %d.%d.%d.%d",
           ARDUINO_LMIC_VERSION_GET_MAJOR(ARDUINO_LMIC_VERSION),
           ARDUINO_LMIC_VERSION_GET_MINOR(ARDUINO_LMIC_VERSION),
           ARDUINO_LMIC_VERSION_GET_PATCH(ARDUINO_LMIC_VERSION),
           ARDUINO_LMIC_VERSION_GET_LOCAL(ARDUINO_LMIC_VERSION));
#endif // HAS_LORA

#if (HAS_GPS)
  ESP_LOGI(TAG, "TinyGPS+ version %s", TinyGPSPlus::libraryVersion());
#endif
}

void print_wakeup_reason()
{
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  Serial.print(F("WakeUp caused by: "));
  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_EXT0:
    Serial.println(F("external signal using RTC_IO"));
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    Serial.println(F("external signal using RTC_CNTL"));
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.println("by timer");
    break;
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    Serial.println(F("touchpad"));
    break;
  case ESP_SLEEP_WAKEUP_ULP:
    Serial.println(F("ULP program"));
    break;
  default:
    Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
    break;
  }
}

void print_wakeup_touchpad()
{
  touch_pad_t pin;
  touchPin = esp_sleep_get_touchpad_wakeup_status();
  switch (touchPin)
  {
  case 0:
    Serial.println("Touch detected on GPIO 4");
    break;
  case 1:
    Serial.println("Touch detected on GPIO 0");
    break;
  case 2:
    Serial.println("Touch detected on GPIO 2");
    break;
  case 3:
    Serial.println("Touch detected on GPIO 15");
    break;
  case 4:
    Serial.println("Touch detected on GPIO 13");
    break;
  case 5:
    Serial.println("Touch detected on GPIO 12");
    break;
  case 6:
    Serial.println("Touch detected on GPIO 14");
    break;
  case 7:
    Serial.println("Touch detected on GPIO 27");
    break;
  case 8:
    Serial.println("Touch detected on GPIO 33");
    break;
  case 9:
    Serial.println("Touch detected on GPIO 32");
    break;
  default:
    Serial.println("Wakeup not by touchpad");
    break;
  }
}

void setup_sensors()
{
#if (USE_BME280)
  ESP_LOGI(TAG, "BME280 Setup...");
  unsigned status;

  status = bme.begin(0x76);
  if (!status)
  {
    ESP_LOGI(TAG, "Could not find a valid BME280 sensor");
  }
  else
  {
    Serial.println();
    Serial.print("Temperature = ");
    Serial.print(bme.readTemperature());
    Serial.println(" *C");
    Serial.print("Pressure = ");
    Serial.print(bme.readPressure() / 100.0F);
    Serial.println(" hPa");
    Serial.print("Approx. Altitude = ");
    Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
    Serial.println(" m");
    Serial.print("Humidity = ");
    Serial.print(bme.readHumidity());
    Serial.println(" %");
    Serial.println();
  }
#endif
}

void t_send_cayenne()
{

#if (USE_CAYENNE)
  Cayenne_send();
#endif
}



void t_cyclicRTOS(void *pvParameters)
{
  
}

void t_cyclic()
{
  String stringOne;
    ESP_LOGI(TAG, "Runmode %d", dataBuffer.data.runmode);
    dataBuffer.data.freeheap =  ESP.getFreeHeap();
// Temperatur
#if (USE_BME280)
    dataBuffer.data.temperature = bme.readTemperature();
    dataBuffer.data.humidity = bme.readHumidity();
    ESP_LOGI(TAG, "BME280  %.1f C/%.1f%", dataBuffer.data.temperature, dataBuffer.data.humidity);
#endif

#if (HAS_PMU)
    dataBuffer.data.bus_voltage = pmu.getVbusVoltage() / 1000;
    dataBuffer.data.bus_current = pmu.getVbusCurrent();

    dataBuffer.data.bat_voltage = pmu.getBattVoltage() / 1000;
    dataBuffer.data.bat_charge_current = pmu.getBattChargeCurrent();
    dataBuffer.data.bat_discharge_current = pmu.getBattDischargeCurrent();
    // AXP192_showstatus();
#else
    dataBuffer.data.bat_voltage = read_voltage() / 1000;
#endif

#if (HAS_INA)
    //print_ina();
    dataBuffer.data.panel_voltage = ina3221.getBusVoltage_V(1);
    dataBuffer.data.panel_current = ina3221.getCurrent_mA(1);
#endif

#if (USE_ADXL345)
    adxl_dumpValues();
#endif

#if (HAS_LORA)

    ESP_LOGI(TAG, "Radio parameters: %s / %s / %s",
             getSfName(updr2rps(LMIC.datarate)),
             getBwName(updr2rps(LMIC.datarate)),
             getCrName(updr2rps(LMIC.datarate)));

    if (LoraSendQueue != 0)
    {
      dataBuffer.data.LoraQueueCounter = uxQueueMessagesWaiting(LoraSendQueue);
    }
    else
    {
      dataBuffer.data.LoraQueueCounter = 0;
    }
#endif

    gps.encode();
    gps.checkGpsFix();

// Refresh Display
#if (USE_DISPLAY)
if ( dataBuffer.data.runmode > 0)
    showPage(PageNumber);
#endif

#if (USE_DASH)
    if (WiFi.status() == WL_CONNECTED)
      update_web_dash();
#endif
}

void t_sleep()
{
  //-----------------------------------------------------
  // Deep sleep
  //-----------------------------------------------------

#if (ESP_SLEEP)
  dataBuffer.data.sleepCounter--;
  if (dataBuffer.data.sleepCounter <= 0 || dataBuffer.data.txCounter >= SLEEP_AFTER_N_TX_COUNT)
  {

#if (HAS_PMU)
    AXP192_power(pmu_power_sleep);
#endif
    gps.enable_sleep();
    Serial.flush();
    showPage(PAGE_SLEEP);
    ESP_LOGI(TAG, "Deep Sleep started");
    esp_deep_sleep_start();
    Serial.println("This will never be printed");
  }
#endif
}

void setup_wifi()
{

#if (USE_WIFI)
  // WIFI Setup
  WiFi.begin(ssid, wifiPassword);

  ESP_LOGI(TAG, "Connecting to WiFi..");
  int i = 0;
  wifi_connected = false;
  while ((WiFi.status() != WL_CONNECTED) && (i < 10))
  {
    delay(1000);
    i++;
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    wifi_connected = true;
    ESP_LOGV(TAG, String(WiFi.localIP()));
    log_display(String(WiFi.localIP()));
    delay(2000);
  }
  else
  {

    //Turn off WiFi if no connection was made
    log_display("WIFI OFF");
    WiFi.mode(WIFI_OFF);
  }

#endif
}



#if (USE_BLE)
void setup_BLE() {  
  Serial.println("Setup BLE");

  BLEDevice::init("Long name works now");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE
                                       );

  pCharacteristic->setValue("SAP GTT");
  pService->start();
  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("SAP GTT Monitor");
}
#endif


  void createRTOStasks()
  {

  xTaskCreatePinnedToCore(t_cyclicRTOS,              // task function
                          "t_cyclic",            // name of task
                          4096,                  // stack size of task
                          (void *)1,             // parameter of the task
                          2,                     // priority of the task
                          &t_cyclic_HandlerTask, // task handle
                          1);                    // CPU core

  xTaskCreatePinnedToCore(t_moveDisplayRTOS,           // task function
                          "moveDisplay",           // name of task
                          4096,                    // stack size of task
                          (void *)1,               // parameter of the task
                          2,                       // priority of the task
                          &moveDisplayHandlerTask, // task handle
                          1);                      // CPU core

  }

void setup()
{
  Serial.begin(115200);
  dataBuffer.data.runmode = 0;
  Serial.println("Runmode: " + String(dataBuffer.data.runmode));

  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  print_wakeup_reason();
  // print_wakeup_touchpad();
  display_chip_info();

  // create some semaphores for syncing / mutexing tasks
  I2Caccess = xSemaphoreCreateMutex(); // for access management of i2c bus
  assert(I2Caccess != NULL);
  I2C_MUTEX_UNLOCK();
  delay(1000);

  //---------------------------------------------------------------
  // Get preferences from Flash
  //---------------------------------------------------------------
  //preferences.begin("config", false); // NVS Flash RW mode
  //preferences.getULong("uptime", uptime_seconds_old);
  //Serial.println("Uptime old: " + String(uptime_seconds_old));
  //preferences.getString("info", lastword, sizeof(lastword));

  ESP_LOGI(TAG, "Starting..");
  Serial.println(F("TTN Mapper"));
  i2c_scan();
  delay(2000);

#if (HAS_PMU)
  AXP192_init();
  AXP192_showstatus();
  AXP192_power_gps(ON);
#endif

delay(1000);

#if (HAS_INA)
  ina3221.begin();
  Serial.print("Manufact. ID=0x");
  int MID;
  MID = ina3221.getManufID();
  Serial.println(MID, HEX);
  print_ina();
#endif

  dataBuffer.data.txCounter = 0;
  dataBuffer.data.sleepCounter = TIME_TO_NEXT_SLEEP;
  dataBuffer.data.firmware_version = VERSION;

  setup_display();
  setup_sensors();
  setup_wifi();
  calibrate_voltage();

  //Turn off Bluetooth
  log_display("Stop Bluethooth");
  btStop();
  delay(1000);

#if (USE_MQTT)
  setup_mqtt();
#endif

#if (USE_CAYENNE)
  if (WiFi.status() == WL_CONNECTED)
  {
    Cayenne.begin(username, password, clientID, ssid, wifiPassword);
    log_display("Cayenne connected...");
  }
#endif

  //---------------------------------------------------------------
  // OTA Update
  //---------------------------------------------------------------

#if (USE_OTA)
  if (WiFi.status() == WL_CONNECTED)
  {
    _lastOTACheck = millis();
    checkFirmwareUpdates();
    
  }
#endif
delay(1000);

#if (USE_ADXL345)
  setup_adxl345();
#ifdef ADXL_INT
  attachInterrupt(digitalPinToInterrupt(ADXL_INT), ADXL_IRQ, CHANGE);
#endif
#endif

//---------------------------------------------------------------
// Deep sleep settings
//---------------------------------------------------------------
#if (ESP_SLEEP)
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR * 60);
  log_display("ESP32 wake-up timer " + String(TIME_TO_SLEEP) +
              " min");

#ifdef HAS_BUTTON
//esp_sleep_enable_ext0_wakeup(HAS_BUTTON, 0); //1 = High, 0 = Low
#endif

#if (USE_ADXL345)
#ifdef ADXL_INT
  //esp_sleep_enable_ext0_wakeup(ADXL_INT, 0); //1 = High, 0 = Low
#endif
#endif

#endif

  gps.init();
  //gps.softwareReset();
  gps.wakeup();
  gps.ecoMode();

  delay(2000); // Wait for GPS beeing stable


#if (USE_BLE)
setup_BLE();
#endif


#if (HAS_LORA)
  setup_lora();
  lora_queue_init();
  delay(1000);
#endif

#if (USE_DASH)
  if (WiFi.status() == WL_CONNECTED)
  {
    create_web_dash();
  }
#endif

#ifdef HAS_BUTTON
  pinMode(HAS_BUTTON, INPUT_PULLUP);
  button_init(HAS_BUTTON);
#endif

  //-------------------------------------------------------------------------------
  // Tasks
  //-------------------------------------------------------------------------------
  log_display("Starting Tasks");

  sleepTicker.attach(60, t_sleep);
  displayTicker.attach(displayRefreshIntervall, t_cyclic);
  displayMoveTicker.attach(displayMoveIntervall, t_moveDisplay);

#if (HAS_LORA)
  sendMessageTicker.attach(sendMessagesIntervall, t_enqueue_LORA_messages);
#endif

#if (USE_CAYENNE)
  sendCayenneTicker.attach(sendCayenneIntervall, t_send_cayenne);
#endif

// Interrupt ISR Handler
#if (USE_INTERRUPTS)
  ESP_LOGI(TAG, "Starting Interrupt Handler...");
  xTaskCreatePinnedToCore(irqHandler,      // task function
                          "irqhandler",    // name of task
                          4096,            // stack size of task
                          (void *)1,       // parameter of the task
                          2,               // priority of the task
                          &irqHandlerTask, // task handle
                          1);              // CPU core
#endif



//#if (USE_CAYENNE)
//  if (WiFi.status() == WL_CONNECTED)
//    Cayenne_send();
//#endif

#if (HAS_LORA)
  t_enqueue_LORA_messages();
#endif

  log_display("Setup done");

  dataBuffer.data.runmode = 1; // Switch from Terminal Mode to page Display
  Serial.println("Runmode5: " + String(dataBuffer.data.runmode));
  //showPage(PAGE_VALUES);
}

void loop()
{
#if (HAS_LORA)
  os_runloop_once();
#endif

#if (USE_CAYENNE)
  if (WiFi.status() == WL_CONNECTED)
  {
    Cayenne.loop();
  }
#endif

#if (USE_MQTT)
  // MQTT Connection
  if (WiFi.status() == WL_CONNECTED)
  {
    if (!client.connected())
    {
      reconnect();
    }
    client.loop();
  }
#endif

#if (HAS_BUTTON)
  readButton();
#endif
  delay(1);
}