/**
 * ============================================================================
 *  PERINGATAN DINI POTENSI KEBAKARAN HUTAN — src/main.cpp (PlatformIO Build)
 *  Tim : Testarossa's Vanguard — Universitas Ahmad Dahlan
 * ============================================================================
 *  File ini identik dengan sketch.ino untuk kompilasi PlatformIO.
 *  Gunakan sketch.ino jika membuka langsung di Wokwi Editor.
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ── PIN DEFINITION ────────────────────────────────────────────────────────────
#define PIN_DHT         4
#define DHTTYPE         DHT22
#define PIN_MQ2         34
#define PIN_SOIL        35
#define PIN_LED_GREEN   18
#define PIN_LED_YELLOW  19
#define PIN_LED_RED     21
#define PIN_BUZZER      22

// ── WIFI & MQTT ───────────────────────────────────────────────────────────────
const char* WIFI_SSID       = "Wokwi-GUEST";
const char* WIFI_PASS       = "";
const char* MQTT_BROKER     = "broker.hivemq.com";
const int   MQTT_PORT       = 1883;
const char* DEVICE_ID       = "ESP32_FIRE_NODE_01";
const char* TOPIC_TELEMETRY = "testarossas_vanguard/fire/karhutla/telemetry";
const char* TOPIC_CONTROL   = "testarossas_vanguard/fire/karhutla/control";

// ── TIMING ────────────────────────────────────────────────────────────────────
const unsigned long SEND_INTERVAL     = 3000;
const unsigned long SERVER_TIMEOUT_MS = 12000;
const unsigned long BLINK_PERIOD_MS   = 500;

// ── THRESHOLD ─────────────────────────────────────────────────────────────────
const float TEMP_WARN   = 32.0;
const float TEMP_DANGER = 40.0;
const float SOIL_WARN   = 35.0;
const float SOIL_DANGER = 20.0;
const float SMOKE_WARN  = 200.0;
const float SMOKE_DANGER= 400.0;

// ── GLOBALS ───────────────────────────────────────────────────────────────────
DHT          dht(PIN_DHT, DHTTYPE);
WiFiClient   espClient;
PubSubClient mqtt(espClient);

enum RiskLevel : uint8_t { LEVEL_NORMAL=0, LEVEL_WARNING=1, LEVEL_DANGER=2 };
RiskLevel     riskLevel     = LEVEL_NORMAL;
const char*   RISK_NAMES[]  = { "Normal", "Warning", "Danger" };

bool          serverOnline  = false;
unsigned long lastServerCmd = 0;
unsigned long lastSendTime  = 0;
unsigned long lastBlinkTime = 0;
bool          blinkState    = false;

// ── PROTOTYPES ────────────────────────────────────────────────────────────────
void      connectWiFi();
void      connectMQTT();
void      onMQTTMessage(char*, byte*, unsigned int);
void      readAllSensors(float&, float&, float&, float&);
RiskLevel localRiskEval(float, float, float, float);
void      updateActuators();
void      publishTelemetry(float, float, float, float);
void      printSensorTable(float, float, float, float);

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println(F("╔══════════════════════════════════════════════╗"));
  Serial.println(F("║  SISTEM PERINGATAN DINI KEBAKARAN HUTAN      ║"));
  Serial.println(F("║  Tim : Testarossa's Vanguard - UAD           ║"));
  Serial.println(F("╚══════════════════════════════════════════════╝"));

  pinMode(PIN_LED_GREEN,  OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED,    OUTPUT);
  pinMode(PIN_BUZZER,     OUTPUT);

  // Self-test aktuator
  Serial.println(F("[BOOT] Self-test aktuator..."));
  digitalWrite(PIN_LED_GREEN, HIGH); digitalWrite(PIN_LED_YELLOW, HIGH);
  digitalWrite(PIN_LED_RED,   HIGH); tone(PIN_BUZZER, 1000, 200);
  delay(400);
  digitalWrite(PIN_LED_GREEN, LOW); digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_RED,   LOW);
  delay(200);

  dht.begin();
  connectWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(onMQTTMessage);
  mqtt.setBufferSize(512);

  Serial.println(F("[SYSTEM] Siap. Mulai membaca sensor...\n"));
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  } else {
    if (!mqtt.connected()) connectMQTT();
    mqtt.loop();
  }

  unsigned long now = millis();

  if (now - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = now;

    float temp=0, hum=0, soil=0, smoke=0;
    readAllSensors(temp, hum, soil, smoke);

    bool serverTimedOut = serverOnline && (now - lastServerCmd > SERVER_TIMEOUT_MS);
    if (serverTimedOut) {
      serverOnline = false;
      Serial.println(F("[WARN] Server Python timeout! Beralih ke evaluasi LOKAL."));
    }

    if (!serverOnline) {
      riskLevel = localRiskEval(temp, hum, soil, smoke);
    }

    printSensorTable(temp, hum, soil, smoke);
    publishTelemetry(temp, hum, soil, smoke);
  }

  updateActuators();
}

// ─────────────────────────────────────────────────────────────────────────────
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  static unsigned long lastConnectAttempt = 0;
  if (millis() - lastConnectAttempt < 8000 && lastConnectAttempt != 0) return;
  lastConnectAttempt = millis();

  Serial.printf("[WIFI] Menghubungkan ke '%s' (Channel 6)...", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
    delay(250);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Terhubung! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println(F("\n[WIFI] Gagal. Coba lagi dalam 8 detik..."));
  }
}

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;

  static unsigned long lastTry = 0;
  if (millis() - lastTry < 5000) return;
  lastTry = millis();

  char clientId[48];
  snprintf(clientId, sizeof(clientId), "%s_%04X", DEVICE_ID, (unsigned)random(0xFFFF));

  Serial.printf("[MQTT] Menghubungkan ke %s:%d ...", MQTT_BROKER, MQTT_PORT);
  if (mqtt.connect(clientId)) {
    Serial.println(F(" Terhubung!"));
    mqtt.subscribe(TOPIC_CONTROL);
    Serial.printf("[MQTT] Berlangganan topik: %s\n", TOPIC_CONTROL);
  } else {
    Serial.printf(" Gagal (rc=%d)\n", mqtt.state());
  }
}

void onMQTTMessage(char* topic, byte* payload, unsigned int len) {
  char buf[256];
  if (len >= sizeof(buf)) len = sizeof(buf)-1;
  memcpy(buf, payload, len);
  buf[len] = '\0';
  Serial.printf("[MQTT] Terima [%s]: %s\n", topic, buf);

  StaticJsonDocument<128> doc;
  String statusStr;
  if (buf[0] == '{') {
    if (!deserializeJson(doc, buf)) {
      if (doc.containsKey("status"))    statusStr = doc["status"].as<const char*>();
      else if (doc.containsKey("risk")) statusStr = doc["risk"].as<const char*>();
    }
  } else { statusStr = String(buf); statusStr.trim(); }

  if      (statusStr.equalsIgnoreCase("Normal"))  riskLevel = LEVEL_NORMAL;
  else if (statusStr.equalsIgnoreCase("Warning")) riskLevel = LEVEL_WARNING;
  else if (statusStr.equalsIgnoreCase("Danger"))  riskLevel = LEVEL_DANGER;
  else return;

  serverOnline  = true;
  lastServerCmd = millis();
  Serial.printf("[MQTT] Status: %s (dari Server)\n", RISK_NAMES[riskLevel]);
}

void readAllSensors(float& temp, float& hum, float& soil, float& smoke) {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  temp = isnan(t) ? 27.0f : t;
  hum  = isnan(h) ? 65.0f : h;

  int rawSoil  = analogRead(PIN_SOIL);
  soil  = constrain((float)map(rawSoil,  0, 4095, 0, 100),  0.0f, 100.0f);

  int rawSmoke = analogRead(PIN_MQ2);
  smoke = constrain((float)map(rawSmoke, 0, 4095, 0, 1000), 0.0f, 1000.0f);
}

RiskLevel localRiskEval(float temp, float hum, float soil, float smoke) {
  bool dangerCombined = (temp >= TEMP_DANGER && soil <= SOIL_DANGER && smoke >= SMOKE_DANGER);
  bool dangerSmoke    = (smoke >= SMOKE_DANGER * 1.5f);
  bool dangerHeat     = (temp >= 45.0f && smoke >= 250.0f);
  if (dangerCombined || dangerSmoke || dangerHeat) return LEVEL_DANGER;

  if (temp >= TEMP_WARN || soil <= SOIL_WARN || smoke >= SMOKE_WARN) return LEVEL_WARNING;
  return LEVEL_NORMAL;
}

void updateActuators() {
  unsigned long now = millis();
  if (now - lastBlinkTime >= BLINK_PERIOD_MS) {
    lastBlinkTime = now;
    blinkState    = !blinkState;
  }
  switch (riskLevel) {
    case LEVEL_NORMAL:
      digitalWrite(PIN_LED_GREEN,  HIGH);
      digitalWrite(PIN_LED_YELLOW, LOW);
      digitalWrite(PIN_LED_RED,    LOW);
      digitalWrite(PIN_BUZZER,     LOW);
      break;
    case LEVEL_WARNING:
      digitalWrite(PIN_LED_GREEN,  LOW);
      digitalWrite(PIN_LED_YELLOW, blinkState ? HIGH : LOW);
      digitalWrite(PIN_LED_RED,    LOW);
      digitalWrite(PIN_BUZZER,     LOW);
      break;
    case LEVEL_DANGER:
      digitalWrite(PIN_LED_GREEN,  LOW);
      digitalWrite(PIN_LED_YELLOW, LOW);
      digitalWrite(PIN_LED_RED,    HIGH);
      digitalWrite(PIN_BUZZER,     blinkState ? HIGH : LOW);
      break;
  }
}

void publishTelemetry(float temp, float hum, float soil, float smoke) {
  if (!mqtt.connected()) return;
  StaticJsonDocument<256> doc;
  doc["device_id"]     = DEVICE_ID;
  doc["temperature"]   = serialized(String(temp,  1));
  doc["humidity"]      = serialized(String(hum,   1));
  doc["soil_moisture"] = serialized(String(soil,  1));
  doc["smoke_ppm"]     = (int)smoke;
  doc["local_status"]  = RISK_NAMES[riskLevel];
  doc["server_ctrl"]   = serverOnline;
  char buf[256];
  serializeJson(doc, buf);
  if (mqtt.publish(TOPIC_TELEMETRY, buf))
    Serial.printf("[MQTT] Telemetri dikirim ke '%s'\n", TOPIC_TELEMETRY);
  else
    Serial.println(F("[MQTT] Gagal publish."));
}

void printSensorTable(float temp, float hum, float soil, float smoke) {
  const char* mode = serverOnline ? "Server Python" : "Lokal (Fallback)";
  Serial.println(F("┌─────────────────────────────────────────────────┐"));
  Serial.printf( "│ Suhu Udara       : %5.1f °C                     │\n", temp);
  Serial.printf( "│ Kelembapan Udara : %5.1f %%                      │\n", hum);
  Serial.printf( "│ Kelembapan Tanah : %5.1f %%                      │\n", soil);
  Serial.printf( "│ Gas/Asap (MQ-2)  : %5.0f ppm                    │\n", smoke);
  Serial.printf( "│ Status Risiko    : %-10s [%s]\n", RISK_NAMES[riskLevel], mode);
  Serial.println(F("└─────────────────────────────────────────────────┘"));
}
