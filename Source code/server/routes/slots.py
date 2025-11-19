from fastapi import APIRouter, Depends
from fastapi.responses import JSONResponse
from sqlalchemy.orm import Session
from datetime import datetime
from models import get_db, ParkingSlot
from services import websocket_service

router = APIRouter(prefix="/api")

@router.get("/slots")
async def get_slots(db: Session = Depends(get_db)):
    # API lấy danh sách slots
    try:
        slots = db.query(ParkingSlot).all()
        return {
            "success": True,
            "count": len(slots),
            "slots": [
                {
                    "id": s.id,
                    "slot_number": s.slot_number,
                    "is_occupied": s.is_occupied,
                    "last_updated": s.last_updated.isoformat() if s.last_updated else None
                }
                for s in slots
            ]
        }
    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"success": False, "error": str(e)}
        )

@router.post("/slot-update")
async def update_slot(
    slot_number: str,
    is_occupied: bool,
    db: Session = Depends(get_db)
):
    # API cập nhật trạng thái slot (không sử dụng vì đang dùng MQTT)
    try:
        slot = db.query(ParkingSlot).filter(ParkingSlot.slot_number == slot_number).first()
        
        if slot:
            slot.is_occupied = is_occupied
            slot.last_updated = datetime.utcnow()
        else:
            slot = ParkingSlot(
                slot_number=slot_number,
                is_occupied=is_occupied
            )
            db.add(slot)
        
        db.commit()
        
        print(f"[SLOT] Updated: {slot_number} -> {'OCCUPIED' if is_occupied else 'FREE'}")
        
        # Broadcast WebSocket
        await websocket_service.broadcast({
            'type': 'slot_update',
            'slot': slot_number,
            'occupied': is_occupied,
            'timestamp': datetime.utcnow().isoformat()
        })
        
        return {
            "success": True,
            "message": f"Slot {slot_number} updated"
        }
    
    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"success": False, "error": str(e)}
        )
