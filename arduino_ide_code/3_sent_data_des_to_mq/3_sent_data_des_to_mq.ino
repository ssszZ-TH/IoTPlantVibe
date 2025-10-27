#include <DHT.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
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
const char* ssid = "Magic5";
const char* password = "8charactor";

// Backend API
const char* backend_url = "http://10.133.243.210:8080/mac-text/lookup"; // เปลี่ยน IP/port ตาม backend

// MQTT Config
#if USE_LOCAL
  const char* mqtt_server = "10.133.243.210";
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
String sensorId, description, token;

DHT dht(DHTPIN, DHTTYPE);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000);

#if USE_LOCAL
  WiFiClient espClient;
#else
  WiFiClientSecure espClient;
#endif
PubSubClient mqttClient(espClient);

bool getMacToken() {
  HTTPClient http;
  http.begin(backend_url);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<100> doc;
  doc["mac_address"] = sensorId;
  String payload;
  serializeJson(doc, payload);
  
  int httpCode = http.POST(payload);
  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("Backend response: " + response);
    
    StaticJsonDocument<300> resDoc;
    DeserializationError error = deserializeJson(resDoc, response);
    if (!error) {
      description = resDoc["description"].as<String>();
      token = resDoc["token"].as<String>();
      Serial.println("Got token: " + token);
      Serial.println("Description: " + description);
      return true;
    }
  }
  Serial.println("Failed to get token, HTTP: " + String(httpCode));
  http.end();
  return false;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  dht.begin();
  
  WiFi.mode(WIFI_STA);
  delay(100);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH); delay(500);
    digitalWrite(LED_BUILTIN, LOW); delay(500);
    Serial.print(".");
  }
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_BUILTIN, HIGH); delay(300);
    digitalWrite(LED_BUILTIN, LOW); delay(300);
  }
  Serial.println("WiFi connected");

  sensorId = WiFi.macAddress();
  if (sensorId == "00:00:00:00:00:00") {
    Serial.println("Error: Invalid MAC, restarting...");
    ESP.restart();
  }
  Serial.println("MAC Address: " + sensorId);
  Serial.println("POST to: " + String(backend_url));
  // Get token from backend
  if (!getMacToken()) {
    Serial.println("Failed to get token, restarting...");
    delay(5000);
    ESP.restart();
  }
  
  snprintf(mqtt_topic, sizeof(mqtt_topic), "cucumber/sensors/%s", sensorId.c_str());
  timeClient.begin();

  #if !USE_LOCAL
    espClient.setInsecure();
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

  unsigned long epochTime = timeClient.getEpochTime();
  time_t rawtime = epochTime;
  struct tm * ti = localtime(&rawtime);
  char isoTime[25];
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", ti);

  StaticJsonDocument<300> doc;
  doc["time"] = isoTime;
  doc["sensor_id"] = sensorId;
  doc["description"] = description;
  doc["token"] = token;
  doc["temp"] = temp;
  doc["hum"] = hum;
  doc["light"] = lumen;
  
  String payload;
  serializeJson(doc, payload);

  if (mqttClient.publish(mqtt_topic, payload.c_str())) {
    Serial.println("Published: " + payload);
  } else {
    Serial.println("Publish failed");
  }

  delay(10000);
}

void reconnect() {
  unsigned long start = millis();
  while (!mqttClient.connected() && (millis() - start < 30000)) {
    Serial.print("Connecting MQTT...");
    if (sensorId == "00:00:00:00:00:00" || token.isEmpty()) {
      Serial.println("Invalid sensorId/token, restarting...");
      ESP.restart();
    }
    #if USE_LOCAL
      if (mqttClient.connect(sensorId.c_str())) {
    #else
      if (mqttClient.connect(sensorId.c_str(), mqtt_user, mqtt_pass)) {
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