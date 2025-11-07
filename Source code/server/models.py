"""
Database Models
"""
from sqlalchemy import Column, Integer, String, DateTime, Boolean, create_engine
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker
from datetime import datetime
from config import settings

Base = declarative_base()

class ParkingSlot(Base):
    """Model cho slot đỗ xe"""
    __tablename__ = "parking_slots"
    
    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    slot_number = Column(String(10), unique=True, index=True, nullable=False)
    is_occupied = Column(Boolean, default=False)
    last_updated = Column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)

class VehicleLog(Base):
    """Model cho log xe ra vào"""
    __tablename__ = "vehicle_logs"
    
    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    license_plate = Column(String(20), index=True)
    image_path = Column(String(255))
    ocr_result = Column(String(1000))
    confidence = Column(String(10))
    timestamp = Column(DateTime, default=datetime.utcnow)
    action = Column(String(10))  # "entry" hoặc "exit"

# Tạo engine và session
# Bỏ check_same_thread vì MySQL không cần (chỉ SQLite mới cần)
engine = create_engine(
    settings.DATABASE_URL,
    pool_pre_ping=True,  # Kiểm tra connection trước khi dùng
    pool_recycle=3600,   # Recycle connection mỗi 1 giờ
    echo=False           # Set True để debug SQL queries
)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

def init_db():
    """Khởi tạo database"""
    Base.metadata.create_all(bind=engine)

def get_db():
    """Dependency để lấy database session"""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
