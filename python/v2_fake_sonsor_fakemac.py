import paho.mqtt.client as mqtt
import time
import json
from datetime import datetime
import random

server_ip = "localhost"  # Change to your MQTT broker IP

# Fake Cucumber Config
USE_LOCAL = True
MQTT_SERVER = server_ip if USE_LOCAL else "xxxxxx.s1.eu.hivemq.cloud"
MQTT_PORT = 1883 if USE_LOCAL else 8883
MQTT_USER = "" if USE_LOCAL else "your-username"
MQTT_PASS = "" if USE_LOCAL else "your-password"
FAKE_MAC = "24:0A:C4:12:34:56"  # Fake MAC address
MQTT_TOPIC = f"cucumber/sensors/{FAKE_MAC}"

# MQTT Client
client = mqtt.Client(client_id=FAKE_MAC)

# Set credentials for cloud
if not USE_LOCAL:
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.tls_set(tls_version=mqtt.ssl.PROTOCOL_TLS)
    client.tls_insecure_set(True)  # For testing

# Connect to MQTT
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT")
    else:
        print(f"Connect failed, rc={rc}")

client.on_connect = on_connect
client.connect(MQTT_SERVER, MQTT_PORT, 60)

# Simulate sensor data
def get_sensor_data():
    return {
        "time": datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ"),
        "sensor_id": FAKE_MAC,
        "temp": round(random.uniform(20.0, 30.0), 1),  # Fake temp
        "hum": round(random.uniform(40.0, 80.0), 1),   # Fake humidity
        "light": round(random.uniform(0.0, 7.9), 2)     # Fake lumen
    }

# Main loop
client.loop_start()
while True:
    data = get_sensor_data()
    payload = json.dumps(data)
    result = client.publish(MQTT_TOPIC, payload)
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        print(f"Published: {payload}")
    else:
        print("Publish failed")
    time.sleep(10)