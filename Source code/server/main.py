"""
Main application - FastAPI Server
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

from config import settings
from models import init_db, get_db, ParkingSlot, VehicleLog
from serial_handler import SerialHandler
from mqtt_handler import MQTTHandler
from ocr_service import ocr_service

# Khởi tạo FastAPI
app = FastAPI(title="IoT Parking System", version="1.0.0")

# Templates và Static files
templates = Jinja2Templates(directory="templates")
os.makedirs("static", exist_ok=True)
os.makedirs(settings.UPLOAD_DIR, exist_ok=True)
os.makedirs(settings.TEMP_DIR, exist_ok=True)  # Thư mục cache tạm
os.makedirs(settings.ARCHIVE_DIR, exist_ok=True)  # Lưu trữ lâu dài
app.mount("/static", StaticFiles(directory="static"), name="static")

# Global handlers
serial_handler = None
mqtt_handler = None

# WebSocket clients cho realtime update
websocket_clients = []

@app.on_event("startup")
async def startup_event():
    """Khởi tạo khi server start"""
    global serial_handler, mqtt_handler
    
    print("=" * 50)
    print("Starting IoT Parking System Server")
    print("=" * 50)
    
    # Khởi tạo database
    init_db()
    print("✓ Database initialized")
    
    # Khởi tạo Serial Handler
    serial_handler = SerialHandler(on_image_received=handle_image_received)
    if serial_handler.connect():
        serial_handler.start_receiving()
    
    # Khởi tạo MQTT Handler
    mqtt_handler = MQTTHandler(on_slot_update=handle_slot_update)
    mqtt_handler.connect()
    
    print("=" * 50)
    print(f"Server running at http://{settings.SERVER_HOST}:{settings.SERVER_PORT}")
    print("=" * 50)

@app.on_event("shutdown")
async def shutdown_event():
    """Cleanup khi server stop"""
    if serial_handler:
        serial_handler.stop()
    if mqtt_handler:
        mqtt_handler.disconnect()
    print("✓ Server shutdown complete")

# ==================== Callbacks ====================

def handle_image_received(image_bytes):
    """Xử lý khi nhận ảnh từ UART"""
    print(f"[APP] Processing image ({len(image_bytes)} bytes)...")
    
    # Bước 1: Lưu ảnh vào TEMP (cache tạm)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    temp_path = os.path.join(settings.TEMP_DIR, f"temp_{timestamp}.jpg")
    
    with open(temp_path, 'wb') as f:
        f.write(image_bytes)
    print(f"[APP] ✓ Temp image saved: {temp_path}")
    
    # Bước 2: Gọi OCR API (synchronous trong thread riêng)
    import threading
    thread = threading.Thread(target=process_ocr_sync, args=(image_bytes, temp_path, timestamp))
    thread.start()

def process_ocr_sync(image_bytes, temp_path, timestamp):
    """Xử lý OCR đồng bộ trong thread riêng"""
    result = ocr_service.recognize_plate(image_bytes)
    
    if result:
        plate = result.get('plate', 'UNKNOWN')
        confidence = result.get('confidence', 0)
        
        print(f"[APP] ✓ OCR Result: {plate} (confidence: {confidence})")
        
        # Bước 3: Nếu đọc được biển số, lưu vào ARCHIVE (lưu trữ lâu dài)
        if plate != 'UNKNOWN' and confidence > 0.5:  # Ngưỡng confidence
            archive_path = os.path.join(settings.ARCHIVE_DIR, f"{plate}_{timestamp}.jpg")
            
            # Copy từ temp sang archive
            import shutil
            shutil.copy2(temp_path, archive_path)
            print(f"[APP] ✓ Image archived: {archive_path}")
            
            # Xóa file temp
            try:
                os.remove(temp_path)
                print(f"[APP] ✓ Temp file removed")
            except:
                pass
            
            final_path = archive_path
        else:
            # Không đạt ngưỡng, giữ ở temp
            print(f"[APP] ⚠ Low confidence or unknown plate, keeping in temp")
            final_path = temp_path
        
        # Bước 4: Lưu vào database
        db = next(get_db())
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
            print(f"[APP] ✓ Saved to database")
        finally:
            db.close()
        
        # Bước 5: Gửi ACK về ESP32 qua UART
        ack_message = f"ACK:SUCCESS,PLATE:{plate},CONF:{confidence:.2f}"
        serial_handler.send_response(ack_message)
        print(f"[APP] ✓ ACK sent to ESP32")
        
        # Broadcast đến WebSocket clients
        asyncio.run(broadcast_to_websockets({
            'type': 'new_vehicle',
            'plate': plate,
            'confidence': confidence,
            'timestamp': datetime.now().isoformat()
        }))
    else:
        print(f"[APP] ✗ OCR failed")
        
        # Gửi ACK lỗi về ESP32
        serial_handler.send_response("ACK:FAILED,ERROR:OCR_FAILED")
        
        # Xóa temp file nếu OCR fail
        try:
            os.remove(temp_path)
        except:
            pass

def handle_slot_update(data):
    """Xử lý khi nhận update slot từ MQTT"""
    print(f"[APP] Slot update: {data}")
    
    # Cập nhật database
    db = next(get_db())
    try:
        slot_number = data.get('slot')
        is_occupied = data.get('occupied', False)
        
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
        print(f"[APP] ✓ Slot {slot_number} updated: {'OCCUPIED' if is_occupied else 'FREE'}")
        
        # Broadcast đến WebSocket
        asyncio.create_task(broadcast_to_websockets({
            'type': 'slot_update',
            'slot': slot_number,
            'occupied': is_occupied,
            'timestamp': datetime.utcnow().isoformat()
        }))
    
    finally:
        db.close()

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

# ==================== Routes ====================

@app.get("/", response_class=HTMLResponse)
async def dashboard(request: Request, db: Session = Depends(get_db)):
    """Dashboard chính"""
    slots = db.query(ParkingSlot).all()
    recent_vehicles = db.query(VehicleLog).order_by(VehicleLog.timestamp.desc()).limit(10).all()
    
    return templates.TemplateResponse("dashboard.html", {
        "request": request,
        "slots": slots,
        "recent_vehicles": recent_vehicles
    })

@app.get("/api/slots")
async def get_slots(db: Session = Depends(get_db)):
    """API lấy danh sách slots"""
    slots = db.query(ParkingSlot).all()
    return {
        "slots": [
            {
                "slot_number": s.slot_number,
                "is_occupied": s.is_occupied,
                "last_updated": s.last_updated.isoformat() if s.last_updated else None
            }
            for s in slots
        ]
    }

@app.get("/api/vehicles")
async def get_vehicles(limit: int = 50, db: Session = Depends(get_db)):
    """API lấy danh sách xe"""
    vehicles = db.query(VehicleLog).order_by(VehicleLog.timestamp.desc()).limit(limit).all()
    return {
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

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """WebSocket endpoint cho realtime updates"""
    await websocket.accept()
    websocket_clients.append(websocket)
    print(f"[WS] Client connected (total: {len(websocket_clients)})")
    
    try:
        while True:
            # Keep connection alive
            await websocket.receive_text()
    except:
        pass
    finally:
        websocket_clients.remove(websocket)
        print(f"[WS] Client disconnected (total: {len(websocket_clients)})")

@app.post("/api/upload-image")
async def upload_image(file: UploadFile = File(...)):
    """
    API nhận ảnh từ ESP32-CAM
    ESP32 gửi POST request với multipart/form-data
    """
    print(f"[API] Received image upload: {file.filename} ({file.content_type})")
    
    try:
        # Đọc nội dung ảnh
        image_bytes = await file.read()
        print(f"[API] Image size: {len(image_bytes)} bytes")
        
        # Kiểm tra kích thước hợp lệ
        if len(image_bytes) < 1000:  # Ảnh quá nhỏ
            return JSONResponse(
                status_code=400,
                content={
                    "success": False,
                    "error": "Image too small",
                    "message": "Ảnh quá nhỏ, có thể bị lỗi"
                }
            )
        
        # Bước 1: Lưu ảnh vào TEMP
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        temp_path = os.path.join(settings.TEMP_DIR, f"temp_{timestamp}.jpg")
        
        with open(temp_path, 'wb') as f:
            f.write(image_bytes)
        print(f"[API] ✓ Temp image saved: {temp_path}")
        
        # Bước 2: Gọi OCR API
        result = ocr_service.recognize_plate(image_bytes)
        
        if result:
            plate = result.get('plate', 'UNKNOWN')
            confidence = result.get('confidence', 0)
            
            print(f"[API] ✓ OCR Result: {plate} (confidence: {confidence})")
            
            # Bước 3: Lưu vào ARCHIVE nếu đạt ngưỡng
            if plate != 'UNKNOWN' and confidence > 0.5:
                archive_path = os.path.join(settings.ARCHIVE_DIR, f"{plate}_{timestamp}.jpg")
                
                import shutil
                shutil.copy2(temp_path, archive_path)
                print(f"[API] ✓ Image archived: {archive_path}")
                
                # Xóa temp
                try:
                    os.remove(temp_path)
                except:
                    pass
                
                final_path = archive_path
            else:
                print(f"[API] ⚠ Low confidence, keeping in temp")
                final_path = temp_path
            
            # Bước 4: Lưu database
            db = next(get_db())
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
                print(f"[API] ✓ Saved to database")
            finally:
                db.close()
            
            # Bước 5: Broadcast WebSocket
            await broadcast_to_websockets({
                'type': 'new_vehicle',
                'plate': plate,
                'confidence': confidence,
                'timestamp': datetime.now().isoformat()
            })
            
            # Trả response cho ESP32
            return JSONResponse(
                status_code=200,
                content={
                    "success": True,
                    "plate": plate,
                    "confidence": confidence,
                    "message": f"Biển số: {plate}",
                    "action": "open_gate"  # Signal cho ESP32 mở cổng
                }
            )
        else:
            print(f"[API] ✗ OCR failed")
            
            # Xóa temp nếu OCR fail
            try:
                os.remove(temp_path)
            except:
                pass
            
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
        print(f"[API] ✗ Error: {str(e)}")
        return JSONResponse(
            status_code=500,
            content={
                "success": False,
                "error": str(e),
                "message": "Lỗi xử lý ảnh"
            }
        )

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        "main:app",
        host=settings.SERVER_HOST,
        port=settings.SERVER_PORT,
        reload=True
    )
