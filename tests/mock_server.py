"""Local CPAMP fixtures. No real credentials or external services are used."""
import json
import threading
import socket
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit


def health():
    files = []
    for index, used in enumerate((20, 60)):
        files.append({"provider": "codex", "name": f"account{index}.json",
                      "model_quotas": {"model": {"observed_at": "2026-10-21T10:00:00Z", "signals": {
                          "X-Codex-Primary-Window-Minutes": "300",
                          "X-Codex-Primary-Used-Percent": str(used),
                          "X-Codex-Primary-Reset-At": "1792602000"}}}})
    return {"files": files}


class Handler(BaseHTTPRequestHandler):
    lifecycle_requests = 0
    lifecycle_lock = threading.Lock()
    retry_requests = {}
    retry_lock = threading.Lock()

    def attempt(self):
        with Handler.retry_lock:
            key = (self.command, self.path)
            Handler.retry_requests[key] = Handler.retry_requests.get(key, 0) + 1
            return Handler.retry_requests[key]

    def invalid_response(self):
        # A real malformed HTTP status line, rather than a mocked WinHTTP return code.
        self.connection.sendall(b'HTTP/1.1 invalid-status\r\nContent-Length: 0\r\n\r\n')
        self.close_connection = True
        self.connection.shutdown(socket.SHUT_WR)

    def reply(self, code, data):
        payload = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        path = urlsplit(self.path).path
        scenario = path.split('/')[1]
        if self.headers.get("Authorization") != "Bearer integration-key" or self.headers.get("X-API-Key") != "integration-key":
            return self.reply(401, {})
        if scenario in ('transient', 'persistent', 'no-retry', 'transient-timeout'):
            attempt = self.attempt()
            if scenario == 'transient-timeout' and attempt == 1:
                time.sleep(9)
                try:
                    return self.reply(200, health())
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    return
            if scenario == 'persistent' or (scenario == 'transient' and attempt < 3):
                return self.invalid_response()
            if scenario == 'no-retry':
                # A second attempt would incorrectly succeed, exposing accidental HTTP 401 retries.
                return self.reply(401, {}) if attempt == 1 else self.reply(200, health())
            return self.reply(200, health())
        if scenario == 'retry-check':
            with Handler.retry_lock:
                expected = {
                    ('GET', '/transient/v0/management/auth-files'): 3,
                    ('GET', '/persistent/v0/management/auth-files'): 3,
                    ('GET', '/transient-timeout/v0/management/auth-files'): 2,
                    ('GET', '/no-retry/v0/management/auth-files'): 1,
                    ('GET', '/no-retry/api/admin/manager-data'): 1,
                    ('GET', '/no-retry/api/v1/admin/manager-data'): 1,
                    ('POST', '/transient-post/v0/management/quota-snapshots/query'): 3,
                }
                valid = all(Handler.retry_requests.get(k) == n for k, n in expected.items()) and not any(
                    key[1].startswith('/persistent/api/') for key in Handler.retry_requests)
            return self.reply(200, health() if valid else {})
        if scenario == 'lifecycle':
            if not path.endswith('/v0/management/auth-files'):
                return self.reply(401, {})
            with Handler.lifecycle_lock:
                index = Handler.lifecycle_requests
                Handler.lifecycle_requests += 1
            if index == 2:
                return self.reply(401, {})
            data = health()
            used = (40, 90, 0, 30)[min(index, 3)]
            for item in data['files']:
                item['model_quotas']['model']['signals']['X-Codex-Primary-Used-Percent'] = str(used)
            return self.reply(200, data)
        if scenario == 'redirect':
            self.send_response(302)
            self.send_header('Location', '/health/v0/management/auth-files')
            self.end_headers()
            return
        if scenario == 'malformed':
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'not-json')
            return
        if path.endswith('/v0/management/auth-files'):
            if scenario in ('health', 'prefix', 'partial'):
                return self.reply(200, health())
            if scenario in ('snapshot', 'transient-post'):
                return self.reply(200, {"files": [{"provider": "codex", "name": "one.json"}, {"provider": "codex", "name": "two.json"}]})
            if scenario == 'empty':
                return self.reply(200, {"files": []})
        if (scenario == 'legacy' and path.endswith('/api/admin/manager-data') or
                scenario == 'legacy-v1' and path.endswith('/api/v1/admin/manager-data')):
            return self.reply(200, {"five_hour": {"remaining_percent": 60, "reset_at": 1792602000}})
        return self.reply(404, {})

    def do_POST(self):
        if self.headers.get("Authorization") != "Bearer integration-key":
            return self.reply(401, {})
        body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))))
        if self.path.startswith('/transient-post/') and self.attempt() < 3:
            return self.invalid_response()
        if not self.path.endswith('/quota-snapshots/query') or len(body.get('accounts', [])) != 2:
            return self.reply(400, {})
        return self.reply(200, {"items": [{"windows": [{"window_kind": "five_hour", "remaining_percent": n,
                                                       "reset_at": 1792602000}]} for n in (80, 40)]})

    def log_message(self, fmt, *args):
        print(fmt % args, flush=True)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=18765)
    args = parser.parse_args()
    print(f'CPAMP fixture listening on 127.0.0.1:{args.port}', flush=True)
    ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()
