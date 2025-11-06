"""
Script test gọi API PlateRecognizer với ảnh biển số xe
"""
import requests
import os
from pathlib import Path

# Cấu hình
API_URL = "https://api.platerecognizer.com/v1/plate-reader/"
API_KEY = "e7155d879da9a4d4c62f9836cc006418ce3aa028"  # Thay bằng API key thực của bạn
regions = ["vn"]

def test_plate_recognition(image_path):
    """
    Test API OCR với ảnh biển số
    
    Args:
        image_path: Đường dẫn đến file ảnh
    """
    print("=" * 60)
    print("PlateRecognizer API Test")
    print("=" * 60)
    
    # Kiểm tra file tồn tại
    if not os.path.exists(image_path):
        print(f"❌ File không tồn tại: {image_path}")
        return
    
    print(f"📁 Image file: {image_path}")
    print(f"📊 File size: {os.path.getsize(image_path)} bytes")
    print(f"🔑 API Key: {API_KEY[:10]}..." if len(API_KEY) > 10 else "Not configured")
    print("-" * 60)
    
    try:
        # Đọc ảnh
        with open(image_path, 'rb') as fp:
            # Chuẩn bị request
            files = {'upload': ('image.jpg', fp, 'image/jpeg')}
            headers = {'Authorization': f'Token {API_KEY}'}
            data=dict(regions=regions)
            
            print("🚀 Sending request to API...")
            
            # Gọi API
            response = requests.post(
                API_URL,
                data=data,
                files=files,
                headers=headers,
                timeout=30
            )
            
            print(f"📡 Response status: {response.status_code}")
            print("-" * 60)
            
            if response.status_code == 200:
                result = response.json()
                
                print("✅ SUCCESS!")
                print("\n📋 Full Response:")
                print("-" * 60)
                import json
                print(json.dumps(result, indent=2, ensure_ascii=False))
                print("-" * 60)
                
                # Parse kết quả
                if 'results' in result and len(result['results']) > 0:
                    print("\n🎯 Detected Plates:")
                    print("-" * 60)
                    
                    for idx, plate_data in enumerate(result['results'], 1):
                        plate = plate_data.get('plate', 'N/A')
                        score = plate_data.get('score', 0)
                        region = plate_data.get('region', {})
                        vehicle = plate_data.get('vehicle', {})
                        box = plate_data.get('box', {})
                        
                        print(f"\nPlate #{idx}:")
                        print(f"  🚗 License Plate: {plate}")
                        print(f"  📊 Confidence: {score:.2%}")
                        print(f"  🌍 Region: {region.get('code', 'N/A')} - {region.get('score', 0):.2%}")
                        print(f"  🚙 Vehicle Type: {vehicle.get('type', 'N/A')}")
                        print(f"  📦 Bounding Box: x={box.get('xmin')}, y={box.get('ymin')}, " +
                              f"w={box.get('xmax', 0) - box.get('xmin', 0)}, " +
                              f"h={box.get('ymax', 0) - box.get('ymin', 0)}")
                    
                    print("\n" + "=" * 60)
                    print(f"✅ Total plates detected: {len(result['results'])}")
                    
                    # Hiển thị message cho ESP32
                    if result['results']:
                        plate = result['results'][0].get('plate', 'UNKNOWN')
                        conf = result['results'][0].get('score', 0)
                        print(f"\n📤 ACK Message for ESP32:")
                        print(f"   ACK:SUCCESS,PLATE:{plate},CONF:{conf:.2f}")
                else:
                    print("\n⚠️  No plates detected in image")
                    print("📤 ACK Message for ESP32:")
                    print("   ACK:FAILED,ERROR:NO_PLATE_DETECTED")
                
            elif response.status_code == 401:
                print("❌ ERROR: Invalid API Key")
                print("   Please check your API_KEY configuration")
                
            elif response.status_code == 429:
                print("⚠️  ERROR: Rate limit exceeded")
                print("   Please wait and try again later")
                
            else:
                print(f"❌ ERROR {response.status_code}")
                print(f"Response: {response.text}")
    
    except FileNotFoundError:
        print(f"❌ File not found: {image_path}")
    
    except requests.exceptions.Timeout:
        print("❌ Request timeout (30s)")
        print("   API server might be slow or unreachable")
    
    except requests.exceptions.ConnectionError:
        print("❌ Connection error")
        print("   Please check your internet connection")
    
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
        import traceback
        traceback.print_exc()
    
    print("=" * 60)

def main():
    """Main function"""
    import sys
    
    # Kiểm tra API key
    if API_KEY == "YOUR_API_KEY_HERE":
        print("⚠️  WARNING: API_KEY chưa được cấu hình!")
        print("Sửa API_KEY trong file test_ocr.py hoặc tạo file .env")
        print()
    
    # Lấy đường dẫn ảnh từ argument hoặc dùng mặc định
    if len(sys.argv) > 1:
        image_path = sys.argv[1]
    else:
        # Mặc định tìm ảnh trong thư mục hiện tại
        current_dir = Path(__file__).parent
        
        # Thử các tên file phổ biến
        possible_files = [
            "plate.jpg",
            "test_plate.jpg",
            "image.jpg",
            "99-E1.jpg",  # Từ ảnh bạn gửi
        ]
        
        image_path = None
        for filename in possible_files:
            test_path = current_dir / filename
            if test_path.exists():
                image_path = str(test_path)
                break
        
        if image_path is None:
            print("❌ Không tìm thấy file ảnh!")
            print("\nCách sử dụng:")
            print("  python test_ocr.py <đường_dẫn_ảnh>")
            print("\nVí dụ:")
            print("  python test_ocr.py plate.jpg")
            print("  python test_ocr.py C:/Users/Downloads/car_plate.jpg")
            return
    
    # Chạy test
    test_plate_recognition(image_path)

if __name__ == "__main__":
    main()
