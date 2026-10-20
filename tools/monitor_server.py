"""Serve the local world dashboard and JSON snapshots using only Python's standard library."""
import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "monitoring"
MAX_MESSAGE_BYTES = 2 * 1024 * 1024


def reject_constant(value):
    raise ValueError(f"Invalid JSON number: {value}")


def validate_message(raw):
    if len(raw) > MAX_MESSAGE_BYTES:
        raise ValueError("JSON message is larger than 2 MB")
    data = json.loads(raw, parse_constant=reject_constant)
    if not isinstance(data, dict) or data.get("schemaVersion") != 1:
        raise ValueError("schemaVersion must be 1")
    for key in ("counts", "player", "society"):
        if not isinstance(data.get(key), dict):
            raise ValueError(f"{key} must be an object")
    for key in ("npcs", "towns", "monsters", "news"):
        if not isinstance(data.get(key), list):
            raise ValueError(f"{key} must be an array")
    if not isinstance(data.get("session"), str) or not isinstance(data.get("timestampMs"), (int, float)):
        raise ValueError("session and timestampMs are required")
    if data["counts"].get("npcs") != len(data["npcs"]) or data["counts"].get("monsters") != len(data["monsters"]):
        raise ValueError("Population counts must match the arrays")
    for collection in ("npcs", "towns", "monsters"):
        for item in data[collection]:
            if not isinstance(item, dict) or "id" not in item or not isinstance(item.get("position"), dict):
                raise ValueError(f"Invalid {collection} entry")
            if not all(isinstance(item["position"].get(axis), (int, float)) for axis in ("x", "y")):
                raise ValueError("Positions require numeric x and y")
    return json.dumps(data, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")


class SnapshotSource:
    def __init__(self, state_path, receive):
        self.path = state_path
        self.receive = receive
        self.lock = threading.Lock()
        self.message = None
        self.signature = None
        self.candidates = [ROOT / "monitoring/state.json"]
        for base in (ROOT, ROOT / "SimpleGame"):
            for folder in ("x64/Debug", "x64/Release", "Debug", "Release", "ARM64/Debug", "ARM64/Release"):
                self.candidates.append(base / folder / "monitoring/state.json")

    def read(self):
        with self.lock:
            if self.receive:
                if self.message is None:
                    raise FileNotFoundError("JSON 메시지 대기 중 / POST /api/state")
                return self.message
            path = self.path
            if path is None:
                available = []
                for candidate in self.candidates:
                    try:
                        available.append((candidate.stat().st_mtime_ns, candidate))
                    except FileNotFoundError:
                        pass
                if not available:
                    raise FileNotFoundError("게임 상태 대기 중 / 게임을 실행하거나 --state로 state.json 경로를 지정하세요")
                path = max(available, key=lambda item: item[0])[1]
            signature = (str(path), path.stat().st_mtime_ns)
            if signature != self.signature:
                # C++ atomically replaces this file. Never cache a partially read message.
                with path.open("rb") as stream:
                    raw = stream.read(MAX_MESSAGE_BYTES + 1)
                message = validate_message(raw)
                self.message = message
                self.signature = signature
            return self.message

    def accept(self, raw):
        message = validate_message(raw)
        with self.lock:
            self.message = message


class Handler(BaseHTTPRequestHandler):
    def send(self, status, body, content_type="application/json; charset=utf-8"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def error_json(self, status, message):
        self.send(status, json.dumps({"error": message}, ensure_ascii=False).encode("utf-8"))

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/api/state":
            try:
                self.send(200, self.server.source.read())
            except (OSError, ValueError, UnicodeError) as error:
                self.error_json(503, str(error))
            return
        assets = {"/": ("index.html", "text/html; charset=utf-8"),
                  "/index.html": ("index.html", "text/html; charset=utf-8"),
                  "/dashboard.css": ("dashboard.css", "text/css; charset=utf-8"),
                  "/dashboard.js": ("dashboard.js", "text/javascript; charset=utf-8")}
        if path not in assets:
            self.error_json(404, "Not found")
            return
        name, content_type = assets[path]
        try:
            self.send(200, (WEB / name).read_bytes(), content_type)
        except OSError as error:
            self.error_json(500, str(error))

    def do_POST(self):
        if urlsplit(self.path).path != "/api/state" or not self.server.source.receive:
            self.error_json(405, "JSON 메시지 수신은 --receive 모드에서 사용합니다")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > MAX_MESSAGE_BYTES:
                self.error_json(413, "메시지는 1바이트~2MB여야 합니다")
                return
            if self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
                self.error_json(415, "Content-Type: application/json 필요")
                return
            self.connection.settimeout(3)
            raw = self.rfile.read(length)
            if len(raw) != length:
                raise ValueError("Incomplete JSON message")
            self.server.source.accept(raw)
            self.send(200, b'{"accepted":true}')
        except (ValueError, UnicodeError, OSError) as error:
            self.error_json(400, str(error))

    def log_message(self, format_string, *args):
        # Avoid filling the console with two successful poll requests every second.
        if len(args) > 1 and str(args[1]) not in ("200", "503"):
            super().log_message(format_string, *args)


def main():
    parser = argparse.ArgumentParser(description="녹스 로컬 월드 모니터")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--state", type=Path, help="실행 파일 옆 monitoring/state.json 경로")
    mode.add_argument("--receive", action="store_true", help="파일 대신 POST /api/state JSON 메시지 수신")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    server.source = SnapshotSource(args.state.resolve() if args.state else None, args.receive)
    print(f"월드 모니터: http://127.0.0.1:{args.port}")
    print("JSON 수신 모드" if args.receive else f"JSON 파일: {args.state or '표준 빌드 경로 자동 탐색'}")
    print("종료: Ctrl+C")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
