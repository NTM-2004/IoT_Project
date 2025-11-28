from typing import Dict, Any
from datetime import datetime

class GateService:
    def __init__(self, mqtt_handler=None):
        self.mqtt_handler = mqtt_handler
    
    def send_open_command(self, plate: str, confidence: float) -> bool:
        
        # Gửi lệnh mở cổng qua MQTT
        if not self.mqtt_handler:
            print("[GATE] ERROR MQTT handler not available")
            return False
        
        message = {
            "action": "open",
            "plate": plate,
            "confidence": confidence,
            "timestamp": datetime.now().isoformat()
        }
        
        success = self.mqtt_handler.publish("iot/parking/gate/control", message)
        
        if success:
            print(f"[GATE] SUCCESS OPEN command sent - Plate: {plate} (confidence: {confidence:.2f})")
        else:
            print(f"[GATE] Failed to send OPEN command")
        
        return success
    
    def send_reject_command(self, reason: str = "OCR failed") -> bool:
        
        # Gửi lệnh từ chối (giữ cổng đóng) qua MQTT
        if not self.mqtt_handler:
            print("[GATE] ERROR MQTT handler not available")
            return False
        
        message = {
            "action": "reject",
            "reason": reason,
            "timestamp": datetime.now().isoformat()
        }
        
        success = self.mqtt_handler.publish("iot/parking/gate/control", message)
        
        if success:
            print(f"[GATE] REJECT command sent - Reason: {reason}")
        else:
            print(f"[GATE] Failed to send REJECT command")
        
        return success
    
    def should_open_gate(self, plate: str, confidence: float, threshold: float = 0.5) -> bool:
        """
        Quyết định có nên mở cổng hay không dựa trên kết quả OCR
        
        Args:
            plate: Biển số nhận diện được
            confidence: Độ tin cậy của OCR
            threshold: Ngưỡng confidence tối thiểu
        
        Returns: True nếu nên mở cổng
        """
        if not plate or plate == "UNKNOWN":
            return False
        
        if confidence < threshold:
            return False
              
        return True
    
    def process_ocr_result(self, plate: str, confidence: float) -> Dict[str, Any]:
        """
        Xử lý kết quả OCR và gửi lệnh điều khiển cổng
        Returns: Dict chứa action và status
        """
        should_open = self.should_open_gate(plate, confidence)
        
        if should_open:
            success = self.send_open_command(plate, confidence)
            return {
                "action": "open",
                "success": success,
                "plate": plate,
                "confidence": confidence
            }
        else:
            reason = "Low confidence" if plate else "No plate detected"
            success = self.send_reject_command(reason)
            return {
                "action": "reject",
                "success": success,
                "reason": reason,
                "confidence": confidence
            }
    
    def trigger_manual_gate(self) -> bool:
        """
        Mở cổng thủ công từ admin dashboard
        Returns: True nếu thành công
        """
        if not self.mqtt_handler:
            print("[GATE] ERROR MQTT handler not available")
            return False
        
        message = {
            "action": "open",
            "plate": "MANUAL",
            "confidence": 1.0,
            "manual": True,
            "timestamp": datetime.now().isoformat()
        }
        
        success = self.mqtt_handler.publish("iot/parking/gate/control", message)
        
        if success:
            print(f"[GATE] SUCCESS Manual gate open command sent")
        else:
            print(f"[GATE] Failed to send manual gate command")
        
        return success

# Singleton instance
gate_service = GateService()
