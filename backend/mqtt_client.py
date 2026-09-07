import json
import ssl
from datetime import datetime, timezone

import paho.mqtt.client as mqtt

from config import *
from database import vitals_collection


def on_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
        print("Connected to EMQX")

        client.subscribe(TOPIC_VITALS)
        print(f"Subscribed to: {TOPIC_VITALS}")

    else:
        print("Connection Failed:", reason_code)


def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())

    except json.JSONDecodeError:
        print("Invalid JSON received")
        return

    # Add timestamp
    data["receivedTimestamp"] = datetime.now(timezone.utc).isoformat()

    try:
        result = vitals_collection.insert_one(data)

        print("\n===================================")
        print("Inserted into MongoDB Successfully")
        print("Document ID:", result.inserted_id)
        print("===================================")

        print(data.get("patientId"))
        print(f"Ambulance ID      : {data['ambulanceId']}")
        print(f"Heart Rate        : {data['heartRate']} bpm")
        print(f"SpO2              : {data['spo2']} %")
        print(f"Temperature       : {data['temperature']} °C")
        print(f"Received Time UTC : {data['receivedTimestamp']}")

    except Exception as e:
        print("MongoDB Error:")
        print(e)


client = mqtt.Client(
    mqtt.CallbackAPIVersion.VERSION2,
    client_id=CLIENT_ID
)

client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

client.tls_set(ca_certs="emqx-ca.crt", cert_reqs=ssl.CERT_REQUIRED)

client.on_connect = on_connect
client.on_message = on_message

def start_mqtt():
    client.connect(MQTT_BROKER, MQTT_PORT)
    client.loop_forever()


def stop_mqtt():
    client.disconnect()