# hang-up test: LCP up, Terminate-Request, then a fresh LCP negotiation on the same line
import os, socket, struct, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import gnubox_winshare as w
s = socket.create_connection(("127.0.0.1", 7777)); s.settimeout(5); dec = w.HdlcDecoder()
def cp(proto, code, idn, body): s.sendall(w.hdlc_encode(b"\xff\x03" + struct.pack("!H", proto) + struct.pack("!BBH", code, idn, len(body) + 4) + body))
def frames():
    out = []
    while not out:
        out = [(struct.unpack("!H", f[2:4])[0], f[4:]) for f in dec.feed(s.recv(4096))]
    return out
def lcp_up():
    cp(w.LCP, 1, 1, w.opts_build([(2, b"\0\0\0\0"), (5, b"\1\2\3\4")]))
    us = peer = False; t = time.time()
    while not (us and peer) and time.time() - t < 10:
        for p, b in frames():
            if p == w.LCP and b[0] == 1: cp(w.LCP, 2, b[1], b[4:struct.unpack("!H", b[2:4])[0]]); peer = True
            elif p == w.LCP and b[0] == 2: us = True
    return us and peer
print("LCP up:", lcp_up())
def drain(sec):
    s.settimeout(0.3); got = []; t = time.time()
    while time.time() - t < sec:
        try: got += [(struct.unpack("!H", f[2:4])[0], f[4]) for f in dec.feed(s.recv(4096))]
        except socket.timeout: pass
    s.settimeout(5); return got
drain(1)
cp(w.LCP, 5, 9, b""); print("Terminate-Ack received:", (w.LCP, 6) in drain(1))
print("quiet for 5 s after hang-up:", drain(5) == [])
s.settimeout(5)
print("LCP up again:", lcp_up())
