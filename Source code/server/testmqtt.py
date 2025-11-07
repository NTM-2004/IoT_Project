import paho.mqtt.client as mqtt

# --- Cấu hình ---
BROKER = "192.168.137.1"   # Mosquitto broker local
PORT = 1883
TOPIC = "iot/parking/slots" 
CLIENT_ID = "parking_subscriber"
QOS_LEVEL = 1

# --- Callback khi kết nối thành công ---
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ Connected to MQTT Broker!")
        client.subscribe(TOPIC, qos=QOS_LEVEL)
        print(f"📡 Subscribed to topic: {TOPIC} (QoS={QOS_LEVEL})")
    else:
        print(f"❌ Connection failed, code: {rc}")

# --- Callback khi nhận được message ---
def on_message(client, userdata, msg):
    message = msg.payload.decode()
    print(f"📨 Received message on {msg.topic}: {message}")

# --- Tạo client và gán callback ---
client = mqtt.Client(client_id=CLIENT_ID)
client.on_connect = on_connect
client.on_message = on_message

# --- Kết nối tới broker ---
print(f"🔌 Connecting to broker {BROKER}:{PORT} ...")
client.connect(BROKER, PORT, keepalive=60)

# --- Lắng nghe liên tục ---
print("🕐 Waiting for messages...")
client.loop_forever()
