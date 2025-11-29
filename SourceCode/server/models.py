from sqlalchemy import Column, Integer, String, DateTime, Boolean, Text, create_engine
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker
from datetime import datetime
from config import settings
import hashlib

Base = declarative_base()

class ParkingSlot(Base):
    
    #Model cho slot đỗ xe
    __tablename__ = "parking_slots"
    
    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    slot_number = Column(String(10), unique=True, index=True, nullable=False)
    is_occupied = Column(Boolean, default=False)
    last_updated = Column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)

class VehicleLog(Base):
    # Model cho log xe ra vào
    __tablename__ = "vehicle_logs"
    
    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    license_plate = Column(String(20), index=True)
    image_path = Column(String(255))
    ocr_result = Column(String(1000))
    confidence = Column(String(10))
    timestamp = Column(DateTime, default=datetime.utcnow)
    action = Column(String(10))  # "entry" hoặc "exit"

class Admin(Base):
    # Model cho admin users
    __tablename__ = "admins"
    
    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    username = Column(String(50), unique=True, index=True, nullable=False)
    password_hash = Column(String(64), nullable=False)  # SHA256 hash
    full_name = Column(String(100))
    created_at = Column(DateTime, default=datetime.utcnow)
    last_login = Column(DateTime)
    is_active = Column(Boolean, default=True)
    is_super_admin = Column(Boolean, default=False)  # Super admin có quyền quản lý admin khác
    
    @staticmethod
    def hash_password(password: str) -> str:
        """Hash password using SHA256"""
        return hashlib.sha256(password.encode()).hexdigest()
    
    def verify_password(self, password: str) -> bool:
        """Verify password against hash"""
        return self.password_hash == self.hash_password(password)

class AdminActionLog(Base):
    """Model cho log các thao tác của admin"""
    __tablename__ = "admin_action_logs"
    
    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    admin_id = Column(Integer, nullable=False)  # ID của admin thực hiện
    admin_username = Column(String(50), nullable=False)  # Username để dễ query
    action_type = Column(String(50), nullable=False)  # Loại hành động: 'open_gate_manual', 'create_admin', etc.
    action_detail = Column(Text)  # Chi tiết thêm (JSON string hoặc text)
    ip_address = Column(String(50))  # IP của admin
    timestamp = Column(DateTime, default=datetime.utcnow, nullable=False)
    success = Column(Boolean, default=True)  # Thành công hay thất bại
    
    def __repr__(self):
        return f"<AdminActionLog {self.admin_username} - {self.action_type} at {self.timestamp}>"

# Tạo engine và session
engine = create_engine(
    settings.DATABASE_URL,
    pool_pre_ping=True,  # Kiểm tra connection trước khi dùng
    pool_recycle=3600,   # Recycle connection mỗi 1 giờ
    echo=False           # Set True để debug SQL queries
)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

def init_db():
    # Khởi tạo database
    Base.metadata.create_all(bind=engine)
    
    # Tạo super admin mặc định nếu chưa có
    db = SessionLocal()
    try:
        super_admin = db.query(Admin).filter(Admin.is_super_admin == True).first()
        if not super_admin:
            default_admin = Admin(
                username=settings.ADMIN_USERNAME,
                password_hash=Admin.hash_password(settings.ADMIN_PASSWORD),
                full_name="Super Administrator",
                is_active=True,
                is_super_admin=True  # Đánh dấu là super admin
            )
            db.add(default_admin)
            db.commit()
            print(f"[DATABASE] Created super admin user: {settings.ADMIN_USERNAME}")
    except Exception as e:
        print(f"[DATABASE] Error creating default admin: {e}")
        db.rollback()
    finally:
        db.close()

def get_db():
    # Dependency để lấy database session
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
