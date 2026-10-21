"""Local CPAMP fixtures. No real credentials or external services are used."""
import json
import threading
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
            if scenario == 'snapshot':
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
