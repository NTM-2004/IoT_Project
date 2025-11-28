"""
Script để khởi động cả main.py và main_ota.py đồng thời
"""
import subprocess
import sys
import time
import os

def start_servers():
    # Đường dẫn thư mục hiện tại
    current_dir = os.path.dirname(os.path.abspath(__file__))
    
    print("=" * 60)
    print("Starting Parking System Servers")
    print("=" * 60)
    
    try:
        # Khởi động main.py trên port 8000
        print(f"\n[1] Starting Main Server on port 8000...")
        main_process = subprocess.Popen(
            [sys.executable, "-m", "uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000", "--reload"],
            cwd=current_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # Đợi 2 giây
        time.sleep(2)
        
        # Khởi động main_ota.py trên port 8001
        print(f"[2] Starting OTA Server on port 8001...")
        ota_process = subprocess.Popen(
            [sys.executable, "-m", "uvicorn", "main_ota:app", "--host", "0.0.0.0", "--port", "8001", "--reload"],
            cwd=current_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        print("\n" + "=" * 60)
        print("Both servers started successfully!")
        print("=" * 60)
        print(f"Main Dashboard:  http://localhost:8000")
        print(f"OTA Dashboard:   http://localhost:8001 (Super Admin only)")
        print(f"Login Page:      http://localhost:8000/login")
        print("=" * 60)
        print("\nPress Ctrl+C to stop both servers...\n")
        
        # Đợi cho đến khi người dùng nhấn Ctrl+C
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\n\nShutting down servers...")
            main_process.terminate()
            ota_process.terminate()
            
            # Đợi cho các process kết thúc
            main_process.wait()
            ota_process.wait()
            
            print("All servers stopped.")
            print("=" * 60)
    
    except Exception as e:
        print(f"\n[ERROR] Failed to start servers: {e}")
        sys.exit(1)

if __name__ == "__main__":
    start_servers()
