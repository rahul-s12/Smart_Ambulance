from database import vitals_collection

try:
    doc = {
        "patientId": "TEST",
        "heartRate": 75
    }

    result = vitals_collection.insert_one(doc)

    print("Success")
    print(result.inserted_id)

except Exception as e:
    print(e)