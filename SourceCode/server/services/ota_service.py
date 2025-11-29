"""
OTA Update Service Module
Provides OTA firmware update functionality as a service
"""
from fastapi import APIRouter, File, UploadFile, Request, Cookie
from fastapi.responses import HTMLResponse, FileResponse, JSONResponse, RedirectResponse
from fastapi.templating import Jinja2Templates
import paho.mqtt.client as mqtt
import os
import json
import hashlib
from datetime import datetime
from config import settings
from typing import Optional
from session_manager import verify_super_admin

# Ensure firmware directory exists
os.makedirs(settings.FIRMWARE_DIR, exist_ok=True)

# Router for OTA endpoints
router = APIRouter(prefix="/ota", tags=["OTA"])
templates = Jinja2Templates(directory="templates")

# MQTT Client
mqtt_client = None

# Danh sách thiết bị
DEVICES = {
    "NODE_01": {
        "name": "NODE Parking Slot 01",
        "topic": "iot/parking/node/01/ota",
    },
    "NODE_02": {
        "name": "NODE Parking Slot 02", 
        "topic": "iot/parking/node/02/ota",
    },
    "NODE_03": {
        "name": "NODE Parking Slot 03",
        "topic": "iot/parking/node/03/ota",
    },
    "GATE": {
        "name": "GATE Controller",
        "topic": "iot/parking/gate/ota",
    },
    "CAM_IN": {
        "name": "Camera IN",
        "topic": "iot/parking/cam_in/ota",
    },
    "CAM_OUT": {
        "name": "Camera OUT",
        "topic": "iot/parking/cam_out/ota",
    },
    "MONITOR": {
        "name": "Monitor Display",
        "topic": "iot/parking/monitor/ota",
    }
}

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[OTA MQTT] Connected to broker: {settings.MQTT_BROKER}:{settings.MQTT_PORT}")
    else:
        print(f"[OTA MQTT] Connection failed with code {rc}")

def on_publish(client, userdata, mid):
    print(f"[OTA MQTT] Message published (mid: {mid})")

def init_mqtt():
    # Initialize MQTT client for OTA service
    global mqtt_client
    mqtt_client = mqtt.Client(client_id="ota_service")
    mqtt_client.on_connect = on_connect
    mqtt_client.on_publish = on_publish
    
    # Set authentication if available
    if settings.MQTT_USERNAME and settings.MQTT_USERNAME.strip():
        mqtt_client.username_pw_set(settings.MQTT_USERNAME, settings.MQTT_PASSWORD)
        print(f"[OTA MQTT] Using authentication with username: {settings.MQTT_USERNAME}")
    
    try:
        mqtt_client.connect(settings.MQTT_BROKER, settings.MQTT_PORT, 60)
        mqtt_client.loop_start()
        print("[OTA MQTT] SUCCESS - MQTT initialized")
        return True
    except Exception as e:
        print(f"[OTA MQTT] ERROR: {e}")
        return False

def shutdown_mqtt():
    global mqtt_client
    if mqtt_client:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        print("[OTA MQTT] Shutdown complete")

def calculate_md5(file_path):
    md5_hash = hashlib.md5()
    with open(file_path, "rb") as f:
        # Read file in chunks to handle large files
        for chunk in iter(lambda: f.read(4096), b""):
            md5_hash.update(chunk)
    return md5_hash.hexdigest()

# OTA ROUTES

@router.get("/", response_class=HTMLResponse)
async def ota_dashboard(request: Request, session_id: Optional[str] = Cookie(None)):
    """OTA Dashboard - Super Admin Only"""
    if not verify_super_admin(session_id):
        return RedirectResponse(url="/login", status_code=303)
    
    return templates.TemplateResponse("ota_dashboard.html", {
        "request": request,
        "devices": DEVICES
    })

@router.get("/api/devices")
async def get_devices(session_id: Optional[str] = Cookie(None)):
    # Get list of devices - Super Admin Only
    if not verify_super_admin(session_id):
        return JSONResponse(status_code=401, content={"success": False, "error": "Unauthorized"})
    
    return {
        "success": True,
        "devices": DEVICES
    }

@router.get("/api/firmware/list")
async def list_firmware(session_id: Optional[str] = Cookie(None)):
    # List available firmware files - Super Admin Only
    if not verify_super_admin(session_id):
        return JSONResponse(status_code=401, content={"success": False, "error": "Unauthorized"})
    
    firmware_files = []
    
    for filename in os.listdir(settings.FIRMWARE_DIR):
        if filename.endswith('.bin'):
            filepath = os.path.join(settings.FIRMWARE_DIR, filename)
            stat = os.stat(filepath)
            md5_hash = calculate_md5(filepath)
            firmware_files.append({
                "filename": filename,
                "size": stat.st_size,
                "size_kb": round(stat.st_size / 1024, 2),
                "modified": datetime.fromtimestamp(stat.st_mtime).strftime("%Y-%m-%d %H:%M:%S"),
                "md5": md5_hash
            })
    
    return {
        "success": True,
        "firmware_files": firmware_files,
        "count": len(firmware_files)
    }

@router.post("/api/firmware/upload")
async def upload_firmware(file: UploadFile = File(...), session_id: Optional[str] = Cookie(None)):
    # Upload firmware file - Super Admin Only
    if not verify_super_admin(session_id):
        return JSONResponse(status_code=401, content={"success": False, "error": "Unauthorized"})
    
    try:
        # Check file extension
        if not file.filename.endswith('.bin'):
            return JSONResponse(
                status_code=400,
                content={
                    "success": False,
                    "error": "Only .bin files are allowed"
                }
            )
        
        # Save file
        file_path = os.path.join(settings.FIRMWARE_DIR, file.filename)
        
        with open(file_path, 'wb') as f:
            content = await file.read()
            f.write(content)
        
        file_size = len(content)
        
        # Calculate MD5 hash
        md5_hash = calculate_md5(file_path)
        
        print(f"[OTA FIRMWARE] Uploaded: {file.filename} ({file_size} bytes)")
        print(f"[OTA FIRMWARE] MD5: {md5_hash}")
        
        return {
            "success": True,
            "filename": file.filename,
            "size": file_size,
            "size_kb": round(file_size / 1024, 2),
            "md5": md5_hash,
            "path": file_path
        }
    
    except Exception as e:
        print(f"[OTA FIRMWARE] Upload error: {e}")
        return JSONResponse(
            status_code=500,
            content={
                "success": False,
                "error": str(e)
            }
        )

@router.post("/api/trigger")
async def trigger_ota_update(request: Request, session_id: Optional[str] = Cookie(None)):
    """Trigger OTA update for a device - Super Admin Only"""
    if not verify_super_admin(session_id):
        return JSONResponse(status_code=401, content={"success": False, "error": "Unauthorized"})
    
    try:
        data = await request.json()
        device_id = data.get("device_id")
        firmware_file = data.get("firmware_file")
        
        if not device_id or device_id not in DEVICES:
            return JSONResponse(
                status_code=400,
                content={
                    "success": False,
                    "error": "Invalid device_id"
                }
            )
        
        if not firmware_file:
            return JSONResponse(
                status_code=400,
                content={
                    "success": False,
                    "error": "No firmware file specified"
                }
            )
        
        # Check if firmware file exists
        firmware_path = os.path.join(settings.FIRMWARE_DIR, firmware_file)
        if not os.path.exists(firmware_path):
            return JSONResponse(
                status_code=404,
                content={
                    "success": False,
                    "error": "Firmware file not found"
                }
            )
        
        device = DEVICES[device_id]
        topic = device["topic"]
        
        # Get firmware info
        file_size = os.path.getsize(firmware_path)
        
        # Calculate MD5 hash
        md5_hash = calculate_md5(firmware_path)
        print(f"[OTA] Firmware MD5: {md5_hash}")
        
        # Get download host (IP that ESP32 can reach)
        print(f"[OTA DEBUG] settings.MQTT_BROKER = {settings.MQTT_BROKER}")
        print(f"[OTA DEBUG] settings.OTA_DOWNLOAD_HOST = {settings.OTA_DOWNLOAD_HOST}")
        download_host = settings.get_ota_download_host
        print(f"[OTA] Download host: {download_host}")
        print(f"[OTA] Full URL will be: http://{download_host}:{settings.SERVER_PORT}/ota/firmware/{firmware_file}")
        
        # Create OTA message
        # ESP32 will download firmware from HTTP server
        ota_message = {
            "command": "update",
            "firmware_url": f"http://{download_host}:{settings.SERVER_PORT}/ota/firmware/{firmware_file}",
            "firmware_file": firmware_file,
            "firmware_size": file_size,
            "md5": md5_hash,
            "version": datetime.now().strftime("%Y%m%d_%H%M%S"),
            "timestamp": datetime.now().isoformat()
        }
        
        print(f"[OTA DEBUG] ota_message = {json.dumps(ota_message, indent=2)}")
        
        # Send MQTT message
        if mqtt_client:
            result = mqtt_client.publish(
                topic,
                json.dumps(ota_message),
                qos=1
            )
            
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                print(f"[OTA] Trigger sent to {device_id} ({device['name']})")
                print(f"[OTA] Topic: {topic}")
                print(f"[OTA] Firmware: {firmware_file} ({file_size} bytes)")
                
                return {
                    "success": True,
                    "device_id": device_id,
                    "device_name": device["name"],
                    "topic": topic,
                    "firmware_file": firmware_file,
                    "firmware_size": file_size,
                    "message": f"OTA update triggered for {device['name']}"
                }
            else:
                return JSONResponse(
                    status_code=500,
                    content={
                        "success": False,
                        "error": "MQTT publish failed"
                    }
                )
        else:
            return JSONResponse(
                status_code=503,
                content={
                    "success": False,
                    "error": "MQTT client not connected"
                }
            )
    
    except Exception as e:
        print(f"[OTA] Error: {e}")
        return JSONResponse(
            status_code=500,
            content={
                "success": False,
                "error": str(e)
            }
        )

@router.get("/firmware/{filename}")
async def download_firmware(filename: str):
    """Download firmware file endpoint for ESP32 devices"""
    file_path = os.path.join(settings.FIRMWARE_DIR, filename)
    
    if not os.path.exists(file_path):
        return JSONResponse(
            status_code=404,
            content={
                "error": "Firmware file not found"
            }
        )
    
    print(f"[OTA DOWNLOAD] ESP32 downloading: {filename}")
    
    return FileResponse(
        path=file_path,
        media_type="application/octet-stream",
        filename=filename
    )
