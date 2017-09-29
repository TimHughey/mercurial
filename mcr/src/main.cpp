#include <SPI.h>
#include <TimeLib.h>
#include <WiFi101.h>
#include <Wire.h>
#include <elapsedMillis.h>
// #include <Adafruit_SleepyDog.h>
#include <ArduinoJson.h>

#include "include/mcr_ds.hpp"
#include "include/mcr_i2c.hpp"
#include "include/mcr_mqtt.hpp"
#include "include/mcr_util.hpp"

// boards 32u4, M0, and 328p
#define LED 13
#define VBAT_PIN A7
// #define AM215_PWR_PIN 12
// #define SOIL_METER_PWR_PIN 11
// #define SOIL_METER_ANALOG_PIN A5

//#if defined(ARDUINO_SAMD_ZERO) && defined(SERIAL_PORT_USBVIRTUAL)
// Required for Serial on Zero based boards
//#define Serial SERIAL_PORT_USBVIRTUAL
//#endif

// prototypes
void printFreeMem(uint8_t secs);
void printWiFiData();
void printCurrentNet();

// #define ALT_WIFI

#ifdef ALT_WIFI
const char ssid[] = "TravelerVI";
const char pass[] = "425voosgt5ti8";
#else
const char ssid[] = "WissLanding";               //  your network SSID (name)
const char pass[] = "I once was a porch kitty."; // your network password
#endif

int wifiStatus = WL_IDLE_STATUS; // the WiFi radio's status

char mcrID[25];

#define MQTT_SERVER "jophiel.wisslanding.com"
#define MQTT_PORT 1883

void statusIndicator();

WiFiClient wifi;

#ifdef ALT_WIFI
IPAddress broker(108, 35, 196, 234);
#else
IPAddress broker(192, 168, 2, 4);
#endif

mcrMQTT_t *mqtt = nullptr;
mcrDS_t *ds = nullptr;
mcrI2c_t *i2c = nullptr;

int debugMode = 0;

void setup() {
  WiFi.setPins(8, 7, 4, 2);
  Serial.begin(115200);
  while (!Serial && millis() < 5000) {
    ;
  }

  logDateTime(__PRETTY_FUNCTION__);
  log("serial initialized", true);

  if (WiFi.status() == WL_NO_SHIELD) {
    logDateTime(__PRETTY_FUNCTION__);
    Serial.println("wifi shield not detected");
  } else {
    logDateTime(__PRETTY_FUNCTION__);
    Serial.print("connecting to WPA SSID ");
    Serial.print(ssid);
    Serial.print("...");
    while (wifiStatus != WL_CONNECTED) {
      wifiStatus = WiFi.begin(ssid, pass);

      delay(1);
      Serial.print(".");
    }
  }

  Serial.println();

  mcrUtil::printNet(__PRETTY_FUNCTION__);

  logDateTime(__PRETTY_FUNCTION__);
  Serial.print("mcrID: ");
  Serial.println(mcrUtil::hostID());

  // Watchdog.enable(5000);
  setSyncInterval(120); // setting a high time sync interval since we rely on
                        // updates via MQTT

  logDateTime(__PRETTY_FUNCTION__);
  Serial.print("mcrMQTT");
  mqtt = new mcrMQTT(wifi, broker, MQTT_PORT);
  Serial.print(" created, ");
  mqtt->connect();
  Serial.print("connected, ");
  mqtt->announceStartup();
  Serial.println("announced startup");

  logDateTime(__PRETTY_FUNCTION__);
  Serial.print("mcrDS");
  ds = new mcrDS(mqtt);
  Serial.print(" created,");
  ds->init();
  Serial.println(" initialized");

  logDateTime(__PRETTY_FUNCTION__);
  Serial.print("mcrI2C");
  i2c = new mcrI2c(mqtt);
  Serial.print(" created, ");
  i2c->init();
  Serial.println("initialized");

  mcrUtil::printFreeMem(__PRETTY_FUNCTION__, 0);
  logDateTime(__PRETTY_FUNCTION__);
  Serial.println("completed, transition to main::loop()");
}

elapsedMillis loop_duration;
void loop() {
  static bool first_entry = true;
  static const time_t _loop_run_warning = (150 * 1000); // 150ms
  elapsedMicros loop_elapsed;

  if (first_entry) {
    logDateTime(__PRETTY_FUNCTION__);
    log("first invocation", true);
    first_entry = false;
  }

  mqtt->loop();
  ds->loop();
  i2c->loop();

  statusIndicator();
  mcrUtil::printFreeMem(__PRETTY_FUNCTION__, 15);

  // Watchdog.reset();

  if (loop_elapsed > (_loop_run_warning)) {
    logDateTime(__PRETTY_FUNCTION__);
    log("[WARNING] elapsed time ");
    logElapsedMicros(loop_elapsed, true);
  }
}

void statusIndicator() {
  static boolean led_state = true;
  static elapsedMillis statusMillis = 0;
  uint8_t flash_rate = 100;

  if (ds->isConvertActive() == true) {
    flash_rate = 35;
  }

  if (statusMillis >= flash_rate) {
    if (led_state) {
      digitalWrite(LED, HIGH);
    } else {
      digitalWrite(LED, LOW);
    }

    led_state = !led_state;
    statusMillis = 0;
  }
}
