#include <DHT.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

// กำหนดโหมด: true = Local (Mosquitto), false = Cloud (HiveMQ)
#define USE_LOCAL true

#define DHTPIN 18             // Pin สำหรับ DHT11 sensor
#define DHTTYPE DHT11         // ประเภท sensor DHT11
#define LIGHT_SENSOR 4        // Pin สำหรับ light sensor
#define LED_BUILTIN 2         // Pin สำหรับ LED built-in

// WiFi credentials
const char* ssid = "Magic5";          // ชื่อ WiFi
const char* password = "8charactor";  // รหัส WiFi

// MQTT Config
#if USE_LOCAL
  const char* mqtt_server = "10.13.75.210"; // IP เครื่องที่รัน Mosquitto
  const int mqtt_port = 1883;               // MQTT port สำหรับ local
  const char* mqtt_user = "";               // ไม่ใช้ user/pass สำหรับ local
  const char* mqtt_pass = "";
#else
  const char* mqtt_server = "xxxxxx.s1.eu.hivemq.cloud"; // HiveMQ server
  const int mqtt_port = 8883;                           // MQTT port สำหรับ cloud
  const char* mqtt_user = "your-username";              // HiveMQ credentials
  const char* mqtt_pass = "your-password";
#endif

// Buffer สำหรับ topic เฉพาะ sensor
char mqtt_topic[50];     // e.g., cucumber/sensors/24:0A:C4:12:34:56

DHT dht(DHTPIN, DHTTYPE);         // เริ่มต้น DHT sensor
WiFiUDP ntpUDP;                   // UDP สำหรับ NTP
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000); // NTP client (UTC+7)

#if USE_LOCAL
  WiFiClient espClient;           // Client สำหรับ local MQTT (non-TLS)
#else
  WiFiClientSecure espClient;     // Client สำหรับ cloud MQTT (TLS)
#endif
PubSubClient mqttClient(espClient); // MQTT client

String sensorId; // ตัวแปร global สำหรับเก็บ MAC address

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);    // ตั้งค่า LED pin เป็น output
  Serial.begin(115200);            // เริ่ม Serial communication
  dht.begin();                     // เริ่มต้น DHT sensor
  
  // รอให้ WiFi stack เริ่มต้นสมบูรณ์
  WiFi.mode(WIFI_STA);            // ตั้งค่าเป็น Station mode
  delay(100);                     // รอสั้นๆ เพื่อให้ WiFi stack พร้อม
  
  // WiFi connect
  WiFi.begin(ssid, password);      // เริ่มเชื่อมต่อ WiFi
  while (WiFi.status() != WL_CONNECTED) { // รอจนกว่าจะต่อ WiFi ได้
    digitalWrite(LED_BUILTIN, HIGH); delay(500); // กระพริบ LED 1 จังหวะ
    digitalWrite(LED_BUILTIN, LOW); delay(500);
    Serial.print(".");
  }
  for (int i = 0; i < 2; i++) {    // WiFi connected: กระพริบ 2 จังหวะ
    digitalWrite(LED_BUILTIN, HIGH); delay(300);
    digitalWrite(LED_BUILTIN, LOW); delay(300);
  }
  Serial.println("WiFi connected");

  // ดึง MAC address เป็น SENSOR_ID หลังจาก WiFi ต่อสำเร็จ
  sensorId = WiFi.macAddress(); // e.g., "24:0A:C4:12:34:56"
  if (sensorId == "00:00:00:00:00:00") { // ตรวจสอบ MAC address ไม่ถูกต้อง
    Serial.println("Error: Invalid MAC address, restarting...");
    ESP.restart(); // รีสตาร์ทถ้าได้ MAC address ไม่ถูกต้อง
  }
  Serial.println("MAC Address: " + sensorId);
  
  // สร้าง topic เฉพาะ sensor (e.g., cucumber/sensors/24:0A:C4:12:34:56)
  snprintf(mqtt_topic, sizeof(mqtt_topic), "cucumber/sensors/%s", sensorId.c_str());
  
  // เริ่มต้น NTP
  timeClient.begin();

  // MQTT setup
  #if !USE_LOCAL
    espClient.setInsecure();       // TLS สำหรับ test (ไม่แนะนำใน production)
  #endif
  mqttClient.setServer(mqtt_server, mqtt_port); // ตั้งค่า MQTT server
  reconnect();                     // เริ่มเชื่อมต่อ MQTT
}

void loop() {
  if (!mqttClient.connected()) { reconnect(); } // Reconnect ถ้า MQTT หลุด
  mqttClient.loop();              // อัปเดต MQTT client

  timeClient.update();            // อัปเดตเวลา NTP
  
  // อ่านข้อมูลจาก sensor
  float temp = dht.readTemperature();  // อ่านอุณหภูมิ
  float hum = dht.readHumidity();      // อ่านความชื้น
  int light_adc = analogRead(LIGHT_SENSOR); // อ่านค่า light sensor
  float lux = (light_adc / 4095.0) * 1000.0; // แปลงเป็น lux
  float lumen = lux * 0.0079;          // แปลงเป็น lumen

  // สร้าง ISO time
  unsigned long epochTime = timeClient.getEpochTime();
  time_t rawtime = epochTime;
  struct tm * ti = localtime(&rawtime);
  char isoTime[25];
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", ti);

  // สร้าง JSON payload
  StaticJsonDocument<200> doc;
  doc["time"] = isoTime;                       // Timestamp
  doc["sensor_id"] = sensorId;                 // ใช้ global sensorId
  doc["temp"] = temp;                          // อุณหภูมิ
  doc["hum"] = hum;                            // ความชื้น
  doc["light"] = lumen;                        // ความสว่าง (lumen)

  String payload;
  serializeJson(doc, payload);                 // แปลง JSON เป็น string

  // Publish ไป topic เฉพาะ sensor
  if (mqttClient.publish(mqtt_topic, payload.c_str())) {
    Serial.println("Published: " + payload);
  } else {
    Serial.println("Publish failed");
  }

  delay(10000); // ส่งทุก 10 วินาที
}

void reconnect() {
  unsigned long start = millis();
  while (!mqttClient.connected() && (millis() - start < 30000)) { // Timeout 30 วินาที
    Serial.print("Connecting MQTT...");
    // ใช้ MAC address เป็น client ID
    if (sensorId == "00:00:00:00:00:00") { // ตรวจสอบ MAC address อีกครั้ง
      Serial.println("Error: Invalid MAC address, restarting...");
      ESP.restart();
    }
    #if USE_LOCAL
      if (mqttClient.connect(sensorId.c_str())) { // เชื่อมต่อ MQTT สำหรับ local
        Serial.println("connected");
        for (int i = 0; i < 3; i++) { // MQTT connected: กระพริบ 3 จังหวะ
          digitalWrite(LED_BUILTIN, HIGH); delay(200);
          digitalWrite(LED_BUILTIN, LOW); delay(200);
        }
      } else {
        Serial.print("failed, rc="); Serial.println(mqttClient.state());
        delay(5000);
      }
    #else
      if (mqttClient.connect(sensorId.c_str(), mqtt_user, mqtt_pass)) { // สำหรับ cloud
        Serial.println("connected");
        for (int i = 0; i < 3; i++) { // MQTT connected: กระพริบ 3 จังหวะ
          digitalWrite(LED_BUILTIN, HIGH); delay(200);
          digitalWrite(LED_BUILTIN, LOW); delay(200);
        }
      } else {
        Serial.print("failed, rc="); Serial.println(mqttClient.state());
        delay(5000);
      }
    #endif
  }
}