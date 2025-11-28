"""
Shared session manager for multiple servers
"""
import secrets
from datetime import datetime, timedelta
from typing import Optional, Dict, Any
from config import settings

# Global session storage (shared across imports)
sessions: Dict[str, Dict[str, Any]] = {}

def create_session(username: str, is_super_admin: bool = False) -> str:
    """Create a new session for user"""
    session_id = secrets.token_urlsafe(32)
    sessions[session_id] = {
        "username": username,
        "is_super_admin": is_super_admin,
        "created_at": datetime.now(),
        "expires_at": datetime.now() + timedelta(minutes=settings.SESSION_EXPIRE_MINUTES)
    }
    return session_id

def verify_session(session_id: Optional[str]) -> bool:
    """Verify if session is valid"""
    if not session_id or session_id not in sessions:
        return False
    
    session = sessions[session_id]
    if datetime.now() > session["expires_at"]:
        del sessions[session_id]
        return False
    
    return True

def verify_super_admin(session_id: Optional[str]) -> bool:
    """Verify if session belongs to super admin"""
    if not verify_session(session_id):
        return False
    
    session = sessions.get(session_id)
    return session.get("is_super_admin", False) if session else False

def get_session(session_id: Optional[str]) -> Optional[Dict[str, Any]]:
    """Get session data"""
    if verify_session(session_id):
        return sessions.get(session_id)
    return None

def delete_session(session_id: Optional[str]) -> bool:
    """Delete a session"""
    if session_id and session_id in sessions:
        del sessions[session_id]
        return True
    return False
