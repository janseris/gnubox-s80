# test web server: GET files from ./www, POST returns the sha256 of the body
import hashlib, http.server, os, sys
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "www"))
class H(http.server.SimpleHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); h = hashlib.sha256(); left = n
        while left:
            d = self.rfile.read(min(65536, left)); h.update(d); left -= len(d)
        b = (h.hexdigest() + "\n").encode()
        self.send_response(200); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
