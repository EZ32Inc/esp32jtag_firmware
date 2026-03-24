from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import time
import threading
import random

state = {
    "capturing": False,
    "triggered": False,
    "all_captured": False,
    "start_time": 0,
    "trigger_delay": 1.0,      # seconds until "triggered"
    "capture_delay": 2.0,      # seconds until "all_captured"
    "trigger_position": 0      # sample index where trigger happened
}

def run_capture_simulator():
    """Simulate trigger and capture completion like real ESP32."""
    while True:
        if state["capturing"]:
            now = time.time()
            elapsed = now - state["start_time"]

            if elapsed >= state["trigger_delay"] and not state["triggered"]:
                state["triggered"] = True
                # Simulate a random trigger position
                state["trigger_position"] = random.randint(100, 1900)

            if elapsed >= state["capture_delay"]:
                state["all_captured"] = True
                state["capturing"] = False

        time.sleep(0.05)


class MockESP32(BaseHTTPRequestHandler):

    # --- CORS Support ---
    def send_cors_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_cors_headers()
        self.end_headers()

    # Helper to send JSON with CORS
    def send_json(self, obj):
        body = json.dumps(obj).encode()
        self.send_response(200)
        self.send_cors_headers()
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    # --- REST API ---
    def do_GET(self):

        # 1. /la_start_capture
        if self.path.startswith("/la_start_capture"):
            state["capturing"] = True
            state["triggered"] = False
            state["all_captured"] = False
            state["start_time"] = time.time()

            self.send_json({
                "capture_started": True,
                "triggered": False,
                "all_captured": False
            })
            return

        # 2. /la_status
        if self.path.startswith("/la_status"):
            self.send_json({
                "triggered": state["triggered"],
                "all_captured": state["all_captured"],
                "trigger_position": state["trigger_position"]
            })
            return

        # 3. /la_get_data
        if self.path.startswith("/la_get_data"):
            fake_data = [random.randint(0, 255) for _ in range(2048)]
            self.send_json({
                "data": fake_data,
                "samples": 2048,
                "channels": 8
            })
            return

        # 4. /la_get_info
        if self.path.startswith("/la_get_info"):
            self.send_json({
                "model": "ESP32 Logic Analyzer Mock",
                "channels": 8,
                "buffer": 4096,
                "max_sample_rate": 50000000
            })
            return

        # Default
        self.send_json({"error": "Unknown endpoint"})

    def do_POST(self):
        if self.path.startswith("/la_configure"):
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            try:
                config = json.loads(post_data)
                print(f"Received configuration: {config}")
                
                # Update state based on config if needed, for now just log it
                # In a real scenario, we might update sample rates or trigger settings here
                
                self.send_json({"status": "ok", "config_received": config})
            except json.JSONDecodeError:
                self.send_response(400)
                self.end_headers()
                self.wfile.write(b"Invalid JSON")
            return
        
        self.send_response(404)
        self.end_headers()


# Start background simulator
threading.Thread(target=run_capture_simulator, daemon=True).start()

print("Mock ESP32 API running at http://localhost:8001")
HTTPServer(("localhost", 8001), MockESP32).serve_forever()

