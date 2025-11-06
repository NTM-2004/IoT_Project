"""
Xử lý giao tiếp UART với ESP32
"""
import serial
import serial.tools.list_ports
import threading
import time
from datetime import datetime
from config import settings
import os

class SerialHandler:
    def __init__(self, on_image_received=None):
        self.serial_port = None
        self.is_running = False
        self.on_image_received = on_image_received
        self.receive_thread = None
        
    def find_serial_port(self):
        """Tự động tìm COM port theo tên thiết bị"""
        ports = serial.tools.list_ports.comports()
        
        print("\n=== Scanning COM ports ===")
        for port in ports:
            print(f"Port: {port.device} - {port.description}")
            
            if settings.SERIAL_DEVICE_NAME in port.description:
                print(f"✓ Found device: {port.device}")
                return port.device
        
        print(f"✗ Device '{settings.SERIAL_DEVICE_NAME}' not found!")
        return None
    
    def connect(self):
        """Kết nối Serial - Ưu tiên tìm theo tên thiết bị"""
        try:
            # Luôn tìm theo tên thiết bị trước
            port = self.find_serial_port()
            
            if port is None:
                # Nếu không tìm thấy và có cấu hình SERIAL_PORT
                if settings.SERIAL_PORT:
                    print(f"⚠ Device '{settings.SERIAL_DEVICE_NAME}' not found")
                    print(f"Trying configured port: {settings.SERIAL_PORT}")
                    port = settings.SERIAL_PORT
                else:
                    print(f"✗ Device '{settings.SERIAL_DEVICE_NAME}' not found and no fallback port configured!")
                    return False
            
            self.serial_port = serial.Serial(
                port=port,
                baudrate=settings.SERIAL_BAUD_RATE,
                timeout=1
            )
            print(f"✓ Connected to {port} at {settings.SERIAL_BAUD_RATE} baud")
            return True
        except Exception as e:
            print(f"✗ Error connecting to serial: {e}")
            return False
    
    def start_receiving(self):
        """Bắt đầu nhận dữ liệu"""
        if not self.serial_port:
            print("Serial port not connected!")
            return
        
        self.is_running = True
        self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
        self.receive_thread.start()
        print("✓ Started receiving data from UART")
    
    def _receive_loop(self):
        """Thread nhận dữ liệu liên tục"""
        receiving = False
        data_mode = False
        image_size = 0
        hex_data = ""
        
        while self.is_running:
            try:
                if self.serial_port and self.serial_port.in_waiting:
                    line = self.serial_port.readline().decode('utf-8', errors='ignore').strip()
                    
                    if not line:
                        continue
                    
                    # Bắt đầu nhận ảnh
                    if line == "START_IMAGE":
                        receiving = True
                        hex_data = ""
                        print("\n[UART] Receiving image...")
                        continue
                    
                    # Nhận kích thước
                    if line.startswith("SIZE:"):
                        image_size = int(line.split(":")[1])
                        print(f"[UART] Image size: {image_size} bytes")
                        continue
                    
                    # Bắt đầu data
                    if line == "DATA_START":
                        data_mode = True
                        continue
                    
                    # Kết thúc data
                    if line == "DATA_END":
                        data_mode = False
                        continue
                    
                    # Kết thúc ảnh
                    if line == "END_IMAGE":
                        receiving = False
                        
                        try:
                            image_bytes = bytes.fromhex(hex_data)
                            
                            if len(image_bytes) == image_size:
                                print(f"[UART] ✓ Image received ({len(image_bytes)} bytes)")
                                
                                # Lưu ảnh và gọi callback
                                if self.on_image_received:
                                    self.on_image_received(image_bytes)
                            else:
                                print(f"[UART] ✗ Size mismatch! Expected {image_size}, got {len(image_bytes)}")
                        
                        except Exception as e:
                            print(f"[UART] ✗ Error processing image: {e}")
                        
                        continue
                    
                    # Nhận dữ liệu HEX
                    if data_mode and receiving:
                        hex_data += line.replace(" ", "")
                
                time.sleep(0.001)  # Ngủ ngắn để tránh CPU 100%
            
            except Exception as e:
                print(f"[UART] Error in receive loop: {e}")
                time.sleep(1)
    
    def send_response(self, message):
        """Gửi phản hồi đến ESP32"""
        try:
            if self.serial_port:
                self.serial_port.write(f"{message}\n".encode())
                print(f"[UART] Sent: {message}")
                return True
        except Exception as e:
            print(f"[UART] Error sending: {e}")
        return False
    
    def stop(self):
        """Dừng nhận dữ liệu"""
        self.is_running = False
        if self.receive_thread:
            self.receive_thread.join(timeout=2)
        if self.serial_port:
            self.serial_port.close()
        print("✓ Serial connection closed")
