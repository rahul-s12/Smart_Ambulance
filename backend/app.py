from fastapi.middleware.cors import CORSMiddleware
from contextlib import asynccontextmanager
import threading

from fastapi import FastAPI

from api.routes import router
from mqtt_client import start_mqtt, stop_mqtt

@asynccontextmanager
async def lifespan(app: FastAPI):

    mqtt_thread = threading.Thread(target=start_mqtt, daemon=True)

    mqtt_thread.start()

    print("MQTT Subscriber Started")

    yield

    stop_mqtt()

    print("MQTT Subscriber Stopped")

app = FastAPI(title="Smart Ambulance API", version="1.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=[
        "http://localhost:5173",
        "http://127.0.0.1:5173",
    ],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(router)