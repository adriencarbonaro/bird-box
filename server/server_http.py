import os
import random
from http.server import BaseHTTPRequestHandler, HTTPServer

AUDIO_DIR = "audio-files/wav"

def get_random_wav():
    # List WAV files only
    files = [f for f in os.listdir(AUDIO_DIR) if f.lower().endswith(".wav")]
    if not files:
        raise RuntimeError("No WAV files found in wav-files directory")
    return os.path.join(AUDIO_DIR, random.choice(files))

class SimpleHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        try:
            file_path = get_random_wav()
            file_size = os.path.getsize(file_path)
            print("Serving file:", file_path, "| size:", file_size)

            self.send_response(200)
            self.send_header("Content-Type", "audio/wav")
            self.send_header("Content-Length", str(file_size))
            self.send_header("Connection", "keep-alive")
            self.end_headers()

            # Stream file in chunks (e.g. 4 KB)
            with open(file_path, "rb") as f:
                chunk_size = 4096
                while True:
                    data = f.read(chunk_size)
                    if not data:
                        break
                    self.wfile.write(data)

            print("File sent successfully.")

        except Exception as e:
            print("Error sending file:", e)
            self.send_error(500, str(e))

server = HTTPServer(("0.0.0.0", 8000), SimpleHandler)
print("Serving on port 8000...")
server.serve_forever()
