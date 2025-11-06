"""
Script Python để nhận ảnh từ ESP32 RECEIVER qua UART và lưu vào máy tính
Yêu cầu: pip install pyserial
"""

import serial
import serial.tools.list_ports
import time
from datetime import datetime
import os

# Cấu hình Serial Port
DEVICE_NAME = 'USB-SERIAL CH340'  # Tên thiết bị để tìm kiếm
BAUD_RATE = 115200

# Thư mục lưu ảnh
SAVE_DIR = 'received_images'

def find_serial_port():
    """Tự động tìm COM port theo tên thiết bị"""
    ports = serial.tools.list_ports.comports()
    
    print("\n=== Scanning COM ports ===")
    for port in ports:
        print(f"Port: {port.device}")
        print(f"  Description: {port.description}")
        print(f"  Manufacturer: {port.manufacturer}")
        print()
        
        # Tìm port có tên chứa "USB-SERIAL CH340"
        if DEVICE_NAME in port.description:
            print(f"✓ Found device: {port.device} ({port.description})")
            return port.device
    
    print(f"✗ Device '{DEVICE_NAME}' not found!")
    print("\nAvailable ports:")
    for port in ports:
        print(f"  {port.device}: {port.description}")
    
    return None

def setup_serial():
    """Khởi tạo kết nối Serial"""
    # Tự động tìm COM port
    serial_port = find_serial_port()
    
    if serial_port is None:
        print("\nNo compatible device found. Please check:")
        print("1. ESP32 is connected via USB")
        print("2. CH340 driver is installed")
        print("3. Device is not being used by another program")
        return None
    
    try:
        ser = serial.Serial(serial_port, BAUD_RATE, timeout=1)
        print(f"✓ Connected to {serial_port} at {BAUD_RATE} baud")
        return ser
    except Exception as e:
        print(f"✗ Error connecting to {serial_port}: {e}")
        return None

def receive_image(ser):
    """Nhận ảnh từ ESP32 qua UART"""
    print("\nWaiting for image...")
    
    receiving = False
    data_mode = False
    image_size = 0
    hex_data = ""
    
    while True:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            if not line:
                continue
            
            # Hiển thị log từ ESP32
            if not data_mode and not line.startswith("SIZE:"):
                print(f"ESP32: {line}")
            
            # Bắt đầu nhận ảnh
            if line == "START_IMAGE":
                receiving = True
                hex_data = ""
                print("\n=== Receiving image... ===")
                continue
            
            # Nhận kích thước ảnh
            if line.startswith("SIZE:"):
                image_size = int(line.split(":")[1])
                print(f"Image size: {image_size} bytes")
                continue
            
            # Bắt đầu nhận dữ liệu
            if line == "DATA_START":
                data_mode = True
                continue
            
            # Kết thúc nhận dữ liệu
            if line == "DATA_END":
                data_mode = False
                continue
            
            # Kết thúc nhận ảnh
            if line == "END_IMAGE":
                receiving = False
                
                # Chuyển đổi HEX string thành binary
                try:
                    image_bytes = bytes.fromhex(hex_data)
                    
                    if len(image_bytes) == image_size:
                        # Lưu ảnh
                        save_image(image_bytes)
                        print(f"✓ Image saved successfully! ({len(image_bytes)} bytes)")
                    else:
                        print(f"✗ Size mismatch! Expected {image_size}, got {len(image_bytes)}")
                
                except Exception as e:
                    print(f"✗ Error converting image: {e}")
                
                print("=== Waiting for next image... ===\n")
                continue
            
            # Nhận dữ liệu HEX
            if data_mode and receiving:
                hex_data += line.replace(" ", "")
        
        except KeyboardInterrupt:
            print("\nStopped by user")
            break
        except Exception as e:
            print(f"Error: {e}")
            continue

def save_image(image_bytes):
    """Lưu ảnh vào file"""
    # Tạo thư mục nếu chưa có
    if not os.path.exists(SAVE_DIR):
        os.makedirs(SAVE_DIR)
    
    # Tạo tên file với timestamp
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"image_{timestamp}.jpg"
    filepath = os.path.join(SAVE_DIR, filename)
    
    # Lưu file
    with open(filepath, 'wb') as f:
        f.write(image_bytes)
    
    print(f"Saved to: {filepath}")

def main():
    print("=" * 50)
    print("ESP32 Image Receiver via UART")
    print("=" * 50)
    print(f"Device Name: {DEVICE_NAME}")
    print(f"Baud Rate: {BAUD_RATE}")
    print(f"Save Directory: {SAVE_DIR}")
    print("=" * 50)
    
    # Kết nối Serial
    ser = setup_serial()
    if ser is None:
        return
    
    try:
        # Nhận ảnh liên tục
        receive_image(ser)
    finally:
        ser.close()
        print("Serial port closed")

if __name__ == "__main__":
    main()
