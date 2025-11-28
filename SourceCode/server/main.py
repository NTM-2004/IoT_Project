from fastapi import FastAPI, WebSocket, Depends
from fastapi.templating import Jinja2Templates
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse
import os
import time

from config import settings
from models import init_db

# Import services
from services.websocket_service import websocket_service
from services.mosquitto_service import mosquitto_service
from services.slot_update_service import SlotUpdateService, slot_update_service as _slot_service
from services.gate_service import gate_service
from services import ota_service

# Import routes
from routes import dashboard, slots, vehicles, upload

# Import MQTT handler
from services.mqtt_handler import MQTTHandler

# FastAPI App
app = FastAPI(
    title="IoT Parking System",
    version="3",
    description="Parking management system with OCR and real-time monitoring"
)

# Templates và Static Files
templates = Jinja2Templates(directory="templates")

# Tạo thư mục cần thiết
os.makedirs("static", exist_ok=True)
os.makedirs(settings.UPLOAD_DIR, exist_ok=True)
os.makedirs(settings.TEMP_DIR, exist_ok=True)
os.makedirs(settings.ARCHIVE_DIR, exist_ok=True)

app.mount("/static", StaticFiles(directory="static"), name="static")

# Global State
mqtt_handler = None

# Startup & Shutdown Events
@app.on_event("startup")
async def startup_event():
    # Khởi tạo khi server start
    global mqtt_handler, _slot_service
    
    print("=" * 50)
    print("Starting IoT Parking System Server")
    print("=" * 50)
    
    # Khởi tạo database
    try:
        init_db()
        print("[DATABASE] SUCCESS Database initialized")
    except Exception as e:
        print(f"[DATABASE] ERROR Database error: {e}")
    
    # Khởi động WebSocket broadcast worker
    websocket_service.start_worker()
    
    # Khởi tạo slot update service với websocket callback
    _slot_service = SlotUpdateService(websocket_callback=websocket_service.queue_broadcast)
    
    # Khởi tạo MQTT Handler
    print(f"[MQTT] Attempting to connect to: {settings.MQTT_BROKER}:{settings.MQTT_PORT}")
    
    is_local_broker = settings.MQTT_BROKER in ["localhost", "127.0.0.1", "0.0.0.0"]
    
    mqtt_handler = MQTTHandler(on_slot_update=_slot_service.handle_slot_update)
    
    if mqtt_handler.connect():
        print("[MQTT] SUCCESS MQTT Handler connected")
    else:
        print("[MQTT] ERROR MQTT Handler failed")
        
        # Chỉ thử khởi động Mosquitto nếu broker là localhost
        if is_local_broker:
            print("[MQTT] Broker is localhost, trying to start Mosquitto...")
            
            if mosquitto_service.start():
                print("[MOSQUITTO] Broker started, retrying connection...")
                
                # Thử kết nối lại sau khi khởi động broker
                time.sleep(2)
                if mqtt_handler.connect():
                    print("[MQTT] SUCCESS MQTT Handler connected")
                else:
                    print("[MQTT] ERROR MQTT Handler still failed")
            else:
                print("[MQTT] ERRO Could not start Mosquitto")
        else:
            print("[MQTT] CRITICAL ERROR Continuing without MQTT...")
    
    # Cấu hình Gate Service với MQTT handler
    gate_service.mqtt_handler = mqtt_handler
    print("[GATE] SUCCESS Gate service configured")
    
    # Khởi tạo OTA Service
    if ota_service.init_mqtt():
        print("[OTA] SUCCESS OTA service initialized")
    else:
        print("[OTA] WARNING Running without OTA MQTT")
    
    print("=" * 50)
    print(f"Server running at http://{settings.SERVER_HOST}:{settings.SERVER_PORT}")
    print(f"OTA Dashboard: http://{settings.SERVER_HOST}:{settings.SERVER_PORT}/ota (Super Admin only)")
    print(f"API Docs: http://{settings.SERVER_HOST}:{settings.SERVER_PORT}/docs")
    print("=" * 50)

@app.on_event("shutdown")
async def shutdown_event():
    
    # Cleanup khi server stop
    if mqtt_handler:
        mqtt_handler.disconnect()
    
    # Dừng OTA service
    ota_service.shutdown_mqtt()
    
    # Dừng Mosquitto nếu được khởi động bởi server
    mosquitto_service.stop()
    
    # Dừng WebSocket worker
    await websocket_service.stop_worker()
    
    print("[SERVER] Server shutdown complete")

# Include Routers
app.include_router(dashboard.router)
app.include_router(slots.router)
app.include_router(vehicles.router)
app.include_router(upload.router)
app.include_router(ota_service.router)  # OTA Update Service

# Root Endpoint - Redirect to public dashboard
@app.get("/api-info", response_class=HTMLResponse)
async def api_info():
    # API info endpoint
    return """
    <html>
        <head><title>IoT Parking System - API</title></head>
        <body style="font-family: Arial, sans-serif; max-width: 800px; margin: 50px auto; padding: 20px;">
            <h1>Parking System API</h1>
            <p>Server is running!</p>
            <h2>Available Endpoints:</h2>
            <ul style="line-height: 2;">
                <li><a href="/">Public Dashboard</a> - Real-time parking status (No auth required)</li>
                <li><a href="/admin">Admin Dashboard</a> - Management & control (Auth required)</li>
                <li><a href="/ota">OTA Dashboard</a> - Firmware updates (Super Admin only)</li>
                <li><a href="/login">Admin Login</a> - Login page</li>
                <li><a href="/docs">API Documentation</a> - Interactive API docs</li>
                <li><a href="/api/slots">Get Parking Slots</a> - JSON API</li>
                <li><a href="/api/vehicles">Get Vehicle Logs</a> - JSON API</li>
            </ul>
            <h2>System Info:</h2>
            <ul>
                <li>Version: 3.1</li>
                <li>Architecture: Routes + Services</li>
                <li>WebSocket: Enabled</li>
                <li>MQTT: Auto-start Mosquitto</li>
                <li>Authentication: Session-based</li>
            </ul>
        </body>
    </html>
    """

# WebSocket Endpoint
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    
    # WebSocket endpoint cho realtime updates
    await websocket_service.connect(websocket)
    
    try:
        while True:
            await websocket.receive_text()
    except:
        pass
    finally:
        websocket_service.disconnect(websocket)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        "main:app",
        host=settings.SERVER_HOST,
        port=settings.SERVER_PORT,
        reload=True
    )
