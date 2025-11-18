"""
Startup utilities
Khởi tạo và cleanup khi server start/stop
"""
import os
import subprocess
import time
from config import settings
from models import init_db

# Global mosquitto process
mosquitto_process = None

def start_mosquitto() -> bool:
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
        time.sleep(5)
        
        return True
    
    except Exception as e:
        print(f"[MOSQUITTO] ✗ Failed to start: {e}")
        return False

def stop_mosquitto():
    """Dừng Mosquitto broker"""
    global mosquitto_process
    
    if mosquitto_process:
        print("[MOSQUITTO] Stopping broker...")
        try:
            mosquitto_process.terminate()
            mosquitto_process.wait(timeout=5)
            print("[MOSQUITTO] ✓ Stopped")
        except:
            print("[MOSQUITTO] ⚠ Could not stop gracefully")

def initialize_database():
    """Khởi tạo database"""
    try:
        init_db()
        print("✓ Database initialized (MySQL)")
        return True
    except Exception as e:
        print(f"✗ Database error: {e}")
        print("  Make sure MySQL is running and credentials are correct")
        return False

def create_directories():
    """Tạo các thư mục cần thiết"""
    os.makedirs("static", exist_ok=True)
    os.makedirs(settings.UPLOAD_DIR, exist_ok=True)
    os.makedirs(settings.TEMP_DIR, exist_ok=True)
    os.makedirs(settings.ARCHIVE_DIR, exist_ok=True)
    print("✓ Directories created")
