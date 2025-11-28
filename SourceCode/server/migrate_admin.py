"""
Migration script to add is_super_admin column to admins table
"""
from models import engine
from sqlalchemy import text

def migrate():
    try:
        conn = engine.connect()
        
        # Check if column already exists
        result = conn.execute(text(
            "SHOW COLUMNS FROM admins LIKE 'is_super_admin'"
        ))
        
        if result.fetchone():
            print("[MIGRATION] Column 'is_super_admin' already exists")
        else:
            # Add the column
            conn.execute(text(
                "ALTER TABLE admins ADD COLUMN is_super_admin TINYINT(1) DEFAULT 0 AFTER is_active"
            ))
            print("[MIGRATION] Added column 'is_super_admin'")
            
            # Set existing admin as super admin
            conn.execute(text(
                "UPDATE admins SET is_super_admin = 1 WHERE username = 'admin'"
            ))
            print("[MIGRATION] Set 'admin' as super admin")
        
        conn.commit()
        conn.close()
        print("[MIGRATION] ✓ Migration completed successfully")
        
    except Exception as e:
        print(f"[MIGRATION] ✗ Migration failed: {e}")

if __name__ == "__main__":
    migrate()
