### Tech Stack ที่ใช้
- **เดิม**: MQTT (HiveMQ Cloud), Telegraf (standalone on VPS/RPi), InfluxDB Cloud, Grafana Cloud.
- **ใหม่ (Local Docker Compose)**: MQTT (Eclipse Mosquitto - local broker), Telegraf (Docker), InfluxDB (Docker, v2), Grafana (Docker). **เปลี่ยน**: ทุกอย่าง local (no cloud), ใช้ Mosquitto แทน HiveMQ (ง่ายกว่า, MQTT 3.1.1 พอใช้แทน 5). Telegraf config สำหรับ subscribe MQTT + write InfluxDB. ไม่ต้อง setup cloud account.

**ข้อดี**: Run local บนเครื่องเดียว (e.g., PC/RPi), free, offline, test เร็ว. Data เก็บใน volumes (persist).

### สอนทีละขั้นตอน (Local Setup)
#### 1. Prep
- Install Docker + Docker Compose (ถ้ายังไม่มี): https://docs.docker.com/compose/install/
- สร้าง folder project: `mkdir iot-local && cd iot-local`
- สร้าง `docker-compose.yml` (copy code ด้านล่าง).
- สร้าง `telegraf.conf` (copy code ด้านล่าง).
- สำหรับ Grafana provisioning (auto add datasource): สร้าง folder `grafana-provisioning/datasources/` แล้วใส่ `datasource.yml` (code ด้านล่าง).

#### 2. docker-compose.yml
```yaml
version: '3.8'
services:
  mosquitto:
    image: eclipse-mosquitto:2.0
    container_name: mosquitto
    ports:
      - "1883:1883"  # MQTT port
    volumes:
      - ./mosquitto.conf:/mosquitto/config/mosquitto.conf  # Optional config (allow anonymous)
    restart: always

  influxdb:
    image: influxdb:2.7
    container_name: influxdb
    ports:
      - "8086:8086"
    environment:
      - DOCKER_INFLUXDB_INIT_MODE=setup
      - DOCKER_INFLUXDB_INIT_USERNAME=admin
      - DOCKER_INFLUXDB_INIT_PASSWORD=admin123
      - DOCKER_INFLUXDB_INIT_ORG=myorg
      - DOCKER_INFLUXDB_INIT_BUCKET=sensors
      - DOCKER_INFLUXDB_INIT_ADMIN_TOKEN=my-token-1234567890abcdef
    volumes:
      - influxdb-data:/var/lib/influxdb2
      - influxdb-config:/etc/influxdb2
    restart: always

  telegraf:
    image: telegraf:1.28
    container_name: telegraf
    depends_on:
      - mosquitto
      - influxdb
    volumes:
      - ./telegraf.conf:/etc/telegraf/telegraf.conf:ro
    restart: always

  grafana:
    image: grafana/grafana:10.2.0
    container_name: grafana
    ports:
      - "3000:3000"
    environment:
      - GF_SECURITY_ADMIN_PASSWORD=admin123
    volumes:
      - grafana-data:/var/lib/grafana
      - ./grafana-provisioning:/etc/grafana/provisioning
    depends_on:
      - influxdb
    restart: always

volumes:
  influxdb-data:
  influxdb-config:
  grafana-data:
```

#### 3. telegraf.conf (MQTT input + InfluxDB output)
```toml
[agent]
  interval = "10s"
  round_interval = true

[[outputs.influxdb_v2]]
  urls = ["http://influxdb:8086"]
  token = "my-token-1234567890abcdef"
  organization = "myorg"
  bucket = "sensors"

[[inputs.mqtt_consumer]]
  servers = ["tcp://mosquitto:1883"]
  topics = ["cucumber/sensors"]
  data_format = "json"
  json_string_fields = ["time"]
  data_type = "string"
```

#### 4. mosquitto.conf (Optional, allow anonymous connect)
```
allow_anonymous true
listener 1883
```

#### 5. grafana-provisioning/datasources/datasource.yml (Auto add InfluxDB datasource)
```yaml
apiVersion: 1
datasources:
  - name: InfluxDB
    type: influxdb
    access: proxy
    url: http://influxdb:8086
    jsonData:
      version: Flux
      organization: myorg
      defaultBucket: sensors
      tlsSkipVerify: true
    secureJsonData:
      token: my-token-1234567890abcdef
```

#### 6. Run System
- `docker-compose up -d` (run background).
- เช็ค logs: `docker-compose logs -f`.
- Setup InfluxDB: เปิด http://localhost:8086 → login admin/admin123 → confirm org/bucket/token.
- Grafana: http://localhost:3000 → login admin/admin123 → datasource auto add → สร้าง dashboard (query Flux: `from(bucket: "sensors") |> range(start: -1h) |> filter(fn: (r) => r._measurement == "cucumber")` สำหรับ temp/hum/light).

#### 7. Code ESP32 (แก้ MQTT server เป็น local)
- เปลี่ยน `mqtt_server = "localhost"` (หรือ IP เครื่อง run Docker), `mqtt_port = 1883`, no username/password (anonymous).
- Topic: "cucumber/sensors".
- Upload → Test: Publish JSON → เช็ค Telegraf logs + Grafana graph.

**ปัญหา?**: ถ้า Telegraf error (connection refused), เช็ค network (ใช้ service name e.g., influxdb). ถ้าต้องการ dashboard JSON export บอกมา! 🚀