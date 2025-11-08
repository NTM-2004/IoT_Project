"""
Main application - Simplified FastAPI Server
API upload ảnh + MQTT subscribe slot status
"""
from fastapi import FastAPI, WebSocket, Depends, Request, File, UploadFile
from fastapi.templating import Jinja2Templates
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse, JSONResponse
from sqlalchemy.orm import Session
from datetime import datetime
import os
import json
import asyncio
import subprocess
import time

from config import settings
from models import init_db, get_db, ParkingSlot, VehicleLog
from ocr_service import ocr_service
from mqtt_handler import MQTTHandler

# Khởi tạo FastAPI
app = FastAPI(title="IoT Parking System - Simplified", version="2.0.0")

# Templates và Static files
templates = Jinja2Templates(directory="templates")
os.makedirs("static", exist_ok=True)
os.makedirs(settings.UPLOAD_DIR, exist_ok=True)
os.makedirs(settings.TEMP_DIR, exist_ok=True)
os.makedirs(settings.ARCHIVE_DIR, exist_ok=True)
app.mount("/static", StaticFiles(directory="static"), name="static")

# WebSocket clients cho realtime update
websocket_clients = []
pending_broadcasts = []  # Queue cho messages cần broadcast

# MQTT Handler
mqtt_handler = None
mosquitto_process = None

def start_mosquitto():
    """Khởi động Mosquitto broker trong cmd mới"""
    global mosquitto_process
    
    mosquitto_dir = r"E:\Program Data\MQ\mosquitto"
    mosquitto_exe = os.path.join(mosquitto_dir, "mosquitto.exe")
    mosquitto_conf = os.path.join(mosquitto_dir, "mosquitto.conf")
    
    # Kiểm tra file tồn tại
    if not os.path.exists(mosquitto_exe):
        print(f"⚠ Mosquitto not found at: {mosquitto_exe}")
        return False
    
    if not os.path.exists(mosquitto_conf):
        print(f"⚠ Config not found at: {mosquitto_conf}")
        return False
    
    try:
        print(f"[MOSQUITTO] Starting broker...")
        print(f"[MOSQUITTO] Dir: {mosquitto_dir}")
        print(f"[MOSQUITTO] Config: {mosquitto_conf}")
        
        # Mở cmd mới và chạy mosquitto
        cmd = f'start "Mosquitto Broker" cmd /k "cd /d "{mosquitto_dir}" && mosquitto.exe -c "{mosquitto_conf}" -v"'
        
        mosquitto_process = subprocess.Popen(
            cmd,
            shell=True,
            cwd=mosquitto_dir
        )
        
        print(f"[MOSQUITTO] ✓ Started in new window (PID: {mosquitto_process.pid})")
        print(f"[MOSQUITTO] Waiting 5 seconds for broker to initialize...")
        time.sleep(5)  # Tăng thời gian chờ từ 3 lên 5 giây
        
        return True
    
    except Exception as e:
        print(f"[MOSQUITTO] ✗ Failed to start: {e}")
        return False

@app.on_event("startup")
async def startup_event():
    """Khởi tạo khi server start"""
    global mqtt_handler
    
    print("=" * 50)
    print("Starting IoT Parking System Server (Simplified)")
    print("=" * 50)
    
    # Khởi tạo database
    try:
        init_db()
        print("✓ Database initialized (MySQL)")
    except Exception as e:
        print(f"✗ Database error: {e}")
        print("  Make sure MySQL is running and credentials are correct")
    
    # Khởi động background task để broadcast WebSocket messages
    asyncio.create_task(websocket_broadcast_worker())
    print("✓ WebSocket broadcast worker started")
    
    # Khởi tạo MQTT Handler để nhận trạng thái slot
    print(f"[MQTT] Attempting to connect to: {settings.MQTT_BROKER}:{settings.MQTT_PORT}")
    
    # Nếu broker là localhost/127.0.0.1, thử kết nối trực tiếp
    # Nếu là IP khác (192.168.x.x), giả sử broker đã chạy sẵn
    is_local_broker = settings.MQTT_BROKER in ["localhost", "127.0.0.1", "0.0.0.0"]
    
    mqtt_handler = MQTTHandler(on_slot_update=handle_slot_update)
    if mqtt_handler.connect():
        print("✓ MQTT Handler connected")
    else:
        print("⚠ MQTT Handler failed")
        
        # Chỉ thử khởi động Mosquitto nếu broker là localhost
        if is_local_broker:
            print("[MQTT] Broker is localhost, trying to start Mosquitto...")
            
            if start_mosquitto():
                print("[MOSQUITTO] ✓ Broker started, retrying connection...")
                
                # Thử kết nối lại sau khi khởi động broker
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
    
    print("=" * 50)
    print(f"Server running at http://{settings.SERVER_HOST}:{settings.SERVER_PORT}")
    print(f"API Docs: http://{settings.SERVER_HOST}:{settings.SERVER_PORT}/docs")
    print("=" * 50)

@app.on_event("shutdown")
async def shutdown_event():
    """Cleanup khi server stop"""
    global mosquitto_process
    
    if mqtt_handler:
        mqtt_handler.disconnect()
    
    # Đóng Mosquitto nếu được khởi động bởi server
    if mosquitto_process:
        print("[MOSQUITTO] Stopping broker...")
        try:
            mosquitto_process.terminate()
            mosquitto_process.wait(timeout=5)
            print("[MOSQUITTO] ✓ Stopped")
        except:
            print("[MOSQUITTO] ⚠ Could not stop gracefully")
    
    print("✓ Server shutdown complete")

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
            
            # Thêm vào queue để broadcast (vì đây là sync function)
            message = {
                'type': 'slot_update',
                'slot': slot_number,
                'occupied': is_occupied,
                'timestamp': datetime.utcnow().isoformat()
            }
            pending_broadcasts.append(message)
            print(f"[WEBSOCKET] Queued broadcast for slot {slot_number}")
        
        finally:
            db.close()
    
    except Exception as e:
        print(f"[MQTT] ✗ Error handling slot update: {e}")

async def broadcast_to_websockets(message):
    """Broadcast message đến tất cả WebSocket clients"""
    disconnected = []
    for client in websocket_clients:
        try:
            await client.send_json(message)
        except:
            disconnected.append(client)
    
    # Remove disconnected clients
    for client in disconnected:
        websocket_clients.remove(client)

async def websocket_broadcast_worker():
    """Background task để xử lý queue broadcast"""
    while True:
        try:
            if pending_broadcasts:
                # Lấy message từ queue
                message = pending_broadcasts.pop(0)
                await broadcast_to_websockets(message)
                print(f"[WEBSOCKET] Broadcasted: {message.get('type')}")
            
            # Chờ 0.1s trước khi check lại
            await asyncio.sleep(0.1)
        except Exception as e:
            print(f"[WEBSOCKET] Broadcast worker error: {e}")
            await asyncio.sleep(1)

# ==================== Routes ====================

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

@app.get("/dashboard", response_class=HTMLResponse)
async def dashboard(request: Request, db: Session = Depends(get_db)):
    """Dashboard realtime hiển thị trạng thái bãi đỗ"""
    try:
        # Khởi tạo 8 slots cố định nếu chưa có
        predefined_slots = ['A1', 'A2', 'A3', 'A4', 'B1', 'B2', 'B3', 'B4']
        for slot_name in predefined_slots:
            existing_slot = db.query(ParkingSlot).filter(ParkingSlot.slot_number == slot_name).first()
            if not existing_slot:
                new_slot = ParkingSlot(
                    slot_number=slot_name,
                    is_occupied=False
                )
                db.add(new_slot)
        db.commit()
        
        # Lấy danh sách slots
        slots = db.query(ParkingSlot).order_by(ParkingSlot.slot_number).all()
        
        # Lấy 10 xe gần đây nhất
        recent_vehicles = db.query(VehicleLog).order_by(VehicleLog.timestamp.desc()).limit(10).all()
        
        return templates.TemplateResponse(
            "dashboard.html",
            {
                "request": request,
                "slots": slots,
                "recent_vehicles": recent_vehicles
            }
        )
    except Exception as e:
        return HTMLResponse(
            content=f"""
            <html>
                <head><title>Dashboard Error</title></head>
                <body>
                    <h1>⚠️ Dashboard Error</h1>
                    <p>Error loading dashboard: {str(e)}</p>
                    <p><a href="/">Back to Home</a></p>
                </body>
            </html>
            """,
            status_code=500
        )

@app.get("/api/slots")
async def get_slots(db: Session = Depends(get_db)):
    """API lấy danh sách slots"""
    try:
        slots = db.query(ParkingSlot).all()
        return {
            "success": True,
            "count": len(slots),
            "slots": [
                {
                    "id": s.id,
                    "slot_number": s.slot_number,
                    "is_occupied": s.is_occupied,
                    "last_updated": s.last_updated.isoformat() if s.last_updated else None
                }
                for s in slots
            ]
        }
    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"success": False, "error": str(e)}
        )

@app.get("/api/vehicles")
async def get_vehicles(limit: int = 50, db: Session = Depends(get_db)):
    """API lấy danh sách xe"""
    try:
        vehicles = db.query(VehicleLog).order_by(VehicleLog.timestamp.desc()).limit(limit).all()
        return {
            "success": True,
            "count": len(vehicles),
            "vehicles": [
                {
                    "id": v.id,
                    "license_plate": v.license_plate,
                    "confidence": v.confidence,
                    "action": v.action,
                    "timestamp": v.timestamp.isoformat() if v.timestamp else None
                }
                for v in vehicles
            ]
        }
    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"success": False, "error": str(e)}
        )

@app.post("/api/upload-image")
async def upload_image(file: UploadFile = File(...), db: Session = Depends(get_db)):
    """
    API nhận ảnh từ ESP32-CAM
    ESP32 gửi POST request với multipart/form-data
    """
    print(f"\n{'='*50}")
    print(f"[UPLOAD] Received: {file.filename} ({file.content_type})")
    
    try:
        # Đọc nội dung ảnh
        image_bytes = await file.read()
        image_size = len(image_bytes)
        print(f"[UPLOAD] Size: {image_size} bytes ({image_size/1024:.2f} KB)")
        
        # Kiểm tra kích thước
        if image_size < 1000:
            print("[UPLOAD] ❌ Image too small!")
            return JSONResponse(
                status_code=400,
                content={
                    "success": False,
                    "error": "IMAGE_TOO_SMALL",
                    "message": "Ảnh quá nhỏ",
                    "action": "none"
                }
            )
        
        # Lưu ảnh vào TEMP
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        temp_filename = f"temp_{timestamp}.jpg"
        temp_path = os.path.join(settings.TEMP_DIR, temp_filename)
        
        with open(temp_path, 'wb') as f:
            f.write(image_bytes)
        
        print(f"[UPLOAD] ✅ Saved temp: {temp_path}")
        
        # Gọi OCR API
        print("[OCR] Processing...")
        result = ocr_service.recognize_plate(image_bytes)
        
        if result:
            plate = result.get('plate', 'UNKNOWN')
            confidence = result.get('confidence', 0)
            
            print(f"[OCR] ✅ Plate: {plate} (confidence: {confidence:.2f})")
            
            # Lưu vào ARCHIVE nếu đạt ngưỡng
            if plate != 'UNKNOWN' and confidence > 0.5:
                archive_filename = f"{plate}_{timestamp}.jpg"
                archive_path = os.path.join(settings.ARCHIVE_DIR, archive_filename)
                
                import shutil
                shutil.copy2(temp_path, archive_path)
                print(f"[ARCHIVE] ✅ Archived: {archive_path}")
                
                # Xóa temp
                try:
                    os.remove(temp_path)
                    print("[CLEANUP] ✅ Temp removed")
                except:
                    pass
                
                final_path = archive_path
            else:
                print("[ARCHIVE] ⚠ Low confidence, keeping in temp")
                final_path = temp_path
            
            # Lưu vào database
            try:
                log = VehicleLog(
                    license_plate=plate,
                    image_path=final_path,
                    ocr_result=json.dumps(result),
                    confidence=str(confidence),
                    action="entry"
                )
                db.add(log)
                db.commit()
                db.refresh(log)
                print(f"[DATABASE] ✅ Saved (ID: {log.id})")
            except Exception as db_error:
                print(f"[DATABASE] ⚠ Error: {db_error}")
                # Continue anyway, không crash vì lỗi DB
            
            # Broadcast WebSocket
            await broadcast_to_websockets({
                'type': 'new_vehicle',
                'plate': plate,
                'confidence': confidence,
                'timestamp': datetime.now().isoformat()
            })
            
            print(f"{'='*50}\n")
            
            # Trả response cho ESP32
            return JSONResponse(
                status_code=200,
                content={
                    "success": True,
                    "plate": plate,
                    "confidence": confidence,
                    "message": f"Biển số: {plate}",
                    "action": "open_gate" if confidence > 0.5 else "none",
                    "saved_path": final_path
                }
            )
        else:
            print("[OCR] ❌ Failed")
            
            # Xóa temp
            try:
                os.remove(temp_path)
            except:
                pass
            
            print(f"{'='*50}\n")
            
            return JSONResponse(
                status_code=200,
                content={
                    "success": False,
                    "error": "OCR_FAILED",
                    "message": "Không đọc được biển số",
                    "action": "none"
                }
            )
    
    except Exception as e:
        print(f"[ERROR] ❌ {str(e)}")
        print(f"{'='*50}\n")
        
        return JSONResponse(
            status_code=500,
            content={
                "success": False,
                "error": str(e),
                "message": "Lỗi xử lý ảnh",
                "action": "none"
            }
        )

@app.post("/api/slot-update")
async def update_slot(
    slot_number: str,
    is_occupied: bool,
    db: Session = Depends(get_db)
):
    """
    API cập nhật trạng thái slot (thay thế MQTT)
    ESP32 NODE có thể gọi API này thay vì dùng MQTT
    """
    try:
        slot = db.query(ParkingSlot).filter(ParkingSlot.slot_number == slot_number).first()
        
        if slot:
            slot.is_occupied = is_occupied
            slot.last_updated = datetime.utcnow()
        else:
            slot = ParkingSlot(
                slot_number=slot_number,
                is_occupied=is_occupied
            )
            db.add(slot)
        
        db.commit()
        
        print(f"[SLOT] Updated: {slot_number} -> {'OCCUPIED' if is_occupied else 'FREE'}")
        
        # Broadcast WebSocket
        await broadcast_to_websockets({
            'type': 'slot_update',
            'slot': slot_number,
            'occupied': is_occupied,
            'timestamp': datetime.utcnow().isoformat()
        })
        
        return {
            "success": True,
            "message": f"Slot {slot_number} updated"
        }
    
    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"success": False, "error": str(e)}
        )

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """WebSocket endpoint cho realtime updates"""
    await websocket.accept()
    websocket_clients.append(websocket)
    print(f"[WS] Client connected (total: {len(websocket_clients)})")
    
    try:
        while True:
            await websocket.receive_text()
    except:
        pass
    finally:
        if websocket in websocket_clients:
            websocket_clients.remove(websocket)
        print(f"[WS] Client disconnected (total: {len(websocket_clients)})")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        "main:app",
        host=settings.SERVER_HOST,
        port=settings.SERVER_PORT,
        reload=True
    )
