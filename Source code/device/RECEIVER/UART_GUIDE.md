# Hướng dẫn sử dụng UART Image Receiver

## Yêu cầu

1. **Python 3.x** đã được cài đặt
2. **pyserial library**

## Cài đặt

### 1. Cài đặt Python (nếu chưa có)
- Tải từ: https://www.python.org/downloads/
- Chọn "Add Python to PATH" khi cài đặt

### 2. Cài đặt pyserial
Mở Command Prompt hoặc PowerShell và chạy:
```bash
pip install pyserial
```

## Cấu hình

### 1. Xác định COM Port
- Mở Device Manager (Win + X → Device Manager)
- Tìm **Ports (COM & LPT)**
- Tìm ESP32 (thường là "USB-SERIAL CH340" hoặc "Silicon Labs CP210x")
- Ghi nhớ số COM port (ví dụ: COM3, COM4, etc.)

### 2. Sửa COM Port trong script
Mở file `receive_image.py` và sửa dòng:
```python
SERIAL_PORT = 'COM3'  # Thay COM3 bằng port của bạn
```

## Sử dụng

### 1. Kết nối phần cứng
- Kết nối ESP32 RECEIVER với máy tính qua USB
- Đảm bảo không có Serial Monitor nào đang mở (Arduino IDE, PlatformIO, etc.)
- **Quan trọng**: Chỉ một chương trình có thể mở COM port tại một thời điểm

### 2. Chạy script
Mở Command Prompt/PowerShell tại thư mục chứa `receive_image.py`:
```bash
cd "e:\SourceCode\IoT_Project\Source code\device\RECEIVER"
python receive_image.py
```

### 3. Hoạt động
Script sẽ:
- Kết nối với COM port
- Đợi nhận ảnh từ ESP32
- Hiển thị log từ ESP32 (WiFi status, image received, etc.)
- Khi nhận được ảnh:
  - Chuyển đổi HEX → Binary
  - Kiểm tra kích thước
  - Lưu file vào thư mục `received_images/`
  - Đặt tên file: `image_YYYYMMDD_HHMMSS.jpg`

### 4. Dừng script
Nhấn `Ctrl + C`

## Quy trình hoạt động đầy đủ

```
IR Sensor (CAM) → ESP32-CAM captures image
                     ↓
                  WiFi (HTTP POST)
                     ↓
              ESP32 RECEIVER receives
                     ↓
                UART (HEX encoded)
                     ↓
              Python script receives
                     ↓
           Saves to received_images/image_*.jpg
```

## Xử lý lỗi

### Lỗi: "PermissionError: [WinError 5] Access is denied"
- **Nguyên nhân**: COM port đang được sử dụng bởi chương trình khác
- **Giải pháp**: 
  - Đóng Arduino IDE Serial Monitor
  - Đóng PlatformIO Serial Monitor
  - Đóng tất cả Terminal đang mở COM port

### Lỗi: "SerialException: could not open port 'COM3'"
- **Nguyên nhân**: Sai COM port hoặc ESP32 chưa được kết nối
- **Giải pháp**:
  - Kiểm tra Device Manager
  - Sửa `SERIAL_PORT` trong script
  - Thử rút và cắm lại USB

### Lỗi: "Size mismatch! Expected X, got Y"
- **Nguyên nhân**: Dữ liệu bị mất trong quá trình truyền
- **Giải pháp**:
  - Kiểm tra kết nối USB
  - Thử lại (trigger IR sensor để chụp ảnh mới)
  - Giảm baud rate nếu cần (xuống 57600)

### Ảnh không mở được
- **Kiểm tra**:
  - File size có đúng không (kiểm tra trong log)
  - Thử mở bằng image viewer khác
  - Kiểm tra JPEG header (file nên bắt đầu bằng `FF D8 FF`)

## Thư mục lưu ảnh

Ảnh được lưu tại: `received_images/`

Ví dụ:
```
received_images/
├── image_20240115_143022.jpg
├── image_20240115_143045.jpg
└── image_20240115_143112.jpg
```

## Tips

### Theo dõi real-time
Script hiển thị:
- Trạng thái kết nối Serial
- Log từ ESP32 (WiFi connected, IP address, etc.)
- Quá trình nhận ảnh (size, progress)
- Kết quả lưu file

### Test kết nối
Sau khi chạy script, trigger IR sensor trên ESP32-CAM:
1. CAM chụp ảnh
2. CAM gửi qua WiFi đến RECEIVER
3. RECEIVER nhận và gửi qua UART
4. Python script nhận và lưu file
5. Kiểm tra thư mục `received_images/`

### Tối ưu hóa
Nếu truyền chậm hoặc bị lỗi:
- Giảm image quality trong CAM.ino: `config.jpeg_quality = 15;` (thay vì 12)
- Giảm resolution: Sử dụng QVGA thay vì VGA
- Giảm baud rate xuống 57600 (sửa cả RECEIVER.ino và script Python)

## Tùy chỉnh

### Thay đổi thư mục lưu ảnh
```python
SAVE_DIR = 'my_custom_folder'
```

### Thay đổi format tên file
Sửa hàm `save_image()`:
```python
filename = f"parking_{timestamp}.jpg"  # Custom prefix
```

### Thêm log chi tiết
Thêm trong hàm `receive_image()`:
```python
if data_mode and receiving:
    hex_data += line.replace(" ", "")
    print(f"Received {len(hex_data)} hex chars...", end='\r')  # Progress
```

## Debug mode

Để debug chi tiết hơn, thêm vào đầu script:
```python
DEBUG = True

# Trong receive_image():
if DEBUG:
    print(f"Raw line: {repr(line)}")
```

---

**Lưu ý**: Đảm bảo RECEIVER.ino đã được upload với code mới (có hàm `sendImageOverUART()`)
