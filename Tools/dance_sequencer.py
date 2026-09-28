"""Serves Tools/dance_sequencer.html plus a track folder, so each track's <name>.dance.json loads and saves right beside it.

    python3 Tools/dance_sequencer.py [folder]     (default: Assets/Audio/Source/Tracks)
"""
import json, os, sys, webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote

HERE = os.path.dirname(os.path.abspath(__file__))
FOLDER = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'Assets/Audio/Source/Tracks'))
AUDIO = ('.mp3', '.wav', '.aiff', '.aif', '.flac', '.ogg', '.m4a')
PORT = 8765


def analysis_path(track):
    return os.path.join(FOLDER, os.path.splitext(os.path.basename(track))[0] + '.dance.json')   # basename: no escaping the folder


class Handler(SimpleHTTPRequestHandler):
    def send(self, code, body=b'', kind='application/json'):
        self.send_response(code)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = unquote(self.path.split('?')[0])
        if path == '/':
            return self.send(200, open(os.path.join(HERE, 'dance_sequencer.html'), 'rb').read(), 'text/html; charset=utf-8')
        if path == '/tracks':
            names = sorted(f for f in os.listdir(FOLDER) if f.lower().endswith(AUDIO))
            return self.send(200, json.dumps({'folder': FOLDER, 'tracks': names}).encode())
        if path.startswith('/analysis/'):
            p = analysis_path(path[len('/analysis/'):])
            return self.send(200, open(p, 'rb').read()) if os.path.exists(p) else self.send(404)
        if path.startswith('/audio/'):
            p = os.path.join(FOLDER, os.path.basename(path[len('/audio/'):]))
            return self.send(200, open(p, 'rb').read(), 'application/octet-stream') if os.path.exists(p) else self.send(404)
        self.send(404)

    def do_PUT(self):
        path = unquote(self.path)
        if not path.startswith('/analysis/'):
            return self.send(404)
        data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        with open(analysis_path(path[len('/analysis/'):]), 'w') as f:
            json.dump(data, f, indent=1)
        self.send(204)

    def log_message(self, *args):
        pass


if __name__ == '__main__':
    print(f'Dance sequencer on http://127.0.0.1:{PORT}  (tracks: {FOLDER})  Ctrl+C to stop')
    webbrowser.open(f'http://127.0.0.1:{PORT}')
    ThreadingHTTPServer(('127.0.0.1', PORT), Handler).serve_forever()
