from pymongo import MongoClient
from config import MONGODB_URI

# Connect to MongoDB Atlas
client = MongoClient(MONGODB_URI)

# Database
db = client["SmartAmbulanceDB"]

# Collection
vitals_collection = db["patient_vitals"]

print("Connected to MongoDB Atlas")
