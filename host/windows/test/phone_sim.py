#!/usr/bin/env python3
"""Test bench: plays the Nokia 9300 against gnubox_winshare.py --tcp.
Does the phone's PPP negotiation (LCP with PFC/ACFC, VJ compression request, CCP, PAP),
then connects a Linux TUN interface, so the real Linux TCP/IP stack talks through the server.
Optional --rate limits the 'Bluetooth' link (bytes/s, both directions)."""
import argparse, fcntl, os, select, socket, struct, subprocess, sys, threading, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import gnubox_winshare as w

ap = argparse.ArgumentParser()
ap.add_argument("--server", default="127.0.0.1:7777")
ap.add_argument("--rate", type=int, default=0)
ap.add_argument("--dev", default="tun9")
a = ap.parse_args()

host, port = a.server.split(":")
s = socket.create_connection((host, int(port)))
dec = w.HdlcDecoder()
ident = [0]


class Limiter:
    def __init__(self, rate):
        self.rate, self.t = rate, time.time()

    def wait(self, n):
        if not self.rate:
            return
        self.t = max(self.t, time.time()) + n / self.rate
        d = self.t - time.time()
        if d > 0:
            time.sleep(d)


up_lim, down_lim = Limiter(a.rate), Limiter(a.rate)
send_lock = threading.Lock()


def send(proto, payload, compress=False):
    # the phone asked for PFC/ACFC: it may also send compressed frames itself
    if compress and proto < 0x100:
        frame = bytes([proto]) + payload
    else:
        frame = b"\xff\x03" + struct.pack("!H", proto) + payload
    data = w.hdlc_encode(frame)
    up_lim.wait(len(data))
    with send_lock:
        s.sendall(data)


def cp(proto, code, idn, body):
    send(proto, struct.pack("!BBH", code, idn, len(body) + 4) + body)


def recv_frames(timeout=5):
    s.settimeout(timeout)
    d = s.recv(65536)
    if not d:
        raise OSError("server closed")
    down_lim.wait(len(d))
    out = []
    for f in dec.feed(d):
        if f[:2] == b"\xff\x03":
            f = f[2:]
        out.append((struct.unpack("!H", f[:2])[0], f[2:]))
    return out


# ---- LCP
ident[0] += 1
cp(w.LCP, 1, ident[0], w.opts_build([(1, struct.pack("!H", 1500)), (2, b"\0\0\0\0"),
                                       (5, b"\x12\x34\x56\x78"), (7, b""), (8, b"")]))
lcp_us = lcp_peer = False
ipcp_opts = [(2, b"\x00\x2d\x0f\x01"), (3, b"\0\0\0\0"), (129, b"\0\0\0\0"), (131, b"\0\0\0\0")]
ipcp_done_us = ipcp_done_peer = False
ip = None
sent_extras = False
t0 = time.time()
while not (ipcp_done_us and ipcp_done_peer):
    if time.time() - t0 > 20:
        sys.exit("negotiation timed out")
    for proto, body in recv_frames():
        code, idn, length = struct.unpack("!BBH", body[:4])
        data = body[4:length]
        if proto == w.LCP:
            if code == 1:
                cp(w.LCP, 2, idn, data)
                lcp_peer = True
            elif code == 2:
                lcp_us = True
            elif code == 8:
                print("server Protocol-Reject for 0x%04x (expected for CCP)" % struct.unpack("!H", data[:2]))
            if lcp_us and lcp_peer and not sent_extras:
                sent_extras = True
                cp(w.PAP, 1, 1, b"\x03abc\x03xyz")                       # PAP login like the phone
                cp(0x80FD, 1, 1, b"\x11\x05\x00\x01\x04")                 # CCP: should be rejected
                ident[0] += 1
                cp(w.IPCP, 1, ident[0], w.opts_build(ipcp_opts))
        elif proto == w.PAP:
            print("PAP answer code", code)
        elif proto == w.IPCP:
            if code == 1:
                cp(w.IPCP, 2, idn, data)
                ipcp_done_peer = True
            elif code == 4:                    # reject: drop those options
                rej = [t for t, v in w.opts_parse(data)]
                print("IPCP rejected options", rej)
                ipcp_opts = [o for o in ipcp_opts if o[0] not in rej]
                ident[0] += 1
                cp(w.IPCP, 1, ident[0], w.opts_build(ipcp_opts))
            elif code == 3:                    # nak: take the offered values
                nak = dict(w.opts_parse(data))
                ipcp_opts = [(t, nak.get(t, v)) for t, v in ipcp_opts]
                print("IPCP offered", {t: w.ip_str(v) for t, v in nak.items()})
                ident[0] += 1
                cp(w.IPCP, 1, ident[0], w.opts_build(ipcp_opts))
            elif code == 2:
                ip = dict(w.opts_parse(data))[3]
                ipcp_done_us = True
print("PPP up, phone IP", w.ip_str(ip))

# ---- TUN
TUNSETIFF, IFF_TUN, IFF_NO_PI = 0x400454ca, 0x0001, 0x1000
tun = os.open("/dev/net/tun", os.O_RDWR)
fcntl.ioctl(tun, TUNSETIFF, struct.pack("16sH", a.dev.encode(), IFF_TUN | IFF_NO_PI))
for cmd in (["ip", "addr", "add", w.ip_str(ip) + "/32", "dev", a.dev], ["ip", "link", "set", a.dev, "mtu", "1500", "up"],
            ["ip", "route", "add", "10.64.64.1/32", "dev", a.dev], ["ip", "route", "add", "10.99.0.0/24", "dev", a.dev], ["ip", "route", "add", "192.168.137.0/24", "dev", a.dev]):
    subprocess.run(cmd, check=True)
print("TUN %s ready" % a.dev, flush=True)


def tun_to_ppp():
    n = 0
    while True:
        pkt = os.read(tun, 65536)
        n += 1
        send(0x21, pkt, compress=(n % 2 == 0))      # every other frame protocol-compressed


threading.Thread(target=tun_to_ppp, daemon=True).start()
while True:
    try:
        frames = recv_frames(timeout=None)
    except (OSError, ValueError):
        break
    for proto, body in frames:
        if proto == 0x21:
            os.write(tun, body)
        elif proto == w.LCP and body[0] == 9:
            pass
