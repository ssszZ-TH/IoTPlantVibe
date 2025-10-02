### สรุปข้อมูลจากแหล่งต่างๆ (จาก web search)
- **Grafana Cloud + MQTT**: Grafana Cloud รองรับ MQTT ผ่าน plugin "MQTT Datasource" สำหรับ real-time streaming (ไม่เก็บ history). สำหรับ history ต้องใช้ time-series DB เช่น InfluxDB Cloud (free tier มี) แล้ว connect Grafana ไปแสดง.
- **HiveMQ Cloud**: เป็น MQTT broker cloud (free tier 10k msg/day, MQTT 5 รองรับ). ใช้ส่งข้อมูลจาก ESP32 ได้ง่าย รองรับ TLS/secure. HiveMQ สามารถ forward ข้อมูลไป InfluxDB ด้วย Telegraf หรือ Node-RED.
- **ESP32 + MQTT**: ใช้ library PubSubClient (Arduino) publish JSON ไป broker. ตัวอย่างจาก HiveMQ tutorial: ESP32 publish sensor data ไป HiveMQ แล้ว visualize ใน Grafana ผ่าน InfluxDB.
- **MQTT 5**: HiveMQ รองรับ แต่ PubSubClient ยัง MQTT 3.1.1 (พอใช้). ถ้าต้องการ 5 ใช้ library อื่นอย่าง HiveMQ Client (แต่ซับซ้อนกว่า).
- **ระบบทั่วไป**: ESP32 → MQTT broker (HiveMQ) → Telegraf/Node-RED → InfluxDB Cloud → Grafana Cloud dashboard. Free tier พอสำหรับ test.

### Design ระบบ (IoT Sensor to Grafana)
ระบบแบบ end-to-end สำหรับ Cucumber Board (ESP32-S2) ส่ง temp/hum/light ทุก 10 วิ:

1. **Device Layer**: Cucumber + DHT11 + Built-in light sensor → อ่านค่า + WiFi + NTP → Publish JSON ไป MQTT topic (e.g., "cucumber/sensors").
2. **Messaging Layer**: HiveMQ Cloud (broker) รับ MQTT msg.
3. **Data Layer**: Telegraf (agent) subscribe MQTT → parse JSON → insert InfluxDB Cloud (time-series DB).
4. **Visualization Layer**: Grafana Cloud connect InfluxDB → สร้าง dashboard (graph temp/hum/light over time).

**Diagram (text-based)**:
```
Cucumber (ESP32) --MQTT JSON--> HiveMQ Cloud (Broker)
                           |
                           v
Telegraf (on VPS/RPi) --InfluxDB Line Protocol--> InfluxDB Cloud
                           |
                           v
Grafana Cloud <--Query--> Dashboard (real-time + history)
```

**ข้อดี**: Scalable, secure (TLS), low-cost (free tier). ถ้า data เยอะ ใช้ paid HiveMQ.

### สอนทีละขั้นตอน (เริ่มจาก blank)
#### 1. Setup HiveMQ Cloud (MQTT Broker)
- ไป https://console.hivemq.cloud → Sign up (free).
- Create cluster (e.g., "cucumber-cluster").
- ได้ credentials: Host (e.g., xxxxx.s1.eu.hivemq.cloud), Port 8883 (TLS), Username/Password.
- Test: ใช้ HiveMQ Web Client (ใน console) subscribe topic "cucumber/sensors" เพื่อดู msg.

#### 2. Setup InfluxDB Cloud (DB)
- ไป https://cloud.influxdata.com → Sign up (free, 5MB storage).
- Create bucket (e.g., "cucumber-bucket").
- ได้ Token (API key) สำหรับ write data.
- Note: Organization ID, Bucket name ไว้ใช้ใน Telegraf.

#### 3. Setup Grafana Cloud (Dashboard)
- ไป https://grafana.com/products/cloud/ → Sign up (free, 10k metrics).
- Add data source: InfluxDB → ใส่ URL (จาก InfluxDB), Token, Org, Bucket.
- สร้าง dashboard: Add panel → Query InfluxDB (e.g., SELECT temp FROM sensors WHERE time > now() - 1h) → เลือก graph.

#### 4. Setup Telegraf (Bridge MQTT to InfluxDB)
- ใช้ VPS/RPi (e.g., Raspberry Pi) ติดตั้ง Telegraf (InfluxData tool).
- Install: `sudo apt install telegraf` (Ubuntu).
- Config `/etc/telegraf/telegraf.conf`:
  ```
  [[inputs.mqtt_consumer]]
    servers = ["tls://xxxxxx.s1.eu.hivemq.cloud:8883"]
    topics = ["cucumber/sensors"]
    username = "your-username"
    password = "your-password"
    data_format = "json"
    json_string_fields = ["time"]

  [[outputs.influxdb_v2]]
    urls = ["https://your-region.influxdb.io:8086"]
    token = "your-influxdb-token"
    organization = "your-org"
    bucket = "cucumber-bucket"
  ```
- Restart: `sudo systemctl restart telegraf`.
- Telegraf จะ subscribe MQTT, parse JSON (time/temp/hum/light), ส่งไป InfluxDB.

#### 5. Code สำหรับ Cucumber (ESP32) - แก้จากเดิม
ใช้ PubSubClient lib (install ใน Arduino IDE: Manage Libraries > "PubSubClient by Nick O'Leary").
เปลี่ยน delay เป็น 10000 ms. Publish JSON ไป HiveMQ.

```cpp
#include <DHT.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>  // MQTT lib

#define DHTPIN 18
#define DHTTYPE DHT11
#define LIGHT_SENSOR 4
#define LED_BUILTIN 2

// WiFi
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// HiveMQ Cloud
const char* mqtt_server = "xxxxxx.s1.eu.hivemq.cloud";  // จาก console
const int mqtt_port = 8883;
const char* mqtt_user = "your-username";
const char* mqtt_pass = "your-password";
const char* mqtt_topic = "cucumber/sensors";

DHT dht(DHTPIN, DHTTYPE);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000);  // UTC+7
WiFiClientSecure espClient;  // สำหรับ TLS
PubSubClient mqttClient(espClient);

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  dht.begin();
  
  // WiFi connect (กระพริบ LED)
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH); delay(500);
    digitalWrite(LED_BUILTIN, LOW); delay(500);
    Serial.print(".");
  }
  // Connected: กระพริบ 2 จังหวะ
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_BUILTIN, HIGH); delay(300);
    digitalWrite(LED_BUILTIN, LOW); delay(300);
  }
  Serial.println("WiFi connected");

  // NTP
  timeClient.begin();

  // MQTT setup
  espClient.setInsecure();  // สำหรับ test (ไม่แนะนำ production; ใช้ CA cert)
  mqttClient.setServer(mqtt_server, mqtt_port);
  while (!mqttClient.connected()) {
    Serial.print("Connecting MQTT...");
    if (mqttClient.connect("CucumberClient", mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      // กระพริบ LED 3 จังหวะ confirm
      for (int i = 0; i < 3; i++) { digitalWrite(LED_BUILTIN, HIGH); delay(200); digitalWrite(LED_BUILTIN, LOW); delay(200); }
    } else {
      Serial.print("failed, rc="); Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

void loop() {
  if (!mqttClient.connected()) { reconnect(); }  // Reconnect if lost
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

  // JSON
  StaticJsonDocument<200> doc;
  doc["time"] = isoTime;
  doc["temp"] = temp;
  doc["hum"] = hum;
  doc["light"] = lumen;

  String payload;
  serializeJson(doc, payload);

  // Publish
  mqttClient.publish(mqtt_topic, payload.c_str());
  Serial.println("Published: " + payload);

  delay(10000);  // ทุก 10 วิ
}

void reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Reconnect MQTT...");
    if (mqttClient.connect("CucumberClient", mqtt_user, mqtt_pass)) {
      Serial.println("reconnected");
    } else {
      Serial.print("failed, rc="); Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}
```

#### 6. Test & Debug
- Upload code → ดู Serial Monitor: เช็ค WiFi/MQTT connect.
- ใน HiveMQ console: Subscribe "cucumber/sensors" → เห็น JSON.
- ใน InfluxDB: Query data → เห็น temp/hum/light.
- ใน Grafana: สร้าง panel → ดู graph real-time.

**ปัญหาที่พบบ่อย**: TLS error (ใช้ setInsecure() test), WiFi unstable (เพิ่ม reconnect). ถ้าติดขัด ถามเพิ่ม! 🚀