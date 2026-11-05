#!/usr/bin/env python3
"""Serves JSON the way api.spring.io did for test_remote_head_forbidden.test.

HEAD is refused with 401, GET answers 200 with a chunked body and no
Content-Length, and a Range header is ignored. The tests used to read
api.spring.io itself, which now and then answers GET with 404.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BODY = json.dumps(
    {
        "_embedded": {
            "generations": [
                {
                    "name": f"{major}.{minor}.x",
                    "initialRelease": f"20{10 + major}-0{minor + 1}-01",
                    "id": major * 10 + minor,
                }
                for major in range(1, 5)
                for minor in range(0, 8)
            ]
        }
    },
    indent=2,
).encode()

CHUNK_SIZE = 1024


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_HEAD(self):
        self.send_response(401)
        self.send_header("Content-Type", "application/json")
        self.send_header("WWW-Authenticate", 'Basic realm="Realm"')
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        if self.path.split("?")[0] != "/generations":
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/hal+json")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        for start in range(0, len(BODY), CHUNK_SIZE):
            chunk = BODY[start : start + CHUNK_SIZE]
            self.wfile.write(b"%x\r\n%s\r\n" % (len(chunk), chunk))
        self.wfile.write(b"0\r\n\r\n")

    def log_message(self, format, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8009
    ThreadingHTTPServer(("localhost", port), Handler).serve_forever()
