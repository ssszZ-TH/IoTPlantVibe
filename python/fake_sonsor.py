import paho.mqtt.client as mqtt
import json
import time
import random
from datetime import datetime, timezone

# Config
SENSOR_ID = "cucumber_1"  # Change to 2, 3, etc.
USE_LOCAL = True

# MQTT Config
if USE_LOCAL:
    MQTT_SERVER = "localhost"  # Local Mosquitto IP
    MQTT_PORT = 1883
    MQTT_USER = ""
    MQTT_PASS = ""
else:
    MQTT_SERVER = "xxxxxx.s1.eu.hivemq.cloud"  # HiveMQ server
    MQTT_PORT = 8883
    MQTT_USER = "your-username"
    MQTT_PASS = "your-password"

MQTT_TOPIC = f"cucumber/sensors/{SENSOR_ID}"

# MQTT Client
client = mqtt.Client(client_id=SENSOR_ID)
if not USE_LOCAL:
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.tls_set(tls_version=mqtt.ssl.PROTOCOL_TLS)
    client.tls_insecure_set(True)  # Test; use CA cert in production

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT")
    else:
        print(f"Connection failed, rc={rc}")

client.on_connect = on_connect
client.connect(MQTT_SERVER, MQTT_PORT, 60)

# Main loop
client.loop_start()
while True:
    # Mock data
    temp = round(random.uniform(20.0, 30.0), 1)  # Random temp 20-30°C
    hum = round(random.uniform(40.0, 80.0), 1)   # Random humidity 40-80%
    lux = random.randint(0, 1000)                # Random lux 0-1000
    lumen = round(lux * 0.0079, 2)               # Convert to lumen

    # ISO time
    iso_time = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    # JSON payload
    payload = {
        "time": iso_time,
        "sensor_id": SENSOR_ID,
        "temp": temp,
        "hum": hum,
        "light": lumen
    }

    # Publish
    result = client.publish(MQTT_TOPIC, json.dumps(payload))
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        print(f"Published: {json.dumps(payload)}")
    else:
        print("Publish failed")

    time.sleep(10)  # Every 10 seconds

client.loop_stop()