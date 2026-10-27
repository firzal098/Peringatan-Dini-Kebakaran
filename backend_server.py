"""
=============================================================================
 SISTEM PERINGATAN DINI KEBAKARAN HUTAN (KARHUTLA) — LOCAL BACKEND SERVER
 Tim : Testarossa's Vanguard — Universitas Ahmad Dahlan
=============================================================================
 Skrip ini menjalankan backend MQTT & REST API lokal untuk menerima telemetri
 dari Wokwi ESP32, mengevaluasi risiko kebakaran, mengirim perintah kendali
 balik, dan menyajikan dashboard.html di browser (http://localhost:8000).
=============================================================================
 Dependensi:
   pip install paho-mqtt
 Jalankan:
   python backend_server.py
=============================================================================
"""

import os
import sys
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
import time
import json
import threading
from datetime import datetime
from zoneinfo import ZoneInfo
from collections import deque
from http.server import HTTPServer, BaseHTTPRequestHandler
import webbrowser

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("❌ Library 'paho-mqtt' belum terpasang!")
    print("👉 Silakan jalankan: pip install paho-mqtt")
    sys.exit(1)

# ── KONFIGURASI ───────────────────────────────────────────────────────────────
MQTT_BROKER     = "broker.hivemq.com"
MQTT_PORT       = 1883
MQTT_CLIENT_ID  = f"local_vanguard_backend_{int(time.time())}"
TOPIC_TELEMETRY = "testarossas_vanguard/fire/karhutla/telemetry"
TOPIC_CONTROL   = "testarossas_vanguard/fire/karhutla/control"

HTTP_HOST = "127.0.0.1"
HTTP_PORT = 8000
WIB = ZoneInfo("Asia/Jakarta")

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DASHBOARD_FILE = os.path.join(BASE_DIR, "frontend", "dashboard.html")

# ── NODE REGISTRY (GEOLOKASI DIKELOLA SERVER) ─────────────────────────────────
# Server meregistrasikan ID perangkat IoT ke lokasi fisik koordinatnya.
# Perangkat IoT (Wokwi) tidak menyimpan GPS/lokasi, server yang melampirkan metadata ini.
NODE_REGISTRY = {
    "ESP32_FIRE_NODE_01": {
        "device_id": "ESP32_FIRE_NODE_01",
        "name": "Node 01 — Lab Riset IoT, UAD Kampus 4",
        "region": "Yogyakarta",
        "lat": -7.833776,
        "lng": 110.383460
    }
}

# ── DATA STORAGE ──────────────────────────────────────────────────────────────
data_lock = threading.Lock()
MAX_HISTORY = 100
telemetry_history = deque(maxlen=MAX_HISTORY)

active_nodes = {
    "ESP32_FIRE_NODE_01": {
        **NODE_REGISTRY["ESP32_FIRE_NODE_01"],
        "temperature": 27.0,
        "humidity": 65.0,
        "soil_moisture": 52.0,
        "smoke_ppm": 90,
        "status": "Normal",
        "risk_index": 18,
        "updated": "Menunggu data...",
        "last_seen": None
    }
}

latest_state = {
    **NODE_REGISTRY["ESP32_FIRE_NODE_01"],
    "temperature": 27.0,
    "humidity": 65.0,
    "soil_moisture": 52.0,
    "smoke_ppm": 90,
    "status": "Normal",
    "risk_index": 18,
    "mqtt_connected": False,
    "messages_received": 0,
    "last_telemetry_time": None,
    "control_override": None
}

# ── DECISION ENGINE ───────────────────────────────────────────────────────────
def evaluate_risk(temp, hum, soil, smoke):
    danger_combined = (temp >= 40.0 and soil <= 20.0 and smoke >= 400.0)
    danger_smoke    = (smoke >= 600.0)
    danger_heat     = (temp >= 45.0 and smoke >= 250.0)
    if danger_combined or danger_smoke or danger_heat:
        return "Danger"
    if temp >= 32.0 or soil <= 35.0 or smoke >= 200.0 or hum < 45.0:
        return "Warning"
    return "Normal"

def calculate_risk_percentage(temp, hum, soil, smoke):
    temp_score = min(100, max(0, ((temp - 20) / 30) * 100))
    soil_dry_score = min(100, max(0, 100 - soil))
    smoke_score = min(100, max(0, (smoke / 350) * 100))
    risk = round((temp_score * 0.35) + (soil_dry_score * 0.25) + (smoke_score * 0.40))
    return min(100, max(5, risk))

# ── MQTT CLIENT ───────────────────────────────────────────────────────────────
mqtt_client = mqtt.Client(
    callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
    client_id=MQTT_CLIENT_ID
)

def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        with data_lock:
            latest_state["mqtt_connected"] = True
        print(f"✅ [MQTT] Terhubung ke {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe(TOPIC_TELEMETRY)
        print(f"📡 [MQTT] Berlangganan topik: '{TOPIC_TELEMETRY}'")
    else:
        print(f"❌ [MQTT] Gagal terhubung (rc={reason_code})")

def on_message(client, userdata, message):
    try:
        payload_raw = message.payload.decode("utf-8").strip()
        data = json.loads(payload_raw)
        now = datetime.now(WIB)

        temp  = float(data.get("temperature", 25.0))
        hum   = float(data.get("humidity", 65.0))
        soil  = float(data.get("soil_moisture", 50.0))
        smoke = float(data.get("smoke_ppm", 90.0))
        dev_id= str(data.get("device_id", "ESP32_FIRE_NODE_01"))

        with data_lock:
            # Ambil metadata lokasi dari NODE_REGISTRY server
            if dev_id not in NODE_REGISTRY:
                NODE_REGISTRY[dev_id] = {
                    "device_id": dev_id,
                    "name": f"Node ({dev_id})",
                    "region": "Indonesia",
                    "lat": -7.833776,
                    "lng": 110.383460
                }
            node_meta = NODE_REGISTRY[dev_id]

            override = latest_state.get("control_override")
            if override and override != "AUTO":
                status = override
            else:
                status = evaluate_risk(temp, hum, soil, smoke)

            risk_pct = calculate_risk_percentage(temp, hum, soil, smoke)

            record = {
                "timestamp": now.strftime("%Y-%m-%d %H:%M:%S"),
                "device_id": dev_id,
                "name": node_meta["name"],
                "region": node_meta["region"],
                "lat": node_meta["lat"],
                "lng": node_meta["lng"],
                "temperature": round(temp, 1),
                "humidity": round(hum, 1),
                "soil_moisture": round(soil, 1),
                "smoke_ppm": int(smoke),
                "status": status,
                "risk_index": risk_pct
            }

            active_nodes[dev_id] = {
                **record,
                "updated": "Baru saja",
                "last_seen": now.strftime("%H:%M:%S WIB")
            }

            latest_state.update(record)
            latest_state["messages_received"] += 1
            latest_state["last_telemetry_time"] = now.strftime("%H:%M:%S WIB")
            telemetry_history.append(record)

        # Kirim instruksi balik ke ESP32 Wokwi
        resp_cmd = json.dumps({"device_id": dev_id, "status": status, "risk": status, "source": "Local_Backend"})
        client.publish(TOPIC_CONTROL, resp_cmd)

        print(f"[{now.strftime('%H:%M:%S')}] Telemetri [{dev_id} @ {node_meta['name']}] -> Suhu: {temp:.1f}°C | Tanah: {soil:.1f}% | Asap: {smoke:.0f}ppm | Status: [{status}]")

    except Exception as exc:
        print(f"⚠️ [MQTT] Gagal membaca data: {exc}")

mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.reconnect_delay_set(min_delay=1, max_delay=30)

# ── HTTP REST API HANDLER ─────────────────────────────────────────────────────
class DashboardHTTPHandler(BaseHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()

    def do_GET(self):
        path = self.path.split('?')[0]
        if path in ['/', '/index.html']:
            if not os.path.exists(DASHBOARD_FILE):
                self.send_error(404, f"File {DASHBOARD_FILE} tidak ditemukan!")
                return
            with open(DASHBOARD_FILE, 'rb') as f:
                content = f.read()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(content)))
            self.end_headers()
            self.wfile.write(content)

        elif path == '/api/nodes':
            with data_lock:
                payload = json.dumps(list(active_nodes.values())).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        elif path == '/api/telemetry/latest':
            with data_lock:
                state_copy = dict(latest_state)
                state_copy["nodes"] = list(active_nodes.values())
                payload = json.dumps(state_copy).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        elif path == '/api/telemetry/history':
            with data_lock:
                payload = json.dumps(list(telemetry_history)).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        elif path == '/api/health':
            payload = json.dumps({
                "status": "ok",
                "mqtt": latest_state["mqtt_connected"],
                "received": latest_state["messages_received"],
                "registered_nodes": len(NODE_REGISTRY)
            }).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        else:
            self.send_error(404, "Endpoint tidak ditemukan")

    def do_POST(self):
        path = self.path.split('?')[0]
        content_len = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_len).decode('utf-8') if content_len > 0 else '{}'
        
        try:
            data = json.loads(body)
        except Exception:
            data = {}

        now = datetime.now(WIB)

        if path == '/api/nodes/location':
            dev_id = str(data.get("device_id", "ESP32_FIRE_NODE_01"))
            with data_lock:
                if dev_id not in NODE_REGISTRY:
                    NODE_REGISTRY[dev_id] = {
                        "device_id": dev_id,
                        "name": f"Node ({dev_id})",
                        "region": "Indonesia",
                        "lat": -7.833776,
                        "lng": 110.383460
                    }
                if "lat" in data: NODE_REGISTRY[dev_id]["lat"] = float(data["lat"])
                if "lng" in data: NODE_REGISTRY[dev_id]["lng"] = float(data["lng"])
                if "name" in data: NODE_REGISTRY[dev_id]["name"] = str(data["name"])
                if "region" in data: NODE_REGISTRY[dev_id]["region"] = str(data["region"])

                if dev_id in active_nodes:
                    active_nodes[dev_id].update(NODE_REGISTRY[dev_id])
                if latest_state.get("device_id") == dev_id:
                    latest_state.update(NODE_REGISTRY[dev_id])

            resp = json.dumps({"success": True, "node": NODE_REGISTRY[dev_id]}).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(resp)))
            self.end_headers()
            self.wfile.write(resp)

        elif path == '/api/telemetry/inject':
            temp  = float(data.get("temperature", 25.0))
            hum   = float(data.get("humidity", 65.0))
            soil  = float(data.get("soil_moisture", 50.0))
            smoke = float(data.get("smoke_ppm", 90.0))
            dev_id= str(data.get("device_id", "ESP32_FIRE_NODE_01"))
            status = data.get("status") or evaluate_risk(temp, hum, soil, smoke)
            risk_pct = calculate_risk_percentage(temp, hum, soil, smoke)

            with data_lock:
                if dev_id not in NODE_REGISTRY:
                    NODE_REGISTRY[dev_id] = {
                        "device_id": dev_id,
                        "name": f"Node ({dev_id})",
                        "region": "Indonesia",
                        "lat": -7.833776,
                        "lng": 110.383460
                    }
                node_meta = NODE_REGISTRY[dev_id]

                record = {
                    "timestamp": now.strftime("%Y-%m-%d %H:%M:%S"),
                    "device_id": dev_id,
                    "name": node_meta["name"],
                    "region": node_meta["region"],
                    "lat": node_meta["lat"],
                    "lng": node_meta["lng"],
                    "temperature": round(temp, 1),
                    "humidity": round(hum, 1),
                    "soil_moisture": round(soil, 1),
                    "smoke_ppm": int(smoke),
                    "status": status,
                    "risk_index": risk_pct
                }

                active_nodes[dev_id] = {
                    **record,
                    "updated": "Baru saja",
                    "last_seen": now.strftime("%H:%M:%S WIB")
                }

                latest_state.update(record)
                latest_state["messages_received"] += 1
                latest_state["last_telemetry_time"] = now.strftime("%H:%M:%S WIB")
                telemetry_history.append(record)

            mqtt_client.publish(TOPIC_CONTROL, json.dumps({"device_id": dev_id, "status": status, "risk": status, "source": "Manual_Inject"}))

            resp = json.dumps({"success": True, "data": record}).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(resp)))
            self.end_headers()
            self.wfile.write(resp)

        elif path == '/api/control':
            mode = data.get("mode")
            with data_lock:
                if mode == "AUTO":
                    latest_state["control_override"] = "AUTO"
                    status = evaluate_risk(
                        latest_state["temperature"], latest_state["humidity"],
                        latest_state["soil_moisture"], latest_state["smoke_ppm"]
                    )
                else:
                    status = data.get("status") or "Normal"
                    latest_state["control_override"] = status
                latest_state["status"] = status

            mqtt_client.publish(TOPIC_CONTROL, json.dumps({"status": status, "risk": status, "source": "Web_Override"}))

            resp = json.dumps({"success": True, "status": status}).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(resp)))
            self.end_headers()
            self.wfile.write(resp)
        else:
            self.send_error(404, "Endpoint tidak ditemukan")

    def log_message(self, format, *args):
        # Mute default HTTP access logs to keep console clean
        return

# ── MAIN RUNNER ───────────────────────────────────────────────────────────────
def main():
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║  PERINGATAN DINI KEBAKARAN HUTAN — LOCAL BACKEND SERVER      ║")
    print("║  Tim : Testarossa's Vanguard — Universitas Ahmad Dahlan      ║")
    print("╚══════════════════════════════════════════════════════════════╝")

    print(f"\n[1] Menghubungkan ke MQTT Broker: {MQTT_BROKER}:{MQTT_PORT}...")
    try:
        mqtt_client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=60)
        mqtt_client.loop_start()
    except Exception as exc:
        print(f"❌ Gagal koneksi ke MQTT: {exc}")

    server_address = (HTTP_HOST, HTTP_PORT)
    httpd = HTTPServer(server_address, DashboardHTTPHandler)
    url = f"http://localhost:{HTTP_PORT}"
    print(f"[2] Server HTTP & REST API aktif di: {url}")
    print(f"[3] Menghubungkan ke dashboard: {DASHBOARD_FILE}")
    print("\nTekan Ctrl+C untuk menghentikan server.\n")

    # Buka browser secara otomatis
    try:
        webbrowser.open(url)
    except Exception:
        pass

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[STOP] Mematikan server...")
    finally:
        mqtt_client.loop_stop()
        httpd.server_close()
        print("Server selesai.")

if __name__ == "__main__":
    main()
