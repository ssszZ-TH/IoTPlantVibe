### วิธีใช้งาน App (Docker Compose + Grafana สำหรับ IoTPlantMonitor)

#### 1. **ตรวจสอบ Docker Services**
- รัน `docker-compose ps` → ดูว่า `mosquitto`, `influxdb`, `telegraf`, `grafana` ขึ้น status `Up`.
- ถ้ามี error: ดู logs ด้วย `docker-compose logs <service>` (e.g., `telegraf`).

#### 2. **ตั้งค่า InfluxDB**
- เปิด http://localhost:8086
- Login: `admin`/`admin123`
- ไป **Data Explorer** → Confirm bucket `sensors` มี data จาก MQTT (`cucumber/sensors`).
- ถ้าไม่มี: เช็ค ESP32 publish ด้วย MQTT client (e.g., MQTT Explorer) ที่ `tcp://localhost:1883`.

#### 3. **ตั้งค่า Grafana**
- เปิด http://localhost:3000
- Login: `admin`/`admin123`
- **Data Source**: InfluxDB ควร auto-add (จาก `datasource.yml`). ถ้าไม่มี:
  - Add Data Source → InfluxDB → URL: `http://influxdb:8086`, Token: `my-token-1234567890abcdef`, Org: `myorg`, Bucket: `sensors`.
- **สร้าง Dashboard**:
  - คลิก **+** > **Dashboard** > **Add new panel**.
  - Query (Flux): `from(bucket: "sensors") |> range(start: -1h) |> filter(fn: (r) => r._measurement == "mqtt_consumer")`
  - เลือก fields: `temp`, `hum`, `light` → Visualize เป็น Graph/Time Series.
  - Save dashboard (e.g., "IoTPlantMonitor").

#### 4. **ตรวจสอบ ESP32**
- Upload โค้ดจากก่อนหน้า (ตั้ง `USE_LOCAL true`, `mqtt_server` เป็น IP เครื่องรัน Docker หรือ `localhost` ถ้า ESP32 เดียวกัน).
- ดู LED: กระพริบ 2 จังหวะ (WiFi OK), 3 จังหวะ (MQTT OK).
- Serial Monitor: ดู JSON publish ทุก 10 วินาที.

#### 5. **ดู Real-time Data**
- ใน Grafana Dashboard: Refresh ทุก 10 วินาที → เห็นกราฟ temp/hum/light อัปเดต.
- ปรับ timeframe (e.g., last 1h) หรือ panel style (line/bar).

**ปัญหา?**: ถ้า data ไม่เข้า → เช็ค MQTT connection (topic `cucumber/sensors`), Telegraf logs, หรือ InfluxDB bucket. ถามมาได้เลย!