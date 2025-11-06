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
    
    id = Column(Integer, primary_key=True, index=True)
    slot_number = Column(String, unique=True, index=True)
    is_occupied = Column(Boolean, default=False)
    last_updated = Column(DateTime, default=datetime.utcnow)

class VehicleLog(Base):
    """Model cho log xe ra vào"""
    __tablename__ = "vehicle_logs"
    
    id = Column(Integer, primary_key=True, index=True)
    license_plate = Column(String, index=True)
    image_path = Column(String)
    ocr_result = Column(String)
    confidence = Column(String)
    timestamp = Column(DateTime, default=datetime.utcnow)
    action = Column(String)  # "entry" hoặc "exit"

# Tạo engine và session
engine = create_engine(settings.DATABASE_URL, connect_args={"check_same_thread": False})
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
