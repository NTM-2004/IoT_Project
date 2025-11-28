import requests
from config import settings

class OCRService:
    def __init__(self):
        self.api_url = settings.OCR_API_URL
        self.api_key = settings.OCR_API_KEY
        self.regions = ["vn"]  
    
    def recognize_plate(self, image_bytes):

        try:
            files = {'upload': ('image.jpg', image_bytes, 'image/jpeg')}
            headers = {'Authorization': f'{self.api_key}'}
            data = {'regions': self.regions}  # Optional: specify regions
            
            print(f"[OCR] Sending request to {self.api_url}...")
            print(f"[OCR] Regions: {self.regions}")
            
            response = requests.post(
                self.api_url,
                data=data,
                files=files,
                headers=headers,
                timeout=30
            )
            
            if response.status_code == 200 or response.status_code == 201:
                result = response.json()
                print(f"[OCR] SUCCESS: {result}")
                return self._parse_result(result)
            else:
                print(f"[OCR] ERROR {response.status_code}: {response.text}")
                return None
        
        except Exception as e:
            print(f"[OCR] Exception: {e}")
            return None
    
    def _parse_result(self, result):
        try:
            if 'results' in result and len(result['results']) > 0:
                plate_data = result['results'][0]
                
                return {
                    'plate': plate_data.get('plate', ''),
                    'confidence': plate_data.get('score', 0),
                    'region': plate_data.get('region', {}).get('code', ''),
                    'vehicle_type': plate_data.get('vehicle', {}).get('type', ''),
                    'raw_result': result
                }
            
            return {
                'plate': 'UNKNOWN',
                'confidence': 0,
                'region': '',
                'vehicle_type': '',
                'raw_result': result
            }
        
        except Exception as e:
            print(f"[OCR] Error parsing result: {e}")
            return None

# Singleton instance
ocr_service = OCRService()
