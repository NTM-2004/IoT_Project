from datetime import datetime
from typing import Callable, Optional, Dict, Any
from sqlalchemy.orm import Session

from models import ParkingSlot, get_db

class SlotUpdateService:
    def __init__(self, websocket_callback: Optional[Callable] = None):
        """
        Args:
            websocket_callback: Function để queue broadcast message
        """
        self.websocket_callback = websocket_callback
    
    def handle_slot_update(self, data: Dict[str, Any]):
        """
        Callback khi nhận message slot update từ MQTT
        Message format: {"slot": "A1", "occupied": true}
        """
        print(f"[MQTT] Slot update: {data}")
        
        try:
            slot_number = data.get('slot')
            is_occupied = data.get('occupied', False)
            
            if not slot_number:
                print("[MQTT] ERROR Missing slot number")
                return
            
            # Cập nhật database
            db = next(get_db())
            try:
                slot = db.query(ParkingSlot).filter(ParkingSlot.slot_number == slot_number).first()
                
                if slot:
                    slot.is_occupied = is_occupied
                    slot.last_updated = datetime.utcnow()
                else:
                    # Tạo mới nếu chưa có
                    slot = ParkingSlot(
                        slot_number=slot_number,
                        is_occupied=is_occupied
                    )
                    db.add(slot)
                
                db.commit()
                print(f"[DATABASE] SUCCESS Slot {slot_number} -> {'OCCUPIED' if is_occupied else 'FREE'}")
                
                # Queue broadcast nếu có callback
                if self.websocket_callback:
                    message = {
                        'type': 'slot_update',
                        'slot': slot_number,
                        'occupied': is_occupied,
                        'timestamp': datetime.utcnow().isoformat()
                    }
                    self.websocket_callback(message)
            
            finally:
                db.close()
        
        except Exception as e:
            print(f"[MQTT] Error handling slot update: {e}")

# Singleton instance
slot_update_service: Optional[SlotUpdateService] = None
