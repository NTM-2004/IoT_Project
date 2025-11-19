from fastapi import FastAPI, File, UploadFile, Request
from fastapi.templating import Jinja2Templates
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse, FileResponse, JSONResponse
import paho.mqtt.client as mqtt
import os
import json
from datetime import datetime
from pathlib import Path
from config import settings

os.makedirs(settings.FIRMWARE_DIR, exist_ok=True)

# FastAPI App
app = FastAPI(title="IoT OTA Update System", version="1.0")
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
        print(f"[MQTT] Connected to broker: {settings.MQTT_BROKER}:{settings.MQTT_PORT}")
    else:
        print(f"[MQTT] Connection failed with code {rc}")

def on_publish(client, userdata, mid):
    print(f"[MQTT] Message published (mid: {mid})")

def init_mqtt():
    global mqtt_client
    mqtt_client = mqtt.Client()
    mqtt_client.on_connect = on_connect
    mqtt_client.on_publish = on_publish
    
    try:
        mqtt_client.connect(settings.MQTT_BROKER, settings.MQTT_PORT, 60)
        mqtt_client.loop_start()
        return True
    except Exception as e:
        print(f"[MQTT] ERROR: {e}")
        return False

@app.on_event("startup")
async def startup_event():
    print("=" * 50)
    print("Starting OTA Update Server")
    print("=" * 50)
    
    if init_mqtt():
        print("[MQTT] SUCCESS MQTT initialized")
    else:
        print("[MQTT] WARNING Running without MQTT")
    
    print(f"[SERVER] Web interface: http://{settings.OTA_SERVER_HOST}:{settings.OTA_SERVER_PORT}")
    print(f"[SERVER] Firmware directory: {os.path.abspath(settings.FIRMWARE_DIR)}")
    print("=" * 50)

@app.on_event("shutdown")
async def shutdown_event():
    if mqtt_client:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
    print("[SERVER] OTA Server shutdown complete")

@app.get("/", response_class=HTMLResponse)
async def root(request: Request):

    return templates.TemplateResponse("ota_dashboard.html", {
        "request": request,
        "devices": DEVICES
    })

@app.get("/api/devices")
async def get_devices():
    # Danh sách thiết bị
    return {
        "success": True,
        "devices": DEVICES
    }

@app.get("/api/firmware/list")
async def list_firmware():
    # List các file firmware có sẵn
    firmware_files = []
    
    for filename in os.listdir(settings.FIRMWARE_DIR):
        if filename.endswith('.bin'):
            filepath = os.path.join(settings.FIRMWARE_DIR, filename)
            stat = os.stat(filepath)
            firmware_files.append({
                "filename": filename,
                "size": stat.st_size,
                "size_kb": round(stat.st_size / 1024, 2),
                "modified": datetime.fromtimestamp(stat.st_mtime).strftime("%Y-%m-%d %H:%M:%S")
            })
    
    return {
        "success": True,
        "firmware_files": firmware_files,
        "count": len(firmware_files)
    }

@app.post("/api/firmware/upload")
async def upload_firmware(file: UploadFile = File(...)):
    # Upload file firmware 
    try:
        # Kiểm tra file extension
        if not file.filename.endswith('.bin'):
            return JSONResponse(
                status_code=400,
                content={
                    "success": False,
                    "error": "Only .bin files are allowed"
                }
            )
        
        # Lưu file
        file_path = os.path.join(settings.FIRMWARE_DIR, file.filename)
        
        with open(file_path, 'wb') as f:
            content = await file.read()
            f.write(content)
        
        file_size = len(content)
        
        print(f"[FIRMWARE] Uploaded: {file.filename} ({file_size} bytes)")
        
        return {
            "success": True,
            "filename": file.filename,
            "size": file_size,
            "size_kb": round(file_size / 1024, 2),
            "path": file_path
        }
    
    except Exception as e:
        print(f"[FIRMWARE] Upload error: {e}")
        return JSONResponse(
            status_code=500,
            content={
                "success": False,
                "error": str(e)
            }
        )

@app.post("/api/ota/trigger")
async def trigger_ota_update(request: Request):
    # Trigger OTA update
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
        
        # Kiểm tra firmware file có tồn tại
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
        
        # Lấy thông tin firmware
        file_size = os.path.getsize(firmware_path)
        
        # Tạo OTA message
        # ESP32 sẽ download firmware từ HTTP server
        ota_message = {
            "command": "update",
            "firmware_url": f"http://{settings.MQTT_BROKER}:{settings.OTA_SERVER_PORT}/firmware/{firmware_file}",
            "firmware_file": firmware_file,
            "firmware_size": file_size,
            "version": datetime.now().strftime("%Y%m%d_%H%M%S"),
            "timestamp": datetime.now().isoformat()
        }
        
        # Gửi MQTT message
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

@app.get("/firmware/{filename}")
async def download_firmware(filename: str):
    # Endpoint firmware
    file_path = os.path.join(settings.FIRMWARE_DIR, filename)
    
    if not os.path.exists(file_path):
        return JSONResponse(
            status_code=404,
            content={
                "error": "Firmware file not found"
            }
        )
    
    print(f"[DOWNLOAD] ESP32 downloading: {filename}")
    
    return FileResponse(
        path=file_path,
        media_type="application/octet-stream",
        filename=filename
    )

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        "main_ota:app",
        host=settings.OTA_SERVER_HOST,
        port=settings.OTA_SERVER_PORT,
        reload=True
    )
