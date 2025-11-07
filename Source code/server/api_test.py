"""
API Test Server - Không dùng Database
Chỉ test upload ảnh và OCR
"""
from fastapi import FastAPI, File, UploadFile
from fastapi.responses import JSONResponse
from datetime import datetime
import os

# Tạo thư mục lưu ảnh
os.makedirs("temp", exist_ok=True)
os.makedirs("archive", exist_ok=True)

app = FastAPI(title="IoT Parking Test API")

@app.get("/")
async def root():
    """Endpoint test"""
    return {
        "message": "IoT Parking Test API",
        "status": "running",
        "endpoints": {
            "upload": "POST /api/upload-image",
            "test": "GET /api/test"
        }
    }

@app.get("/api/test")
async def test():
    """Test endpoint"""
    return {
        "status": "ok",
        "timestamp": datetime.now().isoformat()
    }

@app.post("/api/upload-image")
async def upload_image(file: UploadFile = File(...)):
    """
    API nhận ảnh từ ESP32-CAM
    """
    print(f"\n{'='*50}")
    print(f"[UPLOAD] Received: {file.filename}")
    print(f"[UPLOAD] Content-Type: {file.content_type}")
    
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
        
        # Lưu ảnh vào temp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        temp_filename = f"temp_{timestamp}.jpg"
        temp_path = os.path.join("temp", temp_filename)
        
        with open(temp_path, 'wb') as f:
            f.write(image_bytes)
        
        print(f"[UPLOAD] ✅ Saved: {temp_path}")
        
        # GIẢ LẬP OCR (không gọi API thật)
        # Trong thực tế sẽ gọi ocr_service.recognize_plate(image_bytes)
        fake_plate = f"51F-{timestamp[-6:]}"  # Tạo biển số giả từ timestamp
        fake_confidence = 0.85
        
        print(f"[OCR] Plate: {fake_plate}")
        print(f"[OCR] Confidence: {fake_confidence}")
        
        # Lưu vào archive
        archive_filename = f"{fake_plate}_{timestamp}.jpg"
        archive_path = os.path.join("archive", archive_filename)
        
        import shutil
        shutil.copy2(temp_path, archive_path)
        print(f"[ARCHIVE] ✅ Archived: {archive_path}")
        
        # Xóa temp
        os.remove(temp_path)
        print(f"[CLEANUP] ✅ Temp removed")
        
        print(f"{'='*50}\n")
        
        # Trả response
        return JSONResponse(
            status_code=200,
            content={
                "success": True,
                "plate": fake_plate,
                "confidence": fake_confidence,
                "message": f"Biển số: {fake_plate}",
                "action": "open_gate",
                "saved_path": archive_path
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

@app.post("/api/upload-image-with-ocr")
async def upload_image_with_real_ocr(file: UploadFile = File(...)):
    """
    API nhận ảnh và GỌI OCR THẬT
    Cần cài: pip install requests
    """
    print(f"\n{'='*50}")
    print(f"[UPLOAD] Received: {file.filename}")
    
    try:
        import requests
        
        # Đọc ảnh
        image_bytes = await file.read()
        image_size = len(image_bytes)
        
        print(f"[UPLOAD] Size: {image_size} bytes")
        
        if image_size < 1000:
            return JSONResponse(
                status_code=400,
                content={
                    "success": False,
                    "error": "IMAGE_TOO_SMALL",
                    "message": "Ảnh quá nhỏ",
                    "action": "none"
                }
            )
        
        # Lưu temp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        temp_path = os.path.join("temp", f"temp_{timestamp}.jpg")
        
        with open(temp_path, 'wb') as f:
            f.write(image_bytes)
        
        print(f"[UPLOAD] ✅ Saved: {temp_path}")
        
        # Gọi PlateRecognizer API
        print("[OCR] Calling PlateRecognizer API...")
        
        api_url = "https://api.platerecognizer.com/v1/plate-reader/"
        api_token = "Token 7cc02221bef5bad4659b56b49b015f6007955700"
        
        with open(temp_path, 'rb') as fp:
            response = requests.post(
                api_url,
                files={'upload': fp},
                headers={'Authorization': api_token},
                data={'regions': 'vn'}
            )
        
        if response.status_code == 201:
            result = response.json()
            
            if result.get('results'):
                plate_data = result['results'][0]
                plate = plate_data['plate']
                confidence = plate_data['score']
                
                print(f"[OCR] ✅ Plate: {plate}")
                print(f"[OCR] Confidence: {confidence}")
                
                # Lưu archive
                archive_path = os.path.join("archive", f"{plate}_{timestamp}.jpg")
                import shutil
                shutil.copy2(temp_path, archive_path)
                print(f"[ARCHIVE] ✅ Archived: {archive_path}")
                
                # Xóa temp
                os.remove(temp_path)
                
                print(f"{'='*50}\n")
                
                return JSONResponse(
                    status_code=200,
                    content={
                        "success": True,
                        "plate": plate,
                        "confidence": confidence,
                        "message": f"Biển số: {plate}",
                        "action": "open_gate" if confidence > 0.5 else "none",
                        "saved_path": archive_path
                    }
                )
            else:
                print("[OCR] ❌ No plate detected")
                os.remove(temp_path)
                
                return JSONResponse(
                    status_code=200,
                    content={
                        "success": False,
                        "error": "NO_PLATE",
                        "message": "Không phát hiện biển số",
                        "action": "none"
                    }
                )
        else:
            print(f"[OCR] ❌ API Error: {response.status_code}")
            os.remove(temp_path)
            
            return JSONResponse(
                status_code=200,
                content={
                    "success": False,
                    "error": "OCR_API_ERROR",
                    "message": "Lỗi API OCR",
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
                "message": "Lỗi xử lý",
                "action": "none"
            }
        )

if __name__ == "__main__":
    import uvicorn
    print("\n" + "="*50)
    print("🚀 Starting Test API Server")
    print("="*50)
    print("📍 URL: http://0.0.0.0:8000")
    print("📋 Docs: http://0.0.0.0:8000/docs")
    print("="*50 + "\n")
    
    uvicorn.run(
        "api_test:app",
        host="0.0.0.0",
        port=8000,
        reload=True
    )
