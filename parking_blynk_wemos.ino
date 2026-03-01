/*****************************************************
 * Wemos D1 (ESP8266) parking controller
 * - 2x IR sensors -> Blynk LED widgets + LCD status
 * - 2x Blynk Switch widgets -> 2x relay outputs
 * - Occupancy logs to Google Sheets via Apps Script
 *****************************************************/

#define BLYNK_TEMPLATE_ID "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "ParkingMonitor"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// ------------------- Wi-Fi -------------------
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

// ------------------- Google Sheets -------------------
// Deploy an Apps Script web app and paste URL below.
// Example:
// https://script.google.com/macros/s/AKfycbxxxxxxxxxxxxxxxx/exec
const String GOOGLE_SCRIPT_URL = "YOUR_GOOGLE_APPS_SCRIPT_WEBAPP_URL";

// ------------------- Pin mapping (GPIO numbers) -------------------
// Relay1 -> D7 -> GPIO13
// Relay2 -> D9 -> GPIO2 (as requested)
// IR1    -> D8 -> GPIO0
// IR2    -> D4 -> GPIO4
// LCD SCL -> D5 -> GPIO14
// LCD SDA -> D6 -> GPIO12
constexpr uint8_t RELAY1_PIN = 13;
constexpr uint8_t RELAY2_PIN = 2;
constexpr uint8_t IR1_PIN = 0;
constexpr uint8_t IR2_PIN = 4;
constexpr uint8_t I2C_SDA_PIN = 12;
constexpr uint8_t I2C_SCL_PIN = 14;

// ------------------- Blynk virtual pins -------------------
// LED widgets (set mode to "LED")
constexpr uint8_t VPIN_LED_SPOT1 = V0;
constexpr uint8_t VPIN_LED_SPOT2 = V1;
// Switch widgets (set mode to "Switch")
constexpr uint8_t VPIN_SW_RELAY1 = V2;
constexpr uint8_t VPIN_SW_RELAY2 = V3;

// LCD at common I2C address 0x27 (change to 0x3F if needed)
LiquidCrystal_I2C lcd(0x27, 16, 2);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);  // UTC+5:30, update every 60s

BlynkTimer timer;

struct SpotState {
  bool occupied = false;
  unsigned long occupiedSinceMs = 0;
};

SpotState spot1;
SpotState spot2;

// IR modules are usually active LOW when object detected.
constexpr bool IR_ACTIVE_LEVEL = LOW;

String urlEncode(const String &value) {
  String encoded;
  encoded.reserve(value.length() * 3);

  for (size_t i = 0; i < value.length(); i++) {
    const char c = value.charAt(i);

    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", static_cast<uint8_t>(c));
      encoded += hex;
    }
  }

  return encoded;
}

void sendToGoogleSheet(const String &spot, unsigned long durationSec, const String &stopReason) {
  if (GOOGLE_SCRIPT_URL == "YOUR_GOOGLE_APPS_SCRIPT_WEBAPP_URL") {
    return;  // Skip until configured.
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  timeClient.update();
  unsigned long epoch = timeClient.getEpochTime();

  // Date and time are split by Apps Script if needed. Sending epoch + formatted time text.
  String dateTime = timeClient.getFormattedTime();

  String endpoint = GOOGLE_SCRIPT_URL;
  endpoint += "?epoch=" + String(epoch);
  endpoint += "&time=" + urlEncode(dateTime);
  endpoint += "&spot=" + urlEncode(spot);
  endpoint += "&duration=" + String(durationSec);
  endpoint += "&reason=" + urlEncode(stopReason);

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  HTTPClient http;
  if (!http.begin(*client, endpoint)) {
    return;
  }

  http.setTimeout(4000);
  http.GET();
  http.end();
}

void updateLCD(bool s1Occupied, bool s2Occupied) {
  lcd.setCursor(0, 0);
  lcd.print("Spot1 Spot2    ");

  lcd.setCursor(0, 1);
  lcd.print(s1Occupied ? "Detect" : "Empty ");

  lcd.setCursor(6, 1);
  lcd.print(" ");

  lcd.setCursor(7, 1);
  lcd.print(s2Occupied ? "Detect" : "Empty ");
}

void processSpot(uint8_t irPin, SpotState &spot, const String &spotName, uint8_t ledVpin) {
  const bool occupiedNow = (digitalRead(irPin) == IR_ACTIVE_LEVEL);

  if (occupiedNow != spot.occupied) {
    spot.occupied = occupiedNow;
    Blynk.virtualWrite(ledVpin, occupiedNow ? 255 : 0);

    if (occupiedNow) {
      spot.occupiedSinceMs = millis();
      sendToGoogleSheet(spotName, 0, "Detected");
    } else {
      unsigned long occupiedDurationSec = 0;
      if (spot.occupiedSinceMs > 0) {
        occupiedDurationSec = (millis() - spot.occupiedSinceMs) / 1000;
      }
      sendToGoogleSheet(spotName, occupiedDurationSec, "Car Left");
      spot.occupiedSinceMs = 0;
    }
  }
}

void publishStatus() {
  processSpot(IR1_PIN, spot1, "Spot1", VPIN_LED_SPOT1);
  processSpot(IR2_PIN, spot2, "Spot2", VPIN_LED_SPOT2);
  updateLCD(spot1.occupied, spot2.occupied);
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(VPIN_SW_RELAY1, VPIN_SW_RELAY2);
  Blynk.virtualWrite(VPIN_LED_SPOT1, spot1.occupied ? 255 : 0);
  Blynk.virtualWrite(VPIN_LED_SPOT2, spot2.occupied ? 255 : 0);
}

BLYNK_WRITE(VPIN_SW_RELAY1) {
  const int relayState = param.asInt();
  digitalWrite(RELAY1_PIN, relayState ? HIGH : LOW);
}

BLYNK_WRITE(VPIN_SW_RELAY2) {
  const int relayState = param.asInt();
  digitalWrite(RELAY2_PIN, relayState ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);

  pinMode(IR1_PIN, INPUT_PULLUP);
  pinMode(IR2_PIN, INPUT_PULLUP);

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);

  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Parking System");
  lcd.setCursor(0, 1);
  lcd.print("Booting...");

  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  timeClient.begin();

  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect();

  // Initial status push
  publishStatus();

  // Poll IR sensors and update outputs every 300 ms
  timer.setInterval(300L, publishStatus);
}

void loop() {
  Blynk.run();
  timer.run();
}
