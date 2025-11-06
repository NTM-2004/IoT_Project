# IoT Parking System Server

Server Python cho hệ thống quản lý bãi đỗ xe IoT.

## Tính năng

- ✅ Nhận ảnh từ ESP32 qua UART
- ✅ Gọi API OCR để nhận dạng biển số xe
- ✅ Trả kết quả về ESP32 qua UART
- ✅ Nhận trạng thái slot đỗ từ MQTT
- ✅ Dashboard web realtime với WebSocket
- ✅ Lưu trữ dữ liệu vào SQLite database
- ✅ RESTful API với FastAPI

## Cài đặt

### 1. Cài đặt dependencies

```bash
cd "e:\SourceCode\IoT_Project\Source code\server"
pip install -r requirements.txt
```

### 2. Cấu hình

Copy file `.env.example` thành `.env` và chỉnh sửa:

```bash
cp .env.example .env
```

Sửa các thông số trong `.env`:
- `SERIAL_PORT`: COM port của ESP32
- `MQTT_BROKER`: Địa chỉ MQTT broker
- `OCR_API_KEY`: API key của platerecognizer.com

### 3. Chạy server

```bash
python main.py
```

Hoặc với uvicorn:

```bash
uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```

## Kiến trúc

```
server/
├── main.py                 # FastAPI application
├── config.py              # Cấu hình hệ thống
├── models.py              # Database models (SQLAlchemy)
├── serial_handler.py      # Xử lý UART communication
├── mqtt_handler.py        # Xử lý MQTT messages
├── ocr_service.py         # Service gọi OCR API
├── requirements.txt       # Python dependencies
├── .env                   # Environment variables
├── templates/
│   └── dashboard.html     # Dashboard template
└── static/                # Static files (CSS, JS, images)
```

## API Endpoints

### Web Dashboard
- `GET /` - Dashboard chính

### REST API
- `GET /api/slots` - Lấy danh sách slots
- `GET /api/vehicles?limit=50` - Lấy danh sách xe

### WebSocket
- `WS /ws` - Realtime updates

## Quy trình hoạt động

### 1. Nhận ảnh từ ESP32 (UART)
```
ESP32 → UART → SerialHandler → Lưu ảnh → OCR Service
                                              ↓
ESP32 ← UART ← SerialHandler ← Kết quả OCR ← API
```

### 2. Cập nhật slot từ MQTT
```
NODE ESP32 → MQTT Broker → MQTTHandler → Update Database → WebSocket → Dashboard
```

### 3. Dashboard realtime
```
Browser ← WebSocket ← Server ← MQTT/UART
```

## Database Schema

### ParkingSlot
- `id`: Integer (Primary Key)
- `slot_number`: String (Unique)
- `is_occupied`: Boolean
- `last_updated`: DateTime

### VehicleLog
- `id`: Integer (Primary Key)
- `license_plate`: String
- `image_path`: String
- `ocr_result`: String (JSON)
- `confidence`: String
- `timestamp`: DateTime
- `action`: String ("entry" hoặc "exit")

## MQTT Topics

### Subscribe
- `iot/parking/slots` - Nhận trạng thái slot

Format message:
```json
{
  "slot": "A01",
  "occupied": true
}
```

## UART Protocol

### Nhận ảnh từ ESP32
```
START_IMAGE
SIZE:14537
DATA_START
FFD8FFE000104A46...
DATA_END
END_IMAGE
```

### Gửi kết quả OCR về ESP32
```
PLATE:30A12345,CONF:0.95
```

Hoặc khi lỗi:
```
ERROR:OCR_FAILED
```

## Development

### Chạy ở development mode
```bash
python main.py
```

### Chạy ở production mode
```bash
uvicorn main:app --host 0.0.0.0 --port 8000 --workers 4
```

## Troubleshooting

### Lỗi: "Device not found"
- Kiểm tra ESP32 đã kết nối USB
- Sửa `SERIAL_DEVICE_NAME` trong `.env`
- Hoặc cấu hình trực tiếp `SERIAL_PORT=COM3`

### Lỗi: "MQTT connection failed"
- Kiểm tra MQTT broker đang chạy
- Kiểm tra `MQTT_BROKER` và `MQTT_PORT` trong `.env`

### Lỗi: "OCR API failed"
- Kiểm tra `OCR_API_KEY` trong `.env`
- Kiểm tra quota API (platerecognizer.com)

## License

MIT
