#include <Arduino.h>
#include <EEPROM.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <ArduinoOTA.h>   // https://github.com/jandrassy/ArduinoOTA
#include <TelnetStream.h> // https://github.com/JAndrassy/TelnetStream

#include "RobustWiegand.h"

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

// --- Telnet configuration
#define TELNET_LOGIN_TIME_MS 60000 // 1 minute

// ==== /Configuration ====

#define EEPROM_SIZE 2048
#define EEPROM_MAGIC1_ADDR 0
#define EEPROM_MAGIC2_ADDR 1
#define EEPROM_PASSWORD_ADDR 2
#define EEPROM_PASSWORD_LEN 32
#define EEPROM_FOBS_ADDR EEPROM_PASSWORD_ADDR+EEPROM_PASSWORD_LEN

WiFiManager wifiManager;
unsigned long closeDoorMs = 0;
RobustWiegand reader(PIN_D0, PIN_D1);

unsigned long telnetLogoutMs = 0;
String telnetVal = "";

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_OPEN_BTN, INPUT);
  pinMode(PIN_WIFI_LED, OUTPUT);
  pinMode(PIN_OPEN_LED, OUTPUT);

  digitalWrite(PIN_RELAY, RELAY_CLOSE);
  digitalWrite(PIN_WIFI_LED, WIFI_LED_OFF);
  digitalWrite(PIN_OPEN_LED, OPEN_LED_OFF);

  EEPROM.begin(EEPROM_SIZE);

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

  TelnetStream.begin();

  RobustWiegand::Config config;
  config.minBitSpacingUs = 1200; // ideally 2ms apart, so cut off 400ms frames
  config.frameGapUs = 20000; // 20ms
  config.useInternalPullups = true;
  config.minFrameBits = 22;
  config.maxFrameBits = 34;
  if (!reader.begin(config)) {
    Serial.println("Failed to initialize Wiegand reader");
  }
}

void loop() {
  ArduinoOTA.handle();
  tryOpenOrClose();
  reader.service();

  RobustWiegand::Frame frame;
  while (reader.read(frame)) {
    uint32_t cardValue = (frame.value >> 1) & 0xFFFFFF;
    TelnetStream.printf(
      "D:Wiegand: bits=%u value=%llu fob=%u rejected=%u overflow=%s",
      frame.bits,
      frame.value,
      cardValue,
      frame.rejectedEdges,
      frame.overflow ? "yes" : "no"
    );
    TelnetStream.println();
  }
  TelnetStream.flush();

  processTelnet();
}

void processTelnet() {
  if (!TelnetStream.available()) {
    return;
  }

  char val = TelnetStream.read();
  bool loggedIn = millis() < telnetLogoutMs;
  if (val != '\r' && val != '\n' && val != '\0') {
    telnetVal += val;
    return;
  }
  String command = telnetVal;
  telnetVal = "";
  if (command.length() == 0) {
    return; // entirely empty, ignore
  }
  if (command.length() < 2) {
    TelnetStream.println("E:Command too short");
    return;
  }
  if (command[1] != ':') {
    TelnetStream.println("E:Malformed command");
    return;
  }
  String args = command.substring(2);
  switch(command[0]) {
    case 'D': // Debug
    case 'A': // Audit
    case 'E': // Error
    case '#': // Comment
      return; // ignore
  }
  if (!loggedIn && command[0] != 'L') {
    TelnetStream.println("E:Not logged in");
    return;
  } else if (!loggedIn) { // Log in
    String password = readPassword();
    if (password == args) {
      telnetLogoutMs = millis() + TELNET_LOGIN_TIME_MS;
      TelnetStream.println("D:Logged in");
    } else {
      TelnetStream.println("E:Wrong password");
    }
    return;
  }

  unsigned long unlockTime = DOOR_OPEN_MS;
  switch(command[0]) {
    case 'L': // Log out (log in handled above)
      telnetLogoutMs = 0;
      TelnetStream.println("D:Logged out");
      break;
    case 'R': // Reset
      TelnetStream.stop();
      delay(100);
      ESP.reset();
      break;
    case 'P': // Change password
      writePassword(args);
      TelnetStream.println("D:Password changed");
      break;
    case 'O': // Open door
      if (args.length() > 0) {
        unlockTime = args.toInt();
      }
      openDoor(unlockTime);
      TelnetStream.print("A:Opening door for ");
      TelnetStream.print(unlockTime);
      TelnetStream.println("ms");
      break;
    default:
      TelnetStream.println("E:Unknown command");
      break;
  }
}

void tryOpenOrClose() {
  if (millis() >= closeDoorMs) {
    digitalWrite(PIN_RELAY, RELAY_CLOSE);
    digitalWrite(PIN_OPEN_LED, OPEN_LED_OFF);
  }
  if (millis() < closeDoorMs) {
    return; // door already open
  }
  if (digitalRead(PIN_OPEN_BTN) == BTN_PUSHED) {
    TelnetStream.println("A:Button push open");
    TelnetStream.flush();
    openDoor(DOOR_OPEN_MS);
  }
}

void openDoor(unsigned long forTime) {
  closeDoorMs = millis() + forTime;
  digitalWrite(PIN_RELAY, RELAY_OPEN);
  digitalWrite(PIN_OPEN_LED, OPEN_LED_ON);
}

bool doingUpdate = false;
unsigned long otaSwapStateMs = 50;
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

void writePassword(String password) {
  EEPROM.write(EEPROM_PASSWORD_ADDR, 0x64);
  EEPROM.write(EEPROM_PASSWORD_ADDR+1, 0x42);
  for (byte i = 0; i < EEPROM_PASSWORD_LEN-2; i++) {
    char val = '\0';
    if (i < password.length()) {
      val = password[i];
    }
    EEPROM.write(EEPROM_PASSWORD_ADDR+2+i, val);
  }
  EEPROM.commit();
}

String readPassword() {
  String password = "CHANGE_ME";
  byte magic1 = EEPROM.read(EEPROM_PASSWORD_ADDR);
  byte magic2 = EEPROM.read(EEPROM_PASSWORD_ADDR+1);
  if (magic1 == 0x64 && magic2 == 0x42) {
    password = "";
    for (byte i = 0; i < EEPROM_PASSWORD_LEN-2; i++) {
      char val = EEPROM.read(EEPROM_PASSWORD_ADDR+2+i);
      if (val == '\0') {
        break;
      }
      password += val;
    }
    Serial.print("Using stored password: ");
    Serial.println(password);
  } else {
    Serial.println("Using default password - this is not recommended");
  }
  return password;
}

void setupOTA() {
  String password = readPassword();
  ArduinoOTA.setPassword(password.c_str());
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