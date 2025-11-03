# IoT_Project
Nhóm 7 IoT

Api backend:
1. Plate api
   * Db: id, number, imagePath, timestamp, type (có thể bỏ);
   * Post: /upload : thiết bị call để lưu biển với giờ vào vào db (có ocr)
   * Get: /show: hiển thị toàn bộ danh sách trong db 
3. Slot api
   * Db: id, slotId, state
   * Get: /stream: Sử dụng SseEmitter để cập nhật theo tg thực
   * Put: /status: Cập nhật trạng thái đỗ xe (trống, đã có xe)
   * Get: /show: hiển thị danh sách vị trí
4. Gate api
   * Db: id, gateId, action;
   * Post: /open: Gửi yêu cầu đến backend để mở cửa (cần MQTT để gửi lại thiết bị yêu cầu thiết bị mở cửa)
5. Note
   * Không quản lý thiết bị
   * Chỉ nhận thông tin gửi từ thiết bị (ảnh biển xe và vị trí xe)
