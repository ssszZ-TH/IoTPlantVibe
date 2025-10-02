#include <DHT.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

// กำหนด ID สำหรับ sensor แต่ละตัว (เปลี่ยนได้ตามจำนวน sensor)
#define SENSOR_ID "cucumber_1" // แก้เป็น cucumber_2, cucumber_3, ... สำหรับ sensor อื่น

// กำหนดโหมด: true = Local (Mosquitto), false = Cloud (HiveMQ)
#define USE_LOCAL true

#define DHTPIN 18
#define DHTTYPE DHT11
#define LIGHT_SENSOR 4
#define LED_BUILTIN 2

// WiFi
const char* ssid = "Magic5";
const char* password = "8charactor";

// MQTT Config
#if USE_LOCAL
  const char* mqtt_server = "10.13.75.210"; // IP เครื่องที่รัน Mosquitto
  const int mqtt_port = 1883;
  const char* mqtt_user = ""; // ไม่ใช้ user/pass สำหรับ local
  const char* mqtt_pass = "";
#else
  const char* mqtt_server = "xxxxxx.s1.eu.hivemq.cloud"; // HiveMQ server
  const int mqtt_port = 8883;
  const char* mqtt_user = "your-username"; // HiveMQ credentials
  const char* mqtt_pass = "your-password";
#endif
// สร้าง topic เฉพาะ sensor โดยใช้ SENSOR_ID
char mqtt_topic[50]; // Buffer สำหรับ topic

DHT dht(DHTPIN, DHTTYPE);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000); // UTC+7

#if USE_LOCAL
  WiFiClient espClient;
#else
  WiFiClientSecure espClient;
#endif
PubSubClient mqttClient(espClient);

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  dht.begin();
  
  // สร้าง topic เฉพาะ sensor (e.g., cucumber/sensors/cucumber_1)
  snprintf(mqtt_topic, sizeof(mqtt_topic), "cucumber/sensors/%s", SENSOR_ID);
  
  // WiFi connect
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH); delay(500);
    digitalWrite(LED_BUILTIN, LOW); delay(500);
    Serial.print(".");
  }
  for (int i = 0; i < 2; i++) { // WiFi connected: 2 จังหวะ
    digitalWrite(LED_BUILTIN, HIGH); delay(300);
    digitalWrite(LED_BUILTIN, LOW); delay(300);
  }
  Serial.println("WiFi connected");

  // NTP
  timeClient.begin();

  // MQTT setup
  #if !USE_LOCAL
    espClient.setInsecure(); // Test; production ใช้ CA cert
  #endif
  mqttClient.setServer(mqtt_server, mqtt_port);
  reconnect();
}

void loop() {
  if (!mqttClient.connected()) { reconnect(); }
  mqttClient.loop();

  timeClient.update();
  
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  int light_adc = analogRead(LIGHT_SENSOR);
  float lux = (light_adc / 4095.0) * 1000.0;
  float lumen = lux * 0.0079;

  // ISO time
  unsigned long epochTime = timeClient.getEpochTime();
  time_t rawtime = epochTime;
  struct tm * ti = localtime(&rawtime);
  char isoTime[25];
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", ti);

  // JSON: เพิ่ม sensor_id เพื่อระบุ sensor ใน InfluxDB/Grafana
  StaticJsonDocument<200> doc;
  doc["time"] = isoTime;
  doc["sensor_id"] = SENSOR_ID; // เพิ่ม ID เพื่อแยก sensor
  doc["temp"] = temp;
  doc["hum"] = hum;
  doc["light"] = lumen;

  String payload;
  serializeJson(doc, payload);

  // Publish ไป topic เฉพาะ sensor
  if (mqttClient.publish(mqtt_topic, payload.c_str())) {
    Serial.println("Published: " + payload);
  } else {
    Serial.println("Publish failed");
  }

  delay(10000); // ทุก 10 วิ
}

void reconnect() {
  unsigned long start = millis();
  while (!mqttClient.connected() && (millis() - start < 30000)) { // Timeout 30 วินาที
    Serial.print("Connecting MQTT...");
    // ใช้ SENSOR_ID เป็น client ID เพื่อแยก sensor
    #if USE_LOCAL
      if (mqttClient.connect(SENSOR_ID)) {
    #else
      if (mqttClient.connect(SENSOR_ID, mqtt_user, mqtt_pass)) {
    #endif
        Serial.println("connected");
        for (int i = 0; i < 3; i++) { // MQTT connected: 3 จังหวะ
          digitalWrite(LED_BUILTIN, HIGH); delay(200);
          digitalWrite(LED_BUILTIN, LOW); delay(200);
        }
    } else {
      Serial.print("failed, rc="); Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}