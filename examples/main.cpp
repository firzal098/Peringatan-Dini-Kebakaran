
#include <WiFi.h>
#include <PubSubClient.h>

// ==================================================
// 1. WIFI CONFIGURATION
// ==================================================
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// ==================================================
// 2. MQTT HIVEMQ CONFIGURATION
// ==================================================
const char* MQTT_BROKER = "broker.hivemq.com";
const int MQTT_PORT = 1883;

// Harus sama dengan topic di Google Colab
const char* MQTT_TOPIC =
    "iot-workshop/ardi2026/group01/distance";

// ==================================================
// 3. PIN CONFIGURATION
// ==================================================
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;
const int LED_PIN = 2;
const int BUZZER_PIN = 4;

// ==================================================
// 4. ALERT THRESHOLD
// ==================================================
const float DISTANCE_THRESHOLD = 50.0;

// ==================================================
// 5. MQTT CLIENT
// ==================================================
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ==================================================
// 6. TIMER
// ==================================================
unsigned long lastPublishTime = 0;
const unsigned long PUBLISH_INTERVAL = 1000;

// ==================================================
// 7. WIFI CONNECTION
// ==================================================
void connectWiFi() {
  Serial.println();
  Serial.println("Connecting to WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED &&
         attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connection failed.");
  }
}

// ==================================================
// 8. MQTT CONNECTION
// ==================================================
bool connectMQTT() {
  if (mqtt.connected()) {
    return true;
  }

  Serial.println("Connecting to MQTT broker...");

  String clientId = "ESP32-IoT-";
  clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

  if (mqtt.connect(clientId.c_str())) {
    Serial.println("MQTT connected!");
    Serial.print("MQTT Topic: ");
    Serial.println(MQTT_TOPIC);
    return true;
  }

  Serial.print("MQTT connection failed. State: ");
  Serial.println(mqtt.state());

  return false;
}

// ==================================================
// 9. READ HC-SR04 DISTANCE
// ==================================================
float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Timeout after 30 ms
  unsigned long duration =
      pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    return -1.0;
  }

  float distance = duration * 0.0343 / 2.0;

  return distance;
}

// ==================================================
// 10. CONTROL LED AND BUZZER
// ==================================================
void updateAlert(float distance) {
  if (distance >= 0 &&
      distance <= DISTANCE_THRESHOLD) {

    digitalWrite(LED_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);

    Serial.println("STATUS: ALERT");
    Serial.println("LED: ON | BUZZER: ON");

  } else if (distance > DISTANCE_THRESHOLD) {

    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    Serial.println("STATUS: NORMAL");
    Serial.println("LED: OFF | BUZZER: OFF");

  } else {
    // Sensor read failed: do not report NORMAL
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    Serial.println("STATUS: SENSOR ERROR");
  }
}

// ==================================================
// 11. PUBLISH MQTT DATA
// ==================================================
void publishMQTT(float distance) {
  if (distance < 0) {
    Serial.println("Invalid distance. Skipping publish.");
    return;
  }

  if (!mqtt.connected()) {
    Serial.println("MQTT disconnected. Reconnecting...");
    if (!connectMQTT()) {
      return;
    }
  }

  const char* status =
      (distance <= DISTANCE_THRESHOLD)
      ? "ALERT"
      : "NORMAL";

  char payload[100];

  snprintf(
      payload,
      sizeof(payload),
      "{\"distance\":%.1f,\"status\":\"%s\"}",
      distance,
      status
  );

  bool published = mqtt.publish(
      MQTT_TOPIC,
      payload
  );

  Serial.print("MQTT payload: ");
  Serial.println(payload);

  if (published) {
    Serial.println("Publish successful.");
  } else {
    Serial.println("Publish failed.");
  }

  Serial.println("--------------------------------");
}

// ==================================================
// 12. SETUP
// ==================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println(" MINI IoT MONITORING SYSTEM");
  Serial.println(" ESP32 + HC-SR04 + MQTT");
  Serial.println("================================");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  connectWiFi();

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setKeepAlive(60);
  mqtt.setSocketTimeout(5);

  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }
}

// ==================================================
// 13. MAIN LOOP
// ==================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    connectWiFi();
    delay(500);
    return;
  }

  if (!mqtt.connected()) {
    connectMQTT();
  }

  mqtt.loop();

  unsigned long now = millis();

  if (now - lastPublishTime >= PUBLISH_INTERVAL) {
    lastPublishTime = now;

    float distance = readDistance();

    Serial.print("Distance: ");

    if (distance >= 0) {
      Serial.print(distance, 1);
      Serial.println(" cm");
    } else {
      Serial.println("Sensor read error");
    }

    updateAlert(distance);
    publishMQTT(distance);
  }
}
