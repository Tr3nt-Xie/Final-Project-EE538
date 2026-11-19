#!/usr/bin/env python3
"""Minimal HTTP backend for local testing of AegisProxy.

Not part of the product: it exists so that three developers can run a realistic
topology (proxy + N backends) without writing a C++ server first.

    python3 simple_backend.py --port 9001 --name app-1 [--delay-ms 0] [--fail-after N]

Routes:
    GET /health  -> 200 "ok"           (what HealthMonitor will probe)
    anything     -> 200 JSON echo with the backend name, path and a request counter
    --fail-after N: after N requests, /health returns 503 (simulates an unhealthy backend)
"""
import argparse
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--name", default="backend")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--delay-ms", type=int, default=0)
    parser.add_argument("--fail-after", type=int, default=0)
    args = parser.parse_args()

    counter = {"n": 0}

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def _send(self, status: int, body: bytes, ctype: str = "application/json") -> None:
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("X-Backend", args.name)
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:  # noqa: N802 (http.server API)
            counter["n"] += 1
            if args.delay_ms:
                time.sleep(args.delay_ms / 1000.0)
            if self.path == "/health":
                unhealthy = args.fail_after and counter["n"] > args.fail_after
                self._send(503 if unhealthy else 200, b"unhealthy\n" if unhealthy else b"ok\n", "text/plain")
                return
            payload = {"backend": args.name, "path": self.path, "request": counter["n"]}
            self._send(200, (json.dumps(payload) + "\n").encode())

        def do_POST(self) -> None:  # noqa: N802
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length) if length else b""
            counter["n"] += 1
            payload = {"backend": args.name, "path": self.path, "echo_bytes": len(body)}
            self._send(200, (json.dumps(payload) + "\n").encode())

        def log_message(self, fmt: str, *a: object) -> None:
            print(f"[{args.name}] " + (fmt % a))

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"[{args.name}] listening on {args.host}:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
