import os
from dotenv import load_dotenv

load_dotenv()

MQTT_BROKER = os.getenv("MQTT_BROKER")
MQTT_PORT = int(os.getenv("MQTT_PORT"))

MQTT_USERNAME = os.getenv("MQTT_USERNAME")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD")

TOPIC_VITALS = os.getenv("TOPIC_VITALS")
CLIENT_ID = os.getenv("CLIENT_ID")

MONGODB_URI = os.getenv("MONGODB_URI")