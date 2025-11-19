from fastapi import APIRouter, Request, Depends
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from sqlalchemy.orm import Session
from models import get_db, ParkingSlot, VehicleLog

router = APIRouter()
templates = Jinja2Templates(directory="templates")

@router.get("/dashboard", response_class=HTMLResponse)
async def dashboard(request: Request, db: Session = Depends(get_db)):
    # Dashboard realtime hiển thị trạng thái bãi đỗ
    try:
        predefined_slots = ['A1', 'A2', 'A3', 'A4', 'B1', 'B2', 'B3', 'B4']
        for slot_name in predefined_slots:
            existing_slot = db.query(ParkingSlot).filter(ParkingSlot.slot_number == slot_name).first()
            if not existing_slot:
                new_slot = ParkingSlot(
                    slot_number=slot_name,
                    is_occupied=False
                )
                db.add(new_slot)
        db.commit()
        
        # Lấy danh sách slots
        slots = db.query(ParkingSlot).order_by(ParkingSlot.slot_number).all()
        
        # Lấy 10 xe gần đây nhất
        recent_vehicles = db.query(VehicleLog).order_by(VehicleLog.timestamp.desc()).limit(10).all()
        
        return templates.TemplateResponse(
            "dashboard.html",
            {
                "request": request,
                "slots": slots,
                "recent_vehicles": recent_vehicles
            }
        )
    except Exception as e:
        return HTMLResponse(
            content=f"""
            <html>
                <head><title>Dashboard Error</title></head>
                <body>
                    <h1>Dashboard Error</h1>
                    <p>Error loading dashboard: {str(e)}</p>
                    <p><a href="/">Back to Home</a></p>
                </body>
            </html>
            """,
            status_code=500
        )
