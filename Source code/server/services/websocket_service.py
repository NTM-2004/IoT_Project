import asyncio
from fastapi import WebSocket
from typing import List, Dict, Any

class WebSocketService:
    def __init__(self):
        self.clients: List[WebSocket] = []
        self.pending_broadcasts: List[Dict[str, Any]] = []
        self._worker_task = None
    
    async def connect(self, websocket: WebSocket):
        # Thêm client mới
        await websocket.accept()
        self.clients.append(websocket)
        print(f"[WS] Client connected (total: {len(self.clients)})")
    
    def disconnect(self, websocket: WebSocket):
        # Xóa client
        if websocket in self.clients:
            self.clients.remove(websocket)
            print(f"[WS] Client disconnected (total: {len(self.clients)})")
    
    async def broadcast(self, message: Dict[str, Any]):
        # Broadcast message đến tất cả clients
        disconnected = []
        for client in self.clients:
            try:
                await client.send_json(message)
            except:
                disconnected.append(client)
        
        # Remove disconnected clients
        for client in disconnected:
            self.disconnect(client)
    
    def queue_broadcast(self, message: Dict[str, Any]):
        # Thêm message vào queue 
        self.pending_broadcasts.append(message)
        print(f"[WEBSOCKET] Queued broadcast: {message.get('type')}")
    
    async def broadcast_worker(self):
        # Background task để xử lý queue broadcast
        while True:
            try:
                if self.pending_broadcasts:
                    # Lấy message từ queue
                    message = self.pending_broadcasts.pop(0)
                    await self.broadcast(message)
                    print(f"[WEBSOCKET] Broadcasted: {message.get('type')}")
                
                # Chờ 0.1s trước khi check lại
                await asyncio.sleep(0.1)
            except Exception as e:
                print(f"[WEBSOCKET] Broadcast worker error: {e}")
                await asyncio.sleep(1)
    
    def start_worker(self):
        # Khởi động background worker
        self._worker_task = asyncio.create_task(self.broadcast_worker())
        print("[WEBSOCKET] SUCCESS WebSocket broadcast worker started")
    
    async def stop_worker(self):
        # Dừng background worker
        if self._worker_task:
            self._worker_task.cancel()
            try:
                await self._worker_task
            except asyncio.CancelledError:
                pass

# Singleton instance
websocket_service = WebSocketService()
