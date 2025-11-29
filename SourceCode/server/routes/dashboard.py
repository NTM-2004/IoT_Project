from fastapi import APIRouter, Request, Depends, Form, Response, Cookie
from fastapi.responses import HTMLResponse, RedirectResponse, JSONResponse
from fastapi.templating import Jinja2Templates
from sqlalchemy.orm import Session
from models import get_db, ParkingSlot, VehicleLog, Admin, AdminActionLog
from config import settings
from datetime import datetime
from typing import Optional

# Import session manager
from session_manager import create_session, verify_session, verify_super_admin, get_session, delete_session

router = APIRouter()
templates = Jinja2Templates(directory="templates")

def log_admin_action(db: Session, session_id: str, action_type: str, 
                     action_detail: str = None, request: Request = None, success: bool = True):
    """Log admin action to database"""
    try:
        session_data = get_session(session_id)
        if not session_data:
            return
        
        admin_username = session_data.get("username", "unknown")
        
        # Get admin ID from database
        admin = db.query(Admin).filter(Admin.username == admin_username).first()
        if not admin:
            return
        
        # Get IP address from request
        ip_address = None
        if request:
            ip_address = request.client.host if request.client else None
        
        # Create log entry
        log_entry = AdminActionLog(
            admin_id=admin.id,
            admin_username=admin_username,
            action_type=action_type,
            action_detail=action_detail,
            ip_address=ip_address,
            timestamp=datetime.now(),
            success=success
        )
        
        db.add(log_entry)
        db.commit()
        print(f"[ADMIN LOG] {admin_username} - {action_type} - {'Success' if success else 'Failed'}")
        
    except Exception as e:
        print(f"[ADMIN LOG] Error logging action: {e}")
        db.rollback()

# PUBLIC ENDPOINTS

@router.get("/", response_class=HTMLResponse)
async def public_dashboard(request: Request, db: Session = Depends(get_db)):
    """Public dashboard - hiển thị trạng thái bãi đỗ (không cần auth)"""
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
        
        return templates.TemplateResponse(
            "public_dashboard.html",
            {
                "request": request,
                "slots": slots
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
                </body>
            </html>
            """,
            status_code=500
        )

# AUTH ENDPOINTS

@router.get("/login", response_class=HTMLResponse)
async def login_page(request: Request):
    """Login page"""
    return templates.TemplateResponse(
        "login.html",
        {"request": request}
    )

@router.post("/login")
async def login(response: Response, username: str = Form(...), password: str = Form(...), db: Session = Depends(get_db)):
    """Process login"""
    # Query admin from database
    admin = db.query(Admin).filter(Admin.username == username).first()
    
    # Verify credentials
    if admin and admin.is_active and admin.verify_password(password):
        # Update last login
        admin.last_login = datetime.now()
        db.commit()
        
        session_id = create_session(username, admin.is_super_admin)
        
        # Set cookie
        response = RedirectResponse(url="/admin", status_code=303)
        response.set_cookie(
            key="session_id",
            value=session_id,
            httponly=True,
            max_age=settings.SESSION_EXPIRE_MINUTES * 60,
            samesite="lax"
        )
        return response
    else:
        return templates.TemplateResponse(
            "login.html",
            {
                "request": {"url": "/login"},
                "error": "Invalid username or password"
            },
            status_code=401
        )

@router.get("/logout")
async def logout(response: Response, session_id: Optional[str] = Cookie(None)):
    """Logout user"""
    delete_session(session_id)
    
    response = RedirectResponse(url="/", status_code=303)
    response.delete_cookie("session_id")
    return response

# ADMIN ENDPOINTS (PROTECTED)

@router.get("/admin", response_class=HTMLResponse)
async def admin_dashboard(
    request: Request,
    db: Session = Depends(get_db),
    session_id: Optional[str] = Cookie(None)
):
    """Admin dashboard - quản lý và điều khiển (cần auth)"""
    # Check authentication
    if not verify_session(session_id):
        return RedirectResponse(url="/login", status_code=303)
    
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
        
        # Lấy 20 xe gần đây nhất
        recent_vehicles = db.query(VehicleLog).order_by(VehicleLog.timestamp.desc()).limit(20).all()
        
        # Get username from session
        session_data = get_session(session_id)
        username = session_data["username"] if session_data else "Unknown"
        is_super_admin = session_data.get("is_super_admin", False) if session_data else False
        
        return templates.TemplateResponse(
            "admin_dashboard.html",
            {
                "request": request,
                "slots": slots,
                "recent_vehicles": recent_vehicles,
                "username": username,
                "is_super_admin": is_super_admin
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
                    <p><a href="/admin">Back to Admin Dashboard</a></p>
                </body>
            </html>
            """,
            status_code=500
        )

@router.post("/api/admin/manual-gate")
async def manual_gate_open(request: Request, session_id: Optional[str] = Cookie(None), db: Session = Depends(get_db)):
    """API endpoint to manually open gate (admin only)"""
    # Check authentication
    if not verify_session(session_id):
        return JSONResponse(
            content={"success": False, "error": "Unauthorized"},
            status_code=401
        )
    
    try:
        # Import gate service
        from services.gate_service import gate_service
        
        # Trigger manual gate open
        success = gate_service.trigger_manual_gate()
        
        # Log admin action
        log_admin_action(
            db=db,
            session_id=session_id,
            action_type="open_gate_manual",
            action_detail="Admin manually opened gate from dashboard",
            request=request,
            success=success
        )
        
        if success:
            return JSONResponse(
                content={
                    "success": True,
                    "message": "Gate opened manually"
                }
            )
        else:
            return JSONResponse(
                content={
                    "success": False,
                    "error": "Failed to open gate (MQTT not connected)"
                },
                status_code=500
            )
    except Exception as e:
        return JSONResponse(
            content={
                "success": False,
                "error": str(e)
            },
            status_code=500
        )

# Backward compatibility - redirect old /dashboard to public
@router.get("/dashboard", response_class=HTMLResponse)
async def dashboard_redirect():
    """Redirect old dashboard URL to public dashboard"""
    return RedirectResponse(url="/", status_code=301)

# ADMIN MANAGEMENT ENDPOINTS

@router.post("/api/admin/create-user")
async def create_admin_user(
    username: str = Form(...),
    password: str = Form(...),
    full_name: str = Form(None),
    session_id: Optional[str] = Cookie(None),
    db: Session = Depends(get_db)
):
    """Create a new admin user (super admin only)"""
    # Check if user is super admin
    if not verify_super_admin(session_id):
        return JSONResponse(
            content={"success": False, "error": "Unauthorized - Super admin only"},
            status_code=403
        )
    
    try:
        # Check if username already exists
        existing_admin = db.query(Admin).filter(Admin.username == username).first()
        if existing_admin:
            return JSONResponse(
                content={"success": False, "error": "Username already exists"},
                status_code=400
            )
        
        # Create new admin
        new_admin = Admin(
            username=username,
            password_hash=Admin.hash_password(password),
            full_name=full_name or username,
            is_active=True
        )
        db.add(new_admin)
        db.commit()
        
        return JSONResponse(
            content={
                "success": True,
                "message": f"Admin user '{username}' created successfully"
            }
        )
    except Exception as e:
        db.rollback()
        return JSONResponse(
            content={"success": False, "error": str(e)},
            status_code=500
        )

@router.get("/api/admin/list-users")
async def list_admin_users(
    session_id: Optional[str] = Cookie(None),
    db: Session = Depends(get_db)
):
    """List all admin users (super admin only)"""
    # Check if user is super admin
    if not verify_super_admin(session_id):
        return JSONResponse(
            content={"success": False, "error": "Unauthorized - Super admin only"},
            status_code=403
        )
    
    try:
        admins = db.query(Admin).all()
        admin_list = [
            {
                "id": admin.id,
                "username": admin.username,
                "full_name": admin.full_name,
                "is_active": admin.is_active,
                "is_super_admin": admin.is_super_admin,
                "created_at": admin.created_at.isoformat() if admin.created_at else None,
                "last_login": admin.last_login.isoformat() if admin.last_login else None
            }
            for admin in admins
        ]
        
        return JSONResponse(
            content={"success": True, "admins": admin_list}
        )
    except Exception as e:
        return JSONResponse(
            content={"success": False, "error": str(e)},
            status_code=500
        )

@router.post("/api/admin/toggle-active")
async def toggle_admin_active(
    admin_id: int = Form(...),
    session_id: Optional[str] = Cookie(None),
    db: Session = Depends(get_db)
):
    """Toggle admin active status (super admin only)"""
    if not verify_super_admin(session_id):
        return JSONResponse(
            content={"success": False, "error": "Unauthorized - Super admin only"},
            status_code=403
        )
    
    try:
        admin = db.query(Admin).filter(Admin.id == admin_id).first()
        if not admin:
            return JSONResponse(
                content={"success": False, "error": "Admin not found"},
                status_code=404
            )
        
        # Không cho phép vô hiệu hóa super admin
        if admin.is_super_admin:
            return JSONResponse(
                content={"success": False, "error": "Cannot deactivate super admin"},
                status_code=400
            )
        
        admin.is_active = not admin.is_active
        db.commit()
        
        return JSONResponse(
            content={
                "success": True,
                "message": f"Admin '{admin.username}' is now {'active' if admin.is_active else 'inactive'}",
                "is_active": admin.is_active
            }
        )
    except Exception as e:
        db.rollback()
        return JSONResponse(
            content={"success": False, "error": str(e)},
            status_code=500
        )

@router.post("/api/admin/delete-user")
async def delete_admin_user(
    admin_id: int = Form(...),
    session_id: Optional[str] = Cookie(None),
    db: Session = Depends(get_db)
):
    """Delete admin user (super admin only)"""
    if not verify_super_admin(session_id):
        return JSONResponse(
            content={"success": False, "error": "Unauthorized - Super admin only"},
            status_code=403
        )
    
    try:
        admin = db.query(Admin).filter(Admin.id == admin_id).first()
        if not admin:
            return JSONResponse(
                content={"success": False, "error": "Admin not found"},
                status_code=404
            )
        
        # Không cho phép xóa super admin
        if admin.is_super_admin:
            return JSONResponse(
                content={"success": False, "error": "Cannot delete super admin"},
                status_code=400
            )
        
        username = admin.username
        db.delete(admin)
        db.commit()
        
        return JSONResponse(
            content={
                "success": True,
                "message": f"Admin '{username}' deleted successfully"
            }
        )
    except Exception as e:
        db.rollback()
        return JSONResponse(
            content={"success": False, "error": str(e)},
            status_code=500
        )

@router.post("/api/admin/change-password")
async def change_admin_password(
    admin_id: int = Form(...),
    new_password: str = Form(...),
    session_id: Optional[str] = Cookie(None),
    db: Session = Depends(get_db)
):
    """Change admin password (super admin only)"""
    if not verify_super_admin(session_id):
        return JSONResponse(
            content={"success": False, "error": "Unauthorized - Super admin only"},
            status_code=403
        )
    
    try:
        admin = db.query(Admin).filter(Admin.id == admin_id).first()
        if not admin:
            return JSONResponse(
                content={"success": False, "error": "Admin not found"},
                status_code=404
            )
        
        admin.password_hash = Admin.hash_password(new_password)
        db.commit()
        
        return JSONResponse(
            content={
                "success": True,
                "message": f"Password changed for '{admin.username}'"
            }
        )
    except Exception as e:
        db.rollback()
        return JSONResponse(
            content={"success": False, "error": str(e)},
            status_code=500
        )
