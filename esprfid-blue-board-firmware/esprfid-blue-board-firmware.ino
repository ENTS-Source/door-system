#include <WiFiManager.h>
#include <ArduinoOTA.h>

// ESP8266 (NodeMCU 12E)

// ==== Configuration/ ====

// --- Pin Configuration
#define PIN_RELAY 5
#define PIN_OPEN_BTN 4
#define PIN_WIFI_LED LED_BUILTIN
#define PIN_D0 13
#define PIN_D1 12
#define PIN_OPEN_LED 15

// --- Relay Configuration
#define RELAY_OPEN LOW
#define RELAY_CLOSE HIGH

// --- Open LED configuration
#define OPEN_LED_ON LOW
#define OPEN_LED_OFF HIGH

// --- Button configuration
#define BTN_PUSHED LOW

// --- Door configuration
#define DOOR_OPEN_MS 3000

// --- Wifi configuration
#define WIFI_LED_ON LOW
#define WIFI_LED_OFF HIGH

// ==== /Configuration ====

WiFiManager wifiManager;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_OPEN_BTN, INPUT);
  pinMode(PIN_WIFI_LED, OUTPUT);
  pinMode(PIN_D0, INPUT);
  pinMode(PIN_D1, INPUT);
  pinMode(PIN_OPEN_LED, OUTPUT);

  digitalWrite(PIN_RELAY, RELAY_CLOSE);
  digitalWrite(PIN_WIFI_LED, WIFI_LED_OFF);
  digitalWrite(PIN_OPEN_LED, OPEN_LED_OFF);

  bool ok = wifiManager.autoConnect();
  if (!ok) {
    Serial.println("Failed to set up wifi");
    while (true) {
      for (byte i = 0; i < 5; i++) { // 4 flashes
        digitalWrite(PIN_WIFI_LED, WIFI_LED_ON);
        delay(150);
        digitalWrite(PIN_WIFI_LED, WIFI_LED_OFF);
        delay(150);
      }
      delay(1000);
    }
  }
  digitalWrite(PIN_WIFI_LED, WIFI_LED_ON);

  setupOTA();
}

void loop() {
  ArduinoOTA.handle();
}

bool doingUpdate = false;
long otaSwapStateMs = 50;
bool otaLedOn = false;
void sparkleOTA() {
  if (!doingUpdate) {
    return;
  }
  if (millis() >= otaSwapStateMs) {
    if (otaLedOn) {
      digitalWrite(PIN_WIFI_LED, WIFI_LED_OFF);
    } else {
      digitalWrite(PIN_WIFI_LED, WIFI_LED_ON);
    }
    otaLedOn = !otaLedOn;
    otaSwapStateMs = millis() + 50;
  }
}

void setupOTA() {
  // ArduinoOTA.setPassword("SafetyFirst!");
  ArduinoOTA.onStart([]() {
    doingUpdate = true;
    Serial.println("OTA update started");
  });
  ArduinoOTA.onEnd([]() {
    doingUpdate = false;
    digitalWrite(PIN_WIFI_LED, WIFI_LED_ON); // restore default state
    Serial.println("OTA update complete");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    sparkleOTA();
    unsigned int percent = total == 0 ? 0 : static_cast<unsigned int>(
      (static_cast<uint64_t>(progress) * 100) / 100
    );
    Serial.printf("OTA progress %u%%", percent);
    Serial.println();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    doingUpdate = false;
    digitalWrite(PIN_WIFI_LED, WIFI_LED_ON); // restore default state
    Serial.printf("OTA error: %u - ", error);
    switch(error) {
      case OTA_AUTH_ERROR:
        Serial.println("auth error");
        break;
      case OTA_BEGIN_ERROR:
        Serial.println("begin error");
        break;
      case OTA_CONNECT_ERROR:
        Serial.println("connect error");
        break;
      case OTA_RECEIVE_ERROR:
        Serial.println("receive error");
        break;
      case OTA_END_ERROR:
        Serial.println("end error");
        break;
      default:
        Serial.println("unknown");
        break;
    }
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready");
}