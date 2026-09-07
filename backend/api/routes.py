from fastapi import APIRouter
from database import vitals_collection
from fastapi import APIRouter, HTTPException

router = APIRouter()


@router.get("/")
def home():
    return {
        "message": "Smart Ambulance Backend Running"
    }


@router.get("/patients/{patient_id}/latest")
def latest_patient_vitals(patient_id: str):

    latest = vitals_collection.find_one({"patientId": patient_id}, sort=[("_id", -1)])

    if latest is None:
        raise HTTPException(status_code=404, detail=f"No data found for patient {patient_id}")

    latest["_id"] = str(latest["_id"])

    return latest

@router.get("/patients/{patient_id}/history")
def patient_history(patient_id: str):

    history = list(
        vitals_collection.find({"patientId": patient_id}).sort("_id", -1).limit(10))

    if len(history) == 0:
        raise HTTPException(status_code=404, detail=f"No history found for patient {patient_id}")

    for document in history:
        document["_id"] = str(document["_id"])

    return history