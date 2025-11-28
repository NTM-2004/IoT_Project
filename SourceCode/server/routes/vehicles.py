from fastapi import APIRouter, Depends
from fastapi.responses import JSONResponse
from sqlalchemy.orm import Session
from models import get_db, VehicleLog

router = APIRouter(prefix="/api")

@router.get("/vehicles")
async def get_vehicles(limit: int = 50, db: Session = Depends(get_db)):
    # API lấy danh sách xe
    try:
        vehicles = db.query(VehicleLog).order_by(VehicleLog.timestamp.desc()).limit(limit).all()
        return {
            "success": True,
            "count": len(vehicles),
            "vehicles": [
                {
                    "id": v.id,
                    "license_plate": v.license_plate,
                    "confidence": v.confidence,
                    "action": v.action,
                    "timestamp": v.timestamp.isoformat() if v.timestamp else None
                }
                for v in vehicles
            ]
        }
    except Exception as e:
        return JSONResponse(
            status_code=500,
            content={"success": False, "error": str(e)}
        )
