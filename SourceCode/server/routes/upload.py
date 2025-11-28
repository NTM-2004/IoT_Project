from fastapi import APIRouter, File, UploadFile, Depends
from fastapi.responses import JSONResponse
from sqlalchemy.orm import Session
from datetime import datetime
import os
import json
import shutil

from config import settings
from models import get_db, VehicleLog
from services.ocr_service import ocr_service
from services import websocket_service, gate_service

router = APIRouter(prefix="/api")

@router.get("/test-upload")
async def test_upload():
    
    # Test endpoint để verify server đang chạy
    print("\n[TEST] /api/test-upload called")
    return {
        "status": "OK",
        "message": "Upload API is running",
        "timestamp": datetime.now().isoformat()
    }

@router.post("/upload-image")
async def upload_image(file: UploadFile = File(...), db: Session = Depends(get_db)):
    
    # ESP32 gửi POST request với multipart/form-data
    print(f"\n{'='*50}")
    print(f"[UPLOAD] NEW REQUEST RECEIVED")
    print(f"[UPLOAD] Filename: {file.filename}")
    print(f"[UPLOAD] Content-Type: {file.content_type}")
    
    try:
        # Đọc nội dung ảnh
        image_bytes = await file.read()
        image_size = len(image_bytes)
        print(f"[UPLOAD] Image read successfully")
        print(f"[UPLOAD] Size: {image_size} bytes ({image_size/1024:.2f} KB)")
        
        # Check magic bytes (JPEG starts with FF D8 FF)
        if image_size > 3:
            header = image_bytes[:3].hex()
            print(f"[UPLOAD] Magic bytes: {header} (should be 'ffd8ff' for JPEG)")
        
        # Kiểm tra kích thước
        if image_size < 1000:
            print("[UPLOAD] Image too small")
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
        
        print(f"[UPLOAD] Saving to: {temp_path}")
        
        with open(temp_path, 'wb') as f:
            bytes_written = f.write(image_bytes)
        
        # Verify file was written
        if os.path.exists(temp_path):
            actual_size = os.path.getsize(temp_path)
            print(f"[UPLOAD] File saved successfully")
            print(f"[UPLOAD] Expected: {image_size} bytes, Written: {bytes_written} bytes, Disk: {actual_size} bytes")
            
            if actual_size != image_size:
                print(f"[UPLOAD] WARNING: Size mismatch!")
        else:
            print(f"[UPLOAD] ERROR: File not found after write!")
            raise Exception("Failed to save temp file")
        
        # Gọi OCR API
        print("[OCR] Processing...")
        result = ocr_service.recognize_plate(image_bytes)
        
        if result:
            plate = result.get('plate', 'UNKNOWN')
            confidence = result.get('confidence', 0)
            
            print(f"[OCR] Success Plate: {plate} (confidence: {confidence:.2f})")
            
            # Lưu vào ARCHIVE nếu đạt ngưỡng
            if plate != 'UNKNOWN' and confidence > 0.5:
                archive_filename = f"{plate}_{timestamp}.jpg"
                archive_path = os.path.join(settings.ARCHIVE_DIR, archive_filename)
                
                shutil.move(temp_path, archive_path)
                print(f"[ARCHIVE] Moved to archive: {archive_path}")
                
                final_path = archive_path
            else:
                # Low confidence - xóa ảnh temp
                print("[ARCHIVE] Low confidence")
                try:
                    os.remove(temp_path)
                    print("[CLEANUP] Temp file deleted")
                except Exception as e:
                    print(f"[CLEANUP] Cannot delete temp: {e}")
                
                final_path = None
            
            # Lưu vào database khi thành công
            if final_path:
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
                    print(f"[DATABASE] SUCCESS Saved (ID: {log.id})")
                except Exception as db_error:
                    print(f"[DATABASE] Error: {db_error}")
            else:
                print("[DATABASE] Skipped - low confidence")
            
            # Broadcast WebSocket
            await websocket_service.broadcast({
                'type': 'new_vehicle',
                'plate': plate,
                'confidence': confidence,
                'timestamp': datetime.now().isoformat()
            })
            
            # Điều khiển GATE qua MQTT
            if gate_service:
                gate_result = gate_service.process_ocr_result(plate, confidence)
                gate_action = gate_result.get('action', 'none')
            else:
                gate_action = "none"
                print("[GATE] ERROR Gate service not available")
            
            print(f"{'='*50}\n")
            
            # Trả response cho ESP32-CAM
            return JSONResponse(
                status_code=200,
                content={
                    "success": True,
                    "plate": plate,
                    "confidence": confidence,
                    "message": f"Biển số: {plate}",
                    "action": gate_action,
                    "saved_path": final_path
                }
            )
        else:
            print("[OCR] Failed")
            
            # Xóa temp file
            try:
                os.remove(temp_path)
                print("[CLEANUP] Temp file deleted (OCR failed)")
            except Exception as e:
                print(f"[CLEANUP] Cannot delete temp: {e}")
            
            # Gửi lệnh reject cho GATE
            if gate_service:
                gate_service.send_reject_command("OCR failed")
            
            print(f"{'='*50}\n")
            
            return JSONResponse(
                status_code=200,
                content={
                    "success": False,
                    "error": "OCR_FAILED",
                    "message": "Không đọc được biển số",
                    "action": "reject"
                }
            )
    
    except Exception as e:
        print(f"[ERROR] {str(e)}")
        print(f"{'='*50}\n")
        
        # Gửi lệnh reject cho GATE
        if gate_service:
            gate_service.send_reject_command(f"Error: {str(e)}")
        
        return JSONResponse(
            status_code=500,
            content={
                "success": False,
                "error": str(e),
                "message": "Lỗi xử lý ảnh",
                "action": "reject"
            }
        )
