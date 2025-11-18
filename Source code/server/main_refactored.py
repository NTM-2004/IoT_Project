
from fastapi import FastAPI, WebSocket
from fastapi.templating import Jinja2Templates
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse
from datetime import datetime
import time

from config import settings
from models import get_db, ParkingSlot
from mqtt_handler import MQTTHandler
from services import websocket_service, GateService
from services import gate_service as gate_service_module
from utils import (
    start_mosquitto,
    stop_mosquitto,
    initialize_database,
    create_directories
)

# Import routes
from routes import dashboard, slots, vehicles, upload

# ==================== FastAPI App ====================
app = FastAPI(title="IoT Parking System", version="2.0.0")

# Templates và Static files
templates = Jinja2Templates(directory="templates")
create_directories()
app.mount("/static", StaticFiles(directory="static"), name="static")

# Global variables
mqtt_handler = None
gate_service = None

# ==================== MQTT Callback ====================
def handle_slot_update(data):
    """
    Callback khi nhận message slot update từ MQTT
    Message format: {"slot": "A1", "occupied": true}
    """
    print(f"[MQTT] Slot update: {data}")
    
    try:
        slot_number = data.get('slot')
        is_occupied = data.get('occupied', False)
        
        if not slot_number:
            print("[MQTT] ⚠ Missing slot number")
            return
        
        # Cập nhật database
        db = next(get_db())
        try:
            slot = db.query(ParkingSlot).filter(ParkingSlot.slot_number == slot_number).first()
            
            if slot:
                slot.is_occupied = is_occupied
                slot.last_updated = datetime.utcnow()
            else:
                # Tạo mới nếu chưa có
                slot = ParkingSlot(
                    slot_number=slot_number,
                    is_occupied=is_occupied
                )
                db.add(slot)
            
            db.commit()
            print(f"[DATABASE] ✓ Slot {slot_number} -> {'OCCUPIED' if is_occupied else 'FREE'}")
            
            # Thêm vào queue để broadcast
            message = {
                'type': 'slot_update',
                'slot': slot_number,
                'occupied': is_occupied,
                'timestamp': datetime.utcnow().isoformat()
            }
            websocket_service.queue_broadcast(message)
        
        finally:
            db.close()
    
    except Exception as e:
        print(f"[MQTT] ✗ Error handling slot update: {e}")

# ==================== Startup & Shutdown ====================
@app.on_event("startup")
async def startup_event():
    """Khởi tạo khi server start"""
    global mqtt_handler, gate_service
    
    print("=" * 50)
    print("Starting IoT Parking System Server")
    print("=" * 50)
    
    # Khởi tạo database
    initialize_database()
    
    # Khởi động WebSocket broadcast worker
    websocket_service.start_worker()
    
    # Khởi tạo MQTT Handler
    print(f"[MQTT] Attempting to connect to: {settings.MQTT_BROKER}:{settings.MQTT_PORT}")
    
    is_local_broker = settings.MQTT_BROKER in ["localhost", "127.0.0.1", "0.0.0.0"]
    
    mqtt_handler = MQTTHandler(on_slot_update=handle_slot_update)
    if mqtt_handler.connect():
        print("✓ MQTT Handler connected")
    else:
        print("⚠ MQTT Handler failed")
        
        if is_local_broker:
            print("[MQTT] Broker is localhost, trying to start Mosquitto...")
            
            if start_mosquitto():
                print("[MOSQUITTO] ✓ Broker started, retrying connection...")
                
                time.sleep(2)
                if mqtt_handler.connect():
                    print("✓ MQTT Handler connected (after starting Mosquitto)")
                else:
                    print("⚠ MQTT Handler still failed (continuing without MQTT)")
            else:
                print("⚠ Could not start Mosquitto (continuing without MQTT)")
        else:
            print(f"⚠ Broker is remote ({settings.MQTT_BROKER}), make sure it's running")
            print("⚠ Continuing without MQTT...")
    
    # Khởi tạo Gate Service
    gate_service = GateService(mqtt_handler=mqtt_handler)
    gate_service_module.gate_service = gate_service  # Set global instance
    print("✓ Gate Service initialized")
    
    print("=" * 50)
    print(f"Server running at http://{settings.SERVER_HOST}:{settings.SERVER_PORT}")
    print(f"Dashboard: http://{settings.SERVER_HOST}:{settings.SERVER_PORT}/dashboard")
    print(f"API Docs: http://{settings.SERVER_HOST}:{settings.SERVER_PORT}/docs")
    print("=" * 50)

@app.on_event("shutdown")
async def shutdown_event():
    """Cleanup khi server stop"""
    # Dừng WebSocket worker
    await websocket_service.stop_worker()
    
    # Ngắt kết nối MQTT
    if mqtt_handler:
        mqtt_handler.disconnect()
    
    # Dừng Mosquitto
    stop_mosquitto()
    
    print("✓ Server shutdown complete")

# ==================== Root Route ====================
@app.get("/", response_class=HTMLResponse)
async def root():
    """Root endpoint"""
    return """
    <html>
        <head><title>IoT Parking System</title></head>
        <body>
            <h1>🚗 IoT Parking System API</h1>
            <p>Server is running!</p>
            <ul>
                <li><a href="/dashboard">Dashboard</a></li>
                <li><a href="/docs">API Documentation</a></li>
                <li><a href="/api/slots">Get Parking Slots</a></li>
                <li><a href="/api/vehicles">Get Vehicle Logs</a></li>
            </ul>
        </body>
    </html>
    """

# ==================== WebSocket Route ====================
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """WebSocket endpoint cho realtime updates"""
    await websocket_service.connect(websocket)
    
    try:
        while True:
            await websocket.receive_text()
    except:
        pass
    finally:
        websocket_service.disconnect(websocket)

# ==================== Include Routers ====================
app.include_router(dashboard.router)
app.include_router(slots.router)
app.include_router(vehicles.router)
app.include_router(upload.router)

# ==================== Run ====================
if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        "main:app",
        host=settings.SERVER_HOST,
        port=settings.SERVER_PORT,
        reload=True
    )
