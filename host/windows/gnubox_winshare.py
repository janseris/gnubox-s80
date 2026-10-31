#!/usr/bin/env python3
"""
gnubox_winshare.py - share this Windows PC's internet with a Nokia 9300 over Bluetooth.

The 9300 runs GnuBox in "Other device: PC" mode: it opens the PC's Bluetooth serial port
(Serial Port Profile) and starts PPP at once. This program answers that PPP and carries the
phone's traffic to the internet through ordinary Windows sockets (a small user-space NAT, like
BlueDUN++ does on Android). No Windows networking settings, ICS, admin rights or drivers.

    pip install pyserial
    python gnubox_winshare.py               (direct Bluetooth: no COM port needed)
    python gnubox_winshare.py COM5          (or an INCOMING Bluetooth COM port, see README)

    python gnubox_winshare.py --list   (shows the COM ports; use the INCOMING Bluetooth one)

    options: -v             log every connection
             --tcp 127.0.0.1:7777   listen on TCP instead of a COM port (testing)

Phone side gets 10.64.64.2, gateway/DNS 10.64.64.1. TCP, UDP, DNS and ping work; no
incoming connections to the phone. The phone reaches this PC itself (e.g. localserver on port
8000) as 10.64.64.1 or 192.168.137.1 (the USB-link address, so the same app settings work). Python 3.7+. GPL v2 or later, like GnuBox.
"""
import argparse, errno, queue, random, select, socket, struct, threading, time

PHONE_IP = bytes([10, 64, 64, 2])
GW_IP = bytes([10, 64, 64, 1])
MSS = 1360
RECV_WINDOW = 65535
SEND_WINDOW_CAP = 32768        # max unacked bytes in flight to the phone
VERBOSE = False


def _print_sink(line):
    print(line, flush=True)


LOG_SINK = _print_sink        # the GUI replaces this


def log(*a):
    LOG_SINK(time.strftime("%H:%M:%S") + " " + " ".join(str(x) for x in a))


def vlog(*a):
    if VERBOSE:
        log(*a)


def ip_str(b):
    return ".".join(str(x) for x in b)


# ------------------------------------------------------------------ checksums, HDLC
def csum(data):
    if len(data) & 1:
        data += b"\0"
    s = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def _fcs_table():
    t = []
    for b in range(256):
        v = b
        for _ in range(8):
            v = (v >> 1) ^ 0x8408 if v & 1 else v >> 1
        t.append(v)
    return t


FCS_TABLE = _fcs_table()


def fcs16(data):
    f = 0xFFFF
    for b in data:
        f = (f >> 8) ^ FCS_TABLE[(f ^ b) & 0xFF]
    return f


def _escape_table(accm):
    return [b in (0x7E, 0x7D) or (b < 0x20 and (accm >> b) & 1) for b in range(256)]


ESC_ALL = _escape_table(0xFFFFFFFF)


def hdlc_encode(payload, esc=ESC_ALL):
    """esc: which bytes to escape; control characters only if the peer's ACCM asks for it."""
    f = (fcs16(payload) ^ 0xFFFF)
    raw = payload + bytes([f & 0xFF, f >> 8])
    out = bytearray(b"\x7e")
    for b in raw:
        if esc[b]:
            out += bytes([0x7D, b ^ 0x20])
        else:
            out.append(b)
    out.append(0x7E)
    return bytes(out)


class HdlcDecoder:
    def __init__(self):
        self.buf = bytearray()
        self.esc = False

    def feed(self, data):
        frames = []
        for b in data:
            if b == 0x7E:
                if len(self.buf) >= 4:
                    if fcs16(bytes(self.buf)) == 0xF0B8:
                        frames.append(bytes(self.buf[:-2]))
                    else:
                        vlog("PPP: bad FCS, frame dropped")
                self.buf = bytearray()
                self.esc = False
            elif b == 0x7D:
                self.esc = True
            else:
                if self.esc:
                    b ^= 0x20
                    self.esc = False
                self.buf.append(b)
                if len(self.buf) > 4000:
                    self.buf = bytearray()
        return frames


# ------------------------------------------------------------------ transports
class SerialTransport:
    def __init__(self, port):
        import serial      # pip install pyserial
        self.ser = serial.Serial(port, 115200, timeout=0.2, write_timeout=10)
        self.name = port

    def read(self):
        n = self.ser.in_waiting
        return self.ser.read(n if n else 1)

    def write(self, data):
        self.ser.write(data)

    def close(self):
        self.ser.close()


# --- direct Bluetooth (Windows): the program itself is the Serial Port service, no COM port.
# Documented Win32 calls (ws2_32): an RFCOMM listening socket (AF_BTH) plus an SDP record
# for the Serial Port service class (WSASetService), the same thing C#/.NET libraries do
# with P/Invoke. GnuBox's "Other device: PC" mode looks up exactly this service.
AF_BTH, BTHPROTO_RFCOMM, NS_BTH = 32, 3, 16
BT_PORT_ANY = 0xFFFFFFFF
SPP_UUID = "00001101-0000-1000-8000-00805F9B34FB"


def _bt_structs():
    import ctypes
    from ctypes import c_uint16, c_uint32, c_uint64, c_int, c_void_p, c_wchar_p, POINTER, Structure

    class GUID(Structure):
        _fields_ = [("Data1", c_uint32), ("Data2", c_uint16), ("Data3", c_uint16), ("Data4", ctypes.c_ubyte * 8)]

    class SOCKADDR_BTH(Structure):        # ws2bth.h: packed (pshpack1), 30 bytes
        _pack_ = 1
        _fields_ = [("addressFamily", c_uint16), ("btAddr", c_uint64), ("serviceClassId", GUID), ("port", c_uint32)]

    class SOCKET_ADDRESS(Structure):
        _fields_ = [("lpSockaddr", c_void_p), ("iSockaddrLength", c_int)]

    class CSADDR_INFO(Structure):
        _fields_ = [("LocalAddr", SOCKET_ADDRESS), ("RemoteAddr", SOCKET_ADDRESS), ("iSocketType", c_int), ("iProtocol", c_int)]

    class WSAQUERYSETW(Structure):
        _fields_ = [("dwSize", c_uint32), ("lpszServiceInstanceName", c_wchar_p), ("lpServiceClassId", POINTER(GUID)),
                    ("lpVersion", c_void_p), ("lpszComment", c_wchar_p), ("dwNameSpace", c_uint32),
                    ("lpNSProviderId", c_void_p), ("lpszContext", c_wchar_p), ("dwNumberOfProtocols", c_uint32),
                    ("lpafpProtocols", c_void_p), ("lpszQueryString", c_wchar_p), ("dwNumberOfCsAddrs", c_uint32),
                    ("lpcsaBuffer", POINTER(CSADDR_INFO)), ("dwOutputFlags", c_uint32), ("lpBlob", c_void_p)]

    def guid(text):
        import uuid
        u = uuid.UUID(text)
        g = GUID()
        g.Data1, g.Data2, g.Data3 = u.time_low, u.time_mid, u.time_hi_version
        for i, b in enumerate(u.bytes[8:]):
            g.Data4[i] = b
        return g
    return GUID, SOCKADDR_BTH, CSADDR_INFO, WSAQUERYSETW, guid


def bt_addr_str(a):
    return ":".join("%02X" % ((a >> (8 * i)) & 0xFF) for i in range(5, -1, -1))


class _RawSocket:
    """recv/sendall/close on a raw Winsock handle through ws2_32 (fallback)."""
    def __init__(self, ws2, h):
        import ctypes
        self.ws2, self.h, self.ct = ws2, h, ctypes
        ws2.recv.argtypes = [ctypes.c_size_t, ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
        ws2.send.argtypes = [ctypes.c_size_t, ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
        self.buf = ctypes.create_string_buffer(65536)

    def fileno(self):
        return self.h

    def recv(self, n):
        r, _, _ = select.select([self], [], [], 0.2)
        if not r:
            raise socket.timeout()
        got = self.ws2.recv(self.h, self.buf, min(n, 65536), 0)
        if got < 0:
            raise OSError("Bluetooth recv: error %d" % self.ct.get_last_error())
        return self.buf.raw[:got]

    def sendall(self, data):
        while data:
            n = self.ws2.send(self.h, data, len(data), 0)
            if n <= 0:
                raise OSError("Bluetooth send: error %d" % self.ct.get_last_error())
            data = data[n:]

    def close(self):
        self.ws2.closesocket(self.h)


class BluetoothTransport:
    """Windows only: listens for the phone on an RFCOMM channel advertised as a Serial Port service."""
    def __init__(self, stop=None, name="GnuBox Share"):
        import ctypes
        GUID, SOCKADDR_BTH, CSADDR_INFO, WSAQUERYSETW, guid = _bt_structs()
        self.ctypes = ctypes
        self.SOCKADDR_BTH = SOCKADDR_BTH
        ws2 = ctypes.WinDLL("ws2_32", use_last_error=True)
        self.ws2 = ws2
        ws2.socket.restype = ctypes.c_size_t
        ws2.socket.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        ws2.bind.argtypes = [ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int]
        ws2.listen.argtypes = [ctypes.c_size_t, ctypes.c_int]
        ws2.getsockname.argtypes = [ctypes.c_size_t, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
        ws2.accept.restype = ctypes.c_size_t
        ws2.accept.argtypes = [ctypes.c_size_t, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
        ws2.closesocket.argtypes = [ctypes.c_size_t]
        ws2.WSASetServiceW.argtypes = [ctypes.POINTER(WSAQUERYSETW), ctypes.c_int, ctypes.c_uint32]
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM).close()     # makes sure Winsock is started
        invalid = ctypes.c_size_t(-1).value
        self.lsock = ws2.socket(AF_BTH, socket.SOCK_STREAM, BTHPROTO_RFCOMM)
        if self.lsock == invalid:
            raise OSError("Bluetooth socket: error %d (no Bluetooth adapter, or it is off?)" % ctypes.get_last_error())
        self.c = None
        self.registered = False
        try:
            sa = SOCKADDR_BTH()
            sa.addressFamily = AF_BTH
            sa.port = BT_PORT_ANY
            if ws2.bind(self.lsock, ctypes.byref(sa), ctypes.sizeof(sa)) != 0:
                raise OSError("Bluetooth bind: error %d" % ctypes.get_last_error())
            if ws2.listen(self.lsock, 1) != 0:
                raise OSError("Bluetooth listen: error %d" % ctypes.get_last_error())
            ln = ctypes.c_int(ctypes.sizeof(sa))
            ws2.getsockname(self.lsock, ctypes.byref(sa), ctypes.byref(ln))
            self.channel = sa.port
            # SDP record: Serial Port service on this channel
            self.sa = sa
            self.uuid = guid(SPP_UUID)
            self.csa = CSADDR_INFO()
            self.csa.LocalAddr.lpSockaddr = ctypes.cast(ctypes.byref(self.sa), ctypes.c_void_p)
            self.csa.LocalAddr.iSockaddrLength = ctypes.sizeof(self.sa)
            self.csa.iSocketType = socket.SOCK_STREAM
            self.csa.iProtocol = BTHPROTO_RFCOMM
            self.qs = WSAQUERYSETW()
            self.qs.dwSize = ctypes.sizeof(WSAQUERYSETW)
            self.qs.lpszServiceInstanceName = name
            self.qs.lpszComment = "Internet for the Nokia 9300 (GnuBox)"
            self.qs.lpServiceClassId = ctypes.pointer(self.uuid)
            self.qs.dwNameSpace = NS_BTH
            self.qs.dwNumberOfCsAddrs = 1
            self.qs.lpcsaBuffer = ctypes.pointer(self.csa)
            if ws2.WSASetServiceW(ctypes.byref(self.qs), 0, 0) != 0:      # RNRSERVICE_REGISTER
                raise OSError("Bluetooth service registration: error %d" % ctypes.get_last_error())
            self.registered = True
            self.name = "Bluetooth channel %d" % self.channel
            log("Bluetooth: Serial Port service '%s' on RFCOMM channel %d - press Connect in GnuBox" % (name, self.channel))

            class _H:                       # select() on the raw listening socket
                def __init__(self, h):
                    self.h = h

                def fileno(self):
                    return self.h
            lh = _H(self.lsock)
            while True:
                r, _, _ = select.select([lh], [], [], 0.5)
                if r:
                    break
                if stop is not None and stop.is_set():
                    raise KeyboardInterrupt
            peer = SOCKADDR_BTH()
            pl = ctypes.c_int(ctypes.sizeof(peer))
            h = ws2.accept(self.lsock, ctypes.byref(peer), ctypes.byref(pl))
            if h == invalid:
                raise OSError("Bluetooth accept: error %d" % ctypes.get_last_error())
            try:
                self.c = socket.socket(AF_BTH, socket.SOCK_STREAM, BTHPROTO_RFCOMM, fileno=h)
                self.c.settimeout(0.2)
            except OSError:
                self.c = _RawSocket(ws2, h)       # Python can't wrap it: plain Win32 recv/send
            log("Bluetooth: phone %s connected" % bt_addr_str(peer.btAddr))
        except BaseException:
            self.close()
            raise

    def read(self):
        try:
            d = self.c.recv(65536)
        except socket.timeout:
            return b""
        except OSError:
            raise EOFError
        if not d:
            raise EOFError
        return d

    def write(self, data):
        self.c.sendall(data)

    def close(self):
        if self.c is not None:
            try:
                self.c.close()
            except OSError:
                pass
            self.c = None
        if self.registered:
            try:
                self.ws2.WSASetServiceW(self.ctypes.byref(self.qs), 2, 0)      # RNRSERVICE_DELETE
            except Exception:
                pass
            self.registered = False
        if self.lsock is not None:
            try:
                self.ws2.closesocket(self.lsock)
            except Exception:
                pass
            self.lsock = None


class TcpTransport:
    """For testing: one client at a time on a TCP port acts as the serial line."""
    def __init__(self, addr, stop=None):
        host, port = addr.rsplit(":", 1)
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind((host, int(port)))
        self.srv.listen(1)
        self.srv.settimeout(0.5)
        self.name = "tcp " + addr
        log("waiting for a test client on", addr)
        while True:
            try:
                self.c, _ = self.srv.accept()
                break
            except socket.timeout:
                if stop is not None and stop.is_set():
                    self.srv.close()
                    raise KeyboardInterrupt
        self.c.settimeout(0.2)

    def read(self):
        try:
            d = self.c.recv(65536)
        except socket.timeout:
            return b""
        except ConnectionResetError:
            raise EOFError
        if not d:
            raise EOFError
        return d

    def write(self, data):
        self.c.sendall(data)

    def close(self):
        self.c.close()
        self.srv.close()


# ------------------------------------------------------------------ PPP
LCP, PAP, CHAP, IPCP, IPV4 = 0xC021, 0xC023, 0xC223, 0x8021, 0x0021


def opts_parse(data):
    out = []
    i = 0
    while i + 2 <= len(data):
        t, l = data[i], data[i + 1]
        if l < 2 or i + l > len(data):
            break
        out.append((t, data[i + 2:i + l]))
        i += l
    return out


def opts_build(opts):
    return b"".join(bytes([t, len(v) + 2]) + v for t, v in opts)


class Ppp:
    def __init__(self, link):
        self.link = link            # object with send_frame(bytes) and on_ip(bytes)
        self.ident = 1
        self.lcp_open = False
        self.lcp_acked_us = False
        self.lcp_acked_peer = False
        self.ipcp_acked_us = False
        self.ipcp_acked_peer = False
        self.ip_up = False
        self.magic = random.getrandbits(32) or 1
        self.last_req = 0
        self.esc = ESC_ALL              # escaping for non-LCP frames, from the peer's ACCM
        self.passive = False            # after the phone hung up: wait until it talks again

    # -- sending
    def send(self, proto, payload):
        frame = b"\xff\x03" + struct.pack("!H", proto) + payload
        # LCP always uses the default (escape every control char); the rest the negotiated ACCM
        self.link.send_frame(frame, ESC_ALL if proto == LCP else self.esc)

    def send_cp(self, proto, code, ident, data):
        self.send(proto, struct.pack("!BBH", code, ident, len(data) + 4) + data)

    def lcp_request(self):
        self.ident = (self.ident + 1) & 0xFF
        opts = [(2, b"\0\0\0\0"), (5, struct.pack("!I", self.magic))]   # ACCM 0, magic
        self.send_cp(LCP, 1, self.ident, opts_build(opts))

    def ipcp_request(self):
        self.ident = (self.ident + 1) & 0xFF
        self.send_cp(IPCP, 1, self.ident, opts_build([(3, GW_IP)]))

    def tick(self):
        if self.passive:
            return
        now = time.time()
        if now - self.last_req < 3:
            return
        if not (self.lcp_acked_us and self.lcp_acked_peer):
            self.last_req = now
            self.lcp_request()
        elif not (self.ipcp_acked_us and self.ipcp_acked_peer):
            self.last_req = now
            self.ipcp_request()

    def send_ip(self, packet):
        if self.ip_up:
            self.send(IPV4, packet)

    # -- receiving
    def on_frame(self, f):
        if f[:2] == b"\xff\x03":            # address/control may be compressed away
            f = f[2:]
        if not f:
            return
        if f[0] & 1:                         # protocol field compressed to one byte
            proto, data = f[0], f[1:]
        else:
            if len(f) < 2:
                return
            proto, data = struct.unpack("!H", f[:2])[0], f[2:]
        if proto == IPV4:
            if self.ip_up:
                self.link.on_ip(data)
            return
        if proto == LCP:
            self.on_lcp(data)
        elif proto == IPCP:
            self.on_ipcp(data)
        elif proto == PAP:
            self.on_pap(data)
        else:
            # CCP, IPv6CP, ...: Protocol-Reject
            if self.lcp_open:
                vlog("PPP: rejecting protocol 0x%04x" % proto)
                self.ident = (self.ident + 1) & 0xFF
                self.send_cp(LCP, 8, self.ident, struct.pack("!H", proto) + data[:1000])

    def cp_header(self, data):
        if len(data) < 4:
            return None
        code, ident, length = struct.unpack("!BBH", data[:4])
        return code, ident, data[4:length]

    def on_lcp(self, data):
        h = self.cp_header(data)
        if not h:
            return
        code, ident, body = h
        if code == 1:                                       # Configure-Request
            self.passive = False
            rej = []
            for t, v in opts_parse(body):
                if t in (1, 2, 5, 7, 8):                     # MRU, ACCM, magic, PFC, ACFC: fine
                    continue
                rej.append((t, v))                           # auth (3) and the rest: no
            if rej:
                self.send_cp(LCP, 4, ident, opts_build(rej))
                return
            self.send_cp(LCP, 2, ident, body)                # Configure-Ack
            accm = 0xFFFFFFFF
            for t, v in opts_parse(body):
                if t == 2 and len(v) == 4:
                    accm = struct.unpack("!I", v)[0]
            self.esc = _escape_table(accm)
            if self.lcp_open and self.lcp_acked_peer:
                # renegotiation: the phone restarted the link
                log("PPP: the phone restarted the link")
                self.ip_up = False
                self.ipcp_acked_us = self.ipcp_acked_peer = False
                self.lcp_acked_us = False
            self.lcp_acked_peer = True
            if not self.lcp_acked_us:
                self.lcp_request()
                self.last_req = time.time()
            self.check_lcp()
        elif code == 2:                                     # Configure-Ack (for us)
            self.lcp_acked_us = True
            self.check_lcp()
        elif code in (3, 4):                                # Nak / Reject of our options
            self.lcp_request()
        elif code == 5:                                     # Terminate-Request
            self.send_cp(LCP, 6, ident, b"")
            log("PPP: the phone closed the link")
            self.reset()
        elif code == 9:                                     # Echo-Request
            self.send_cp(LCP, 10, ident, struct.pack("!I", self.magic) + body[4:])
        elif code == 8:
            vlog("PPP: the phone rejected protocol", body[:2].hex())

    def check_lcp(self):
        if self.lcp_acked_us and self.lcp_acked_peer and not self.lcp_open:
            self.lcp_open = True
            log("PPP: link up (LCP)")
            self.ipcp_request()
            self.last_req = time.time()

    def on_pap(self, data):
        h = self.cp_header(data)
        if h and h[0] == 1:                                 # Authenticate-Request: accept anyone
            msg = b"OK"
            self.send_cp(PAP, 2, h[1], bytes([len(msg)]) + msg)

    def on_ipcp(self, data):
        h = self.cp_header(data)
        if not h:
            return
        code, ident, body = h
        if code == 1:
            rej, nak, ok = [], [], []
            for t, v in opts_parse(body):
                if t == 3:                                  # IP address
                    (ok if v == PHONE_IP else nak).append((t, PHONE_IP if v != PHONE_IP else v))
                elif t in (129, 131):                       # primary / secondary DNS
                    (ok if v == GW_IP else nak).append((t, GW_IP if v != GW_IP else v))
                else:                                       # VJ compression etc.
                    rej.append((t, v))
            if rej:
                self.send_cp(IPCP, 4, ident, opts_build(rej))
            elif nak:
                self.send_cp(IPCP, 3, ident, opts_build(nak))
            else:
                self.send_cp(IPCP, 2, ident, body)
                self.ipcp_acked_peer = True
                self.check_ipcp()
        elif code == 2:
            self.ipcp_acked_us = True
            self.check_ipcp()
        elif code in (3, 4):
            self.ipcp_request()
        elif code == 5:
            self.send_cp(IPCP, 6, ident, b"")
            self.ip_up = False

    def check_ipcp(self):
        if self.ipcp_acked_us and self.ipcp_acked_peer and not self.ip_up:
            self.ip_up = True
            log("PPP: IP up - phone %s, gateway and DNS %s" % (ip_str(PHONE_IP), ip_str(GW_IP)))

    def reset(self):
        self.lcp_open = self.lcp_acked_us = self.lcp_acked_peer = False
        self.ipcp_acked_us = self.ipcp_acked_peer = self.ip_up = False
        self.esc = ESC_ALL
        self.passive = True
        self.link.on_reset()


# ------------------------------------------------------------------ IP helpers
def build_ipv4(src, dst, proto, payload, ident=[0]):
    ident[0] = (ident[0] + 1) & 0xFFFF
    hdr = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(payload), ident[0], 0x4000, 64, proto, 0, src, dst)
    hdr = hdr[:10] + struct.pack("!H", csum(hdr)) + hdr[12:]
    return hdr + payload


def l4_checksum(src, dst, proto, seg):
    pseudo = src + dst + struct.pack("!BBH", 0, proto, len(seg))
    return csum(pseudo + seg)


def build_udp(src, sport, dst, dport, data):
    seg = struct.pack("!HHHH", sport, dport, 8 + len(data), 0) + data
    c = l4_checksum(src, dst, 17, seg) or 0xFFFF
    seg = seg[:6] + struct.pack("!H", c) + seg[8:]
    return build_ipv4(src, dst, 17, seg)


F_FIN, F_SYN, F_RST, F_PSH, F_ACK = 0x01, 0x02, 0x04, 0x08, 0x10


def build_tcp(src, sport, dst, dport, seq, ack, flags, window, data=b"", mss=None):
    opts = struct.pack("!BBH", 2, 4, mss) if mss else b""
    off = (20 + len(opts)) // 4
    seg = struct.pack("!HHIIBBHHH", sport, dport, seq & 0xFFFFFFFF, ack & 0xFFFFFFFF,
                      off << 4, flags, min(window, 65535), 0, 0) + opts + data
    c = l4_checksum(src, dst, 6, seg)
    seg = seg[:16] + struct.pack("!H", c) + seg[18:]
    return build_ipv4(src, dst, 6, seg)


# ------------------------------------------------------------------ DNS
def dns_answer(query, lookup):
    """Answer with Windows' own resolver: A records; other types get an empty answer."""
    if len(query) < 12:
        return None
    qid, flags, qd = struct.unpack("!HHH", query[:6])
    if qd != 1:
        return None
    i, labels = 12, []
    while i < len(query) and query[i]:
        l = query[i]
        labels.append(query[i + 1:i + 1 + l].decode("ascii", "replace"))
        i += 1 + l
    i += 1
    if i + 4 > len(query):
        return None
    qtype, qclass = struct.unpack("!HH", query[i:i + 4])
    question = query[12:i + 4]
    name = ".".join(labels)
    addrs = lookup(name)
    rcode = 0
    if addrs is None:
        rcode = 3                            # NXDOMAIN
        addrs = []
    elif qtype not in (1, 255):
        addrs = []                           # the name exists, but we only give IPv4 addresses
    answers = b"".join(b"\xc0\x0c" + struct.pack("!HHIH", 1, 1, 60, 4) + socket.inet_aton(a) for a in addrs[:8])
    hdr = struct.pack("!HHHHHH", qid, 0x8180 | rcode | (flags & 0x0100), 1, len(addrs[:8]), 0, 0)
    return hdr + question + answers


class Resolver:
    def __init__(self):
        self.q = queue.Queue()
        self.done = queue.Queue()
        self.cache = {}
        for _ in range(4):
            threading.Thread(target=self.worker, daemon=True).start()

    def lookup(self, name):
        c = self.cache.get(name)
        if c and c[0] > time.time():
            return c[1]
        try:
            infos = socket.getaddrinfo(name, None, socket.AF_INET, socket.SOCK_STREAM)
            addrs = []
            for inf in infos:
                a = inf[4][0]
                if a not in addrs:
                    addrs.append(a)
        except socket.gaierror:
            addrs = None
        except Exception:
            addrs = None
        self.cache[name] = (time.time() + 60, addrs)
        return addrs

    def worker(self):
        while True:
            key, query = self.q.get()
            ans = dns_answer(query, self.lookup)
            self.done.put((key, ans))


# ------------------------------------------------------------------ TCP NAT
class TcpConn:
    def __init__(self, nat, key, isn_peer, peer_win, peer_mss):
        self.nat = nat
        self.key = key                      # (phone port, dst ip bytes, dst port)
        self.dst = key[1]
        self.rcv_nxt = (isn_peer + 1) & 0xFFFFFFFF
        self.iss = random.getrandbits(32)
        self.snd_una = self.iss
        self.snd_nxt = self.iss              # SYN not sent yet
        self.peer_win = peer_win
        self.mss = min(MSS, peer_mss or 536)
        self.to_server = bytearray()        # phone -> server, not yet written
        self.unacked = bytearray()           # server -> phone, sent but not acked (from snd_una)
        self.pending = bytearray()           # server -> phone, read from the socket, not yet sent
        self.state = "connecting"
        self.peer_fin = False
        self.server_eof = False
        self.fin_sent = False
        self.rto = 3.0
        self.rtx_at = None
        self.last_activity = time.time()
        self.bytes_in = self.bytes_out = 0
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setblocking(False)
        dst = nat.rewrite.get(ip_str(self.dst), ip_str(self.dst))
        try:
            self.sock.connect((dst, key[2]))
        except BlockingIOError:
            pass                                    # normal for a non-blocking connect
        except OSError as e:
            if e.errno not in (errno.EINPROGRESS, errno.EWOULDBLOCK, 10035):
                self.sock.close()
                raise
        vlog("TCP open %s:%d" % (dst, key[2]))

    def send_seg(self, flags, data=b"", seq=None, mss=None):
        if seq is None:
            seq = self.snd_nxt
        win = max(0, RECV_WINDOW - len(self.to_server))
        pkt = build_tcp(self.dst, self.key[2], PHONE_IP, self.key[0], seq, self.rcv_nxt, flags, win, data, mss)
        self.nat.ppp.send_ip(pkt)

    def connected(self):
        self.state = "synrcvd"
        self.send_seg(F_SYN | F_ACK, mss=MSS)
        self.snd_nxt = (self.iss + 1) & 0xFFFFFFFF
        self.rtx_at = time.time() + self.rto

    def failed(self):
        self.send_seg(F_RST | F_ACK, seq=self.iss)
        self.close("connection refused or unreachable")

    def on_segment(self, seq, ack, flags, win, data):
        self.last_activity = time.time()
        self.peer_win = win
        if flags & F_RST:
            self.close("reset by the phone")
            return
        if flags & F_SYN:                    # retransmitted SYN
            if self.state == "synrcvd":
                self.send_seg(F_SYN | F_ACK, seq=self.iss, mss=MSS)
            return
        if flags & F_ACK:
            self.on_ack(ack)
        if self.state == "connecting":
            return
        if data:
            if seq == self.rcv_nxt:
                if len(self.to_server) + len(data) <= RECV_WINDOW:
                    self.to_server += data
                    self.rcv_nxt = (self.rcv_nxt + len(data)) & 0xFFFFFFFF
                    self.bytes_out += len(data)
                self.send_seg(F_ACK)
            else:
                self.send_seg(F_ACK)         # duplicate or out of order: ack what we have
        if flags & F_FIN and not self.peer_fin:
            if (seq + len(data)) & 0xFFFFFFFF == self.rcv_nxt:
                self.peer_fin = True
                self.rcv_nxt = (self.rcv_nxt + 1) & 0xFFFFFFFF
                self.send_seg(F_ACK)
        self.flush_to_server()
        self.pump()

    def on_ack(self, ack):
        acked = (ack - self.snd_una) & 0xFFFFFFFF
        inflight = (self.snd_nxt - self.snd_una) & 0xFFFFFFFF
        if acked == 0 or acked > inflight:
            return
        if self.state == "synrcvd":
            self.state = "open"
            self.snd_una = (self.snd_una + 1) & 0xFFFFFFFF
            acked -= 1
        data_acked = min(acked, len(self.unacked))
        del self.unacked[:data_acked]
        self.snd_una = (self.snd_una + acked) & 0xFFFFFFFF
        self.rto = 3.0
        self.rtx_at = time.time() + self.rto if (self.snd_nxt != self.snd_una) else None
        if self.fin_sent and self.snd_una == self.snd_nxt and self.peer_fin:
            self.close(None)

    def flush_to_server(self):
        if self.to_server and self.state == "open":
            try:
                n = self.sock.send(self.to_server)
                del self.to_server[:n]
            except (BlockingIOError, InterruptedError):
                pass
            except OSError:
                self.send_seg(F_RST | F_ACK)
                self.close("server connection lost")
                return
        if self.peer_fin and not self.to_server and self.state == "open":
            try:
                self.sock.shutdown(socket.SHUT_WR)
            except OSError:
                pass

    def on_readable(self):
        if len(self.pending) > 65536:
            return
        try:
            d = self.sock.recv(32768)
        except (BlockingIOError, InterruptedError):
            return
        except OSError:
            d = b""
        if d:
            self.pending += d
            self.bytes_in += len(d)
            self.last_activity = time.time()
        else:
            self.server_eof = True
        self.pump()

    def pump(self):
        if self.state != "open":
            return
        while self.pending:
            inflight = (self.snd_nxt - self.snd_una) & 0xFFFFFFFF
            room = min(self.peer_win, SEND_WINDOW_CAP) - inflight
            if room <= 0:
                break
            n = min(self.mss, room, len(self.pending))
            chunk = bytes(self.pending[:n])
            del self.pending[:n]
            self.send_seg(F_ACK | F_PSH, chunk)
            self.unacked += chunk
            self.snd_nxt = (self.snd_nxt + n) & 0xFFFFFFFF
            if self.rtx_at is None:
                self.rtx_at = time.time() + self.rto
        if self.server_eof and not self.pending and not self.fin_sent:
            self.send_seg(F_FIN | F_ACK)
            self.fin_sent = True
            self.snd_nxt = (self.snd_nxt + 1) & 0xFFFFFFFF
            if self.rtx_at is None:
                self.rtx_at = time.time() + self.rto

    def tick(self, now):
        if self.rtx_at and now >= self.rtx_at:
            # resend the oldest unacked segment (or SYN-ACK / FIN)
            if self.state == "synrcvd":
                self.send_seg(F_SYN | F_ACK, seq=self.iss, mss=MSS)
            elif self.unacked:
                self.send_seg(F_ACK | F_PSH, bytes(self.unacked[:self.mss]), seq=self.snd_una)
            elif self.fin_sent:
                self.send_seg(F_FIN | F_ACK, seq=(self.snd_nxt - 1) & 0xFFFFFFFF)
            self.rto = min(self.rto * 2, 30)
            self.rtx_at = now + self.rto
        if now - self.last_activity > 600:
            self.send_seg(F_RST | F_ACK)
            self.close("idle 10 min")
        if self.state == "connecting" and now - self.last_activity > 30:
            self.failed()

    def close(self, why):
        if self.state == "closed":
            return
        self.state = "closed"
        try:
            self.sock.close()
        except OSError:
            pass
        self.nat.tcp.pop(self.key, None)
        vlog("TCP closed %s:%d  down %d B, up %d B%s" % (ip_str(self.dst), self.key[2], self.bytes_in,
             self.bytes_out, ("  (" + why + ")") if why else ""))
        self.nat.total_in += self.bytes_in
        self.nat.total_out += self.bytes_out


class Nat:
    def __init__(self, rewrite):
        self.tcp = {}
        self.udp = {}                      # (phone port, dst, dport) -> [sock, last]
        self.rewrite = rewrite
        self.resolver = Resolver()
        self.ppp = None
        self.total_in = self.total_out = 0

    def on_reset(self):
        for c in list(self.tcp.values()):
            c.close("link closed")
        for k, (s, _) in list(self.udp.items()):
            s.close()
        self.udp.clear()

    def on_ip(self, pkt):
        if len(pkt) < 20 or pkt[0] >> 4 != 4:
            return
        ihl = (pkt[0] & 15) * 4
        total = struct.unpack("!H", pkt[2:4])[0]
        proto = pkt[9]
        src, dst = pkt[12:16], pkt[16:20]
        body = pkt[ihl:total]
        if proto == 6:
            self.on_tcp(dst, body)
        elif proto == 17:
            self.on_udp(dst, body)
        elif proto == 1:
            self.on_icmp(src, dst, body)

    def on_icmp(self, src, dst, body):
        if len(body) >= 8 and body[0] == 8:          # echo request: answer it ourselves
            rep = b"\x00\x00\x00\x00" + body[4:]
            rep = rep[:2] + struct.pack("!H", csum(rep)) + rep[4:]
            self.ppp.send_ip(build_ipv4(dst, PHONE_IP, 1, rep))

    def on_udp(self, dst, body):
        if len(body) < 8:
            return
        sport, dport = struct.unpack("!HH", body[:4])
        data = body[8:]
        if dport == 53:
            # every DNS query is answered with Windows' own resolver (in a thread)
            self.resolver.q.put(((sport, dst, dport, data[:2]), data))
            return
        dst_real = (self.rewrite.get(ip_str(dst), ip_str(dst)), dport)
        k = (sport, dst, dport)
        ent = self.udp.get(k)
        if not ent:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setblocking(False)
            ent = self.udp[k] = [s, time.time()]
        ent[1] = time.time()
        try:
            ent[0].sendto(data, dst_real)
        except OSError:
            pass

    def dns_done(self):
        while True:
            try:
                key, ans = self.resolver.done.get_nowait()
            except queue.Empty:
                return
            if ans:
                sport, dst, dport, _ = key
                self.ppp.send_ip(build_udp(dst, 53, PHONE_IP, sport, ans))

    def udp_readable(self, k, s):
        try:
            data, _ = s.recvfrom(65536)
        except OSError:
            return
        sport, dst, dport = k
        self.udp[k][1] = time.time()
        self.ppp.send_ip(build_udp(dst, dport, PHONE_IP, sport, data))

    def on_tcp(self, dst, seg):
        if len(seg) < 20:
            return
        sport, dport, seq, ack, off, flags, win = struct.unpack("!HHIIBBH", seg[:16])
        hl = (off >> 4) * 4
        data = seg[hl:]
        key = (sport, dst, dport)
        c = self.tcp.get(key)
        if c is None:
            if flags & F_SYN and not flags & F_ACK:
                mss = 536
                o = seg[20:hl]
                i = 0
                while i < len(o):
                    if o[i] == 0:
                        break
                    if o[i] == 1:
                        i += 1
                        continue
                    if i + 1 >= len(o) or o[i + 1] < 2:
                        break
                    if o[i] == 2 and o[i + 1] == 4:
                        mss = struct.unpack("!H", o[i + 2:i + 4])[0]
                    i += o[i + 1]
                try:
                    self.tcp[key] = TcpConn(self, key, seq, win, mss)
                except OSError as e:
                    vlog("TCP cannot open %s:%d: %s" % (ip_str(dst), dport, e))
                    self.ppp.send_ip(build_tcp(dst, dport, PHONE_IP, sport, 0, seq + 1, F_RST | F_ACK, 0))
            elif not flags & F_RST:
                # unknown connection: reset it
                self.ppp.send_ip(build_tcp(dst, dport, PHONE_IP, sport, ack, seq + len(data), F_RST | F_ACK, 0))
            return
        c.on_segment(seq, ack, flags, win, data)

    def sockets(self):
        r, w = [], []
        for c in self.tcp.values():
            if c.state == "connecting":
                w.append(c.sock)
            elif c.state != "closed":
                if not c.server_eof and len(c.pending) <= 65536:
                    r.append(c.sock)
                if c.to_server:
                    w.append(c.sock)
        for k, (s, _) in self.udp.items():
            r.append(s)
        return r, w

    def handle(self, readable, writable):
        bysock = {c.sock: c for c in list(self.tcp.values())}
        udps = {s: k for k, (s, _) in self.udp.items()}
        for s in writable:
            c = bysock.get(s)
            if not c:
                continue
            if c.state == "connecting":
                err = s.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                if err:
                    c.failed()
                else:
                    c.connected()
            else:
                c.flush_to_server()
        for s in readable:
            if s in udps:
                self.udp_readable(udps[s], s)
                continue
            c = bysock.get(s)
            if c and c.state != "closed":
                c.on_readable()

    def tick(self):
        now = time.time()
        for c in list(self.tcp.values()):
            c.tick(now)
            if c.state not in ("closed", "connecting"):
                c.pump()
        for k, (s, last) in list(self.udp.items()):
            if now - last > 60:
                s.close()
                del self.udp[k]


# ------------------------------------------------------------------ main loop
class Status:
    """What the GUI shows; updated by serve()."""
    def __init__(self):
        self.state = "stopped"       # stopped, waiting, negotiating, up, error
        self.port = ""
        self.rx = 0                  # bytes from the phone (raw link)
        self.tx = 0                  # bytes to the phone (raw link)
        self.connections = 0
        self.since = None            # time the IP link came up


class Link:
    def __init__(self, transport, nat):
        self.t = transport
        self.nat = nat
        self.out = queue.Queue()

    def send_frame(self, frame, esc=ESC_ALL):
        self.out.put(hdlc_encode(frame, esc))

    def on_ip(self, pkt):
        self.nat.on_ip(pkt)

    def on_reset(self):
        self.nat.on_reset()


def serve(transport, rewrite, stop_event=None, status=None):
    if status is None:
        status = Status()
    nat = Nat(rewrite)
    link = Link(transport, nat)
    ppp = Ppp(link)
    nat.ppp = ppp
    dec = HdlcDecoder()
    inbox = queue.Queue()
    wake_r, wake_w = socket.socketpair()
    wake_r.setblocking(False)
    stop = threading.Event()

    def reader():
        try:
            while not stop.is_set():
                d = transport.read()
                if d:
                    status.rx += len(d)
                    inbox.put(d)
                    try:
                        wake_w.send(b"x")
                    except OSError:
                        pass
        except Exception as e:
            inbox.put(e)
            try:
                wake_w.send(b"x")
            except OSError:
                pass

    def writer():
        try:
            while not stop.is_set():
                try:
                    d = link.out.get(timeout=0.5)
                except queue.Empty:
                    continue
                # coalesce queued frames into one write
                parts = [d]
                size = len(d)
                while size < 8192:
                    try:
                        d = link.out.get_nowait()
                    except queue.Empty:
                        break
                    parts.append(d)
                    size += len(d)
                data = b"".join(parts)
                transport.write(data)
                status.tx += len(data)
        except Exception as e:
            inbox.put(e)

    threading.Thread(target=reader, daemon=True).start()
    threading.Thread(target=writer, daemon=True).start()
    last_stats = time.time()
    seen_bytes = False
    status.state = "waiting"
    try:
        while not (stop_event is not None and stop_event.is_set()):
            r, w = nat.sockets()
            # don't flood the serial link: while much is queued, stop reading from servers
            if link.out.qsize() > 12:
                r = [s for s in r if s not in [c.sock for c in nat.tcp.values()]]
            rs, ws, _ = select.select(r + [wake_r], w, [], 0.05)
            if wake_r in rs:
                try:
                    wake_r.recv(4096)
                except OSError:
                    pass
                rs.remove(wake_r)
            while True:
                try:
                    item = inbox.get_nowait()
                except queue.Empty:
                    break
                if isinstance(item, Exception):
                    raise item
                if not seen_bytes:
                    seen_bytes = True
                    log("the phone is connected, starting PPP")
                    status.state = "negotiating"
                for f in dec.feed(item):
                    ppp.on_frame(f)
            nat.handle(rs, ws)
            nat.dns_done()
            if seen_bytes:
                ppp.tick()
            nat.tick()
            status.connections = len(nat.tcp)
            if ppp.ip_up and status.state != "up":
                status.state = "up"
                status.since = time.time()
            elif not ppp.ip_up and status.state == "up":
                status.state = "waiting"
                status.since = None
            if time.time() - last_stats > 60 and ppp.ip_up:
                last_stats = time.time()
                tin = nat.total_in + sum(c.bytes_in for c in nat.tcp.values())
                tout = nat.total_out + sum(c.bytes_out for c in nat.tcp.values())
                log("%d connections, %.1f MB to the phone, %.1f MB from the phone" % (len(nat.tcp), tin / 1048576, tout / 1048576))
    finally:
        stop.set()
        nat.on_reset()
        status.connections = 0
        status.since = None


# Addresses the phone can use to reach this PC itself (its local servers, e.g. localserver on
# port 8000): the gateway address, and the PC's USB-link (Windows ICS) address, so apps set up
# for the USB link also work over Bluetooth.
PC_ALIASES = {"10.64.64.1": "127.0.0.1", "192.168.137.1": "127.0.0.1"}


def run(port, rewrite=None, stop_event=None, status=None, tcp=None):
    """Serves the phone on a COM port (or a TCP test port) until stop_event is set;
    reopens the port after a hang-up or an error."""
    merged = dict(PC_ALIASES)
    merged.update(rewrite or {})
    rewrite = merged
    if status is None:
        status = Status()
    status.port = tcp or port
    stop_event = stop_event or threading.Event()
    while not stop_event.is_set():
        t = None
        try:
            if tcp:
                t = TcpTransport(tcp, stop_event)
            elif port in (None, "", "BT", "bluetooth"):
                t = BluetoothTransport(stop_event)
            else:
                t = SerialTransport(port)
                log("listening on %s - on the phone press Connect in GnuBox (Other device: PC)" % port)
            serve(t, rewrite, stop_event, status)
        except KeyboardInterrupt:
            break
        except EOFError:
            log("the phone disconnected; waiting for it again")
        except Exception as e:
            status.state = "error"
            log("link error:", e, "- trying again in 3 s")
            stop_event.wait(3)
        finally:
            if t is not None:
                try:
                    t.close()
                except Exception:
                    pass
    status.state = "stopped"
    log("sharing stopped")


def incoming_bt_ports():
    """COM ports that look like Windows' incoming Bluetooth serial ports (no remote address)."""
    from serial.tools import list_ports      # pip install pyserial
    out = []
    for p in list_ports.comports():
        hw = (p.hwid or "").upper()
        if "BTHENUM" in hw and ("LOCALMFG" in hw or "000000000000" in hw):
            out.append(p.device)
    return out


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", default="BT", help="BT (default: direct Bluetooth) or an incoming Bluetooth COM port, e.g. COM5")
    ap.add_argument("--tcp", help="listen on host:port instead of a COM port (testing)")
    ap.add_argument("--rewrite", action="append", default=[], help="testing: ip=realip")
    ap.add_argument("-v", action="store_true", help="log every connection")
    ap.add_argument("--list", action="store_true", help="list the COM ports and exit")
    a = ap.parse_args()
    if a.list:
        from serial.tools import list_ports      # pip install pyserial
        inc = incoming_bt_ports()
        for p in list_ports.comports():
            print("%-7s %s  %s%s" % (p.device, p.description, p.hwid, "   <- incoming Bluetooth" if p.device in inc else ""))
        print("Use the incoming port from Bluetooth settings > More Bluetooth settings > COM Ports.")
        return
    VERBOSE = a.v
    rewrite = dict(x.split("=", 1) for x in a.rewrite)
    try:
        run(a.port, rewrite, tcp=a.tcp)
    except KeyboardInterrupt:
        log("stopped")


if __name__ == "__main__":
    main()
