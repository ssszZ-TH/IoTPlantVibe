#include <DHT.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

// โหมดการเชื่อมต่อ MQTT: true = Mosquitto (Local), false = HiveMQ (Cloud)
#define USE_LOCAL true

// พินเซ็นเซอร์
#define DHTPIN 18             // DHT11
#define DHTTYPE DHT11
#define LIGHT_SENSOR 4        // Light sensor (ADC)
#define LED_BUILTIN 2         // LED บนบอร์ด

// WiFi
const char* ssid = "AI-Room";
const char* password = "Cdti2358";

// Backend API สำหรับ lookup MAC → ได้ sensor_code, sensor_name, token
const char* backend_url = "http://172.16.46.14:8080/mac-text/lookup";

// MQTT Config
#if USE_LOCAL
  const char* mqtt_server = "172.16.46.14";
  const int mqtt_port = 1883;
  const char* mqtt_user = "";
  const char* mqtt_pass = "";
#else
  const char* mqtt_server = "xxxxxx.s1.eu.hivemq.cloud";
  const int mqtt_port = 8883;
  const char* mqtt_user = "your-username";
  const char* mqtt_pass = "your-password";
#endif

char mqtt_topic[50];

// ตัวแปรเก็บข้อมูลจาก backend
String mac_address;      // MAC ของบอร์ด (เช่น 7C:DF:A1:00:AD:6E)
String sensor_code;      // รหัสเซ็นเซอร์ (จาก backend)
String sensor_name;      // ชื่อเซ็นเซอร์ (จาก backend)
String token;            // JWT token สำหรับยืนยันตัวตน

DHT dht(DHTPIN, DHTTYPE);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000); // UTC+7

#if USE_LOCAL
  WiFiClient espClient;
#else
  WiFiClientSecure espClient;
#endif
PubSubClient mqttClient(espClient);

/**
 * ฟังก์ชัน: ดึงข้อมูลจาก backend โดยใช้ MAC address
 * ส่ง: {"mac_address": "7C:DF:A1:00:AD:6E"}
 * ได้: {"sensor_code": "CU01", "sensor_name": "ห้อง Lab", "token": "..."}
 */
bool getMacToken() {
  HTTPClient http;
  http.begin(backend_url);
  http.addHeader("Content-Type", "application/json");

  // สร้าง JSON payload สำหรับส่งไป backend
  StaticJsonDocument<100> reqDoc;
  reqDoc["mac_address"] = mac_address;
  String payload;
  serializeJson(reqDoc, payload);

  // ส่ง POST request
  int httpCode = http.POST(payload);
  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("Backend response: " + response);

    // แปลง JSON response
    StaticJsonDocument<400> resDoc;
    DeserializationError error = deserializeJson(resDoc, response);
    if (!error) {
      sensor_code = resDoc["sensor_code"].as<String>();
      sensor_name = resDoc["sensor_name"].as<String>();
      token = resDoc["token"].as<String>();

      Serial.println("sensor_code: " + sensor_code);
      Serial.println("sensor_name: " + sensor_name);
      Serial.println("token: " + token);
      return true;
    } else {
      Serial.println("JSON parse error");
    }
  } else {
    Serial.println("HTTP Error: " + String(httpCode));
    Serial.println("Response: " + http.getString());
  }
  http.end();
  return false;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  dht.begin();

  // เริ่ม WiFi
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH); delay(500);
    digitalWrite(LED_BUILTIN, LOW); delay(500);
    Serial.print(".");
  }
  // WiFi เชื่อมต่อสำเร็จ
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_BUILTIN, HIGH); delay(300);
    digitalWrite(LED_BUILTIN, LOW); delay(300);
  }
  Serial.println("\nWiFi connected");

  // ดึง MAC address (ต้องหลัง WiFi ต่อ)
  mac_address = WiFi.macAddress();
  if (mac_address == "00:00:00:00:00:00") {
    Serial.println("Error: Invalid MAC, restarting...");
    ESP.restart();
  }
  Serial.println("MAC Address: " + mac_address);

  // ดึงข้อมูลจาก backend
  Serial.println("Requesting token from: " + String(backend_url));
  if (!getMacToken()) {
    Serial.println("Failed to get token, restarting...");
    delay(5000);
    ESP.restart();
  }

  // สร้าง MQTT topic: cucumber/sensors/7C:DF:A1:00:AD:6E
  snprintf(mqtt_topic, sizeof(mqtt_topic), "cucumber/sensors/%s", mac_address.c_str());

  // ตั้งค่า NTP และ MQTT
  timeClient.begin();
  #if !USE_LOCAL
    espClient.setInsecure();
  #endif
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setKeepAlive(60);
  mqttClient.setBufferSize(512); // สำหรับ payload ใหญ่
  reconnect();
}

void loop() {
  if (!mqttClient.connected()) reconnect();
  mqttClient.loop();
  timeClient.update();

  // อ่านเซ็นเซอร์
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  int light_adc = analogRead(LIGHT_SENSOR);
  float lux = (light_adc / 4095.0) * 1000.0;
  float lumen = lux * 0.0079;

  // สร้าง ISO timestamp
  unsigned long epochTime = timeClient.getEpochTime();
  time_t rawtime = epochTime;
  struct tm *ti = localtime(&rawtime);
  char isoTime[25];
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", ti);

  // สร้าง JSON payload ตามที่ต้องการ
  StaticJsonDocument<512> doc; // ขยาย buffer ให้ใหญ่ขึ้น
  doc["time"] = isoTime;
  doc["mac_address"] = mac_address;     // MAC ของบอร์ด
  doc["sensor_code"] = sensor_code;     // รหัสเซ็นเซอร์
  doc["sensor_name"] = sensor_name;     // ชื่อเซ็นเซอร์
  doc["token"] = token;                 // JWT token
  doc["temp"] = temp;
  doc["hum"] = hum;
  doc["light"] = lumen;

  String payload;
  serializeJson(doc, payload);

  // ส่งไป MQTT
  if (mqttClient.publish(mqtt_topic, payload.c_str())) {
    Serial.println("Published: " + payload);
  } else {
    Serial.println("Publish failed");
  }

  delay(10000); // ส่งทุก 10 วินาที
}

/**
 * เชื่อมต่อ MQTT ใหม่
 * ใช้ MAC เป็น client ID
 */
void reconnect() {
  unsigned long start = millis();
  while (!mqttClient.connected() && (millis() - start < 30000)) {
    Serial.print("Connecting MQTT...");
    if (mac_address == "00:00:00:00:00:00" || token.isEmpty()) {
      Serial.println("Invalid MAC/token, restarting...");
      ESP.restart();
    }
    #if USE_LOCAL
      if (mqttClient.connect(mac_address.c_str())) {
    #else
      if (mqttClient.connect(mac_address.c_str(), mqtt_user, mqtt_pass)) {
    #endif
        Serial.println("connected");
        for (int i = 0; i < 3; i++) {
          digitalWrite(LED_BUILTIN, HIGH); delay(200);
          digitalWrite(LED_BUILTIN, LOW); delay(200);
        }
      } else {
        Serial.print("failed, rc="); Serial.println(mqttClient.state());
        delay(5000);
      }
  }
}