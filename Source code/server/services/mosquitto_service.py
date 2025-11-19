import os
import subprocess
import time
from typing import Optional

class MosquittoService:
    def __init__(self, mosquitto_dir: str = r"E:\Program Data\MQ\mosquitto"):
        self.mosquitto_dir = mosquitto_dir
        self.mosquitto_exe = os.path.join(mosquitto_dir, "mosquitto.exe")
        self.mosquitto_conf = os.path.join(mosquitto_dir, "mosquitto.conf")
        self.process: Optional[subprocess.Popen] = None
    
    def is_configured(self) -> bool:
        
        # Kiểm tra xem Mosquitto đã được cấu hình đúng chưa
        if not os.path.exists(self.mosquitto_exe):
            print(f"ERROR Mosquitto not found at: {self.mosquitto_exe}")
            return False
        
        if not os.path.exists(self.mosquitto_conf):
            print(f"ERROR Config not found at: {self.mosquitto_conf}")
            return False
        
        return True
    
    def start(self, wait_time: int = 5) -> bool:
        
        # Khởi động Mosquitto broker trong cmd mới
        if not self.is_configured():
            return False
        
        print(f"[MOSQUITTO] Starting broker...")
        print(f"[MOSQUITTO] Dir: {self.mosquitto_dir}")
        print(f"[MOSQUITTO] Config: {self.mosquitto_conf}")
        
        try:
            # Mở cmd mới và chạy mosquitto
            cmd = f'start "Mosquitto Broker" cmd /k "cd /d "{self.mosquitto_dir}" && mosquitto.exe -c "{self.mosquitto_conf}" -v"'
            
            self.process = subprocess.Popen(
                cmd,
                shell=True,
                cwd=self.mosquitto_dir
            )
            
            print(f"[MOSQUITTO] SUCCESS Started in new window (PID: {self.process.pid})")
            print(f"[MOSQUITTO] Waiting {wait_time} seconds for broker to initialize...")
            time.sleep(wait_time)
            
            return True
        
        except Exception as e:
            print(f"[MOSQUITTO] ERROR Failed to start: {e}")
            return False
    
    def stop(self, timeout: int = 5) -> bool:
        
        # Dừng Mosquitto broker
        if not self.process:
            return True
        
        print("[MOSQUITTO] Stopping broker...")
        try:
            self.process.terminate()
            self.process.wait(timeout=timeout)
            print("[MOSQUITTO] Stopped")
            return True
        except Exception as e:
            print(f"[MOSQUITTO] ERROR Could not stop gracefully: {e}")
            return False
    
    def is_running(self) -> bool:
        
        # Kiểm tra xem broker có đang chạy không
        return self.process is not None and self.process.poll() is None

# Singleton instance
mosquitto_service = MosquittoService()
