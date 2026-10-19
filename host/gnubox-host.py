#!/usr/bin/env python3
"""GnuBox PC side (Linux, BlueZ 5): Internet for a Nokia 9300/9500 over Bluetooth.

Registers a Bluetooth Serial Port (SPP, UUID 0x1101) service through BlueZ's
Profile API (no deprecated rfcomm/sdptool needed) and runs pppd on every
incoming RFCOMM connection. The phone's "GnuBox BT" access point opens
BTCOMM::0, which finds this service by SDP and connects; PPP starts at once.

    sudo ./gnubox-host.py                  # NAT through the default route
    sudo ./gnubox-host.py --channel 3 --dns 9.9.9.9 --debug

Needs: bluez (bluetoothd running), ppp, python3-dbus, python3-gi, nft or iptables.
GPL v2 or later.
"""
import argparse
import os
import shutil
import signal
import subprocess
import sys

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

SPP_UUID = "00001101-0000-1000-8000-00805f9b34fb"
PROFILE_PATH = "/org/gnubox/spp"


def log(*a):
    print("[gnubox]", *a, flush=True)


def pick_dns():
    """First non-loopback name server of the host, else a public one."""
    servers = []
    try:
        out = subprocess.run(["resolvectl", "dns"], capture_output=True, text=True, timeout=5).stdout
        for line in out.splitlines():
            servers += line.split(":", 1)[-1].split()
    except Exception:
        pass
    try:
        with open("/etc/resolv.conf") as f:
            servers += [l.split()[1] for l in f if l.startswith("nameserver") and len(l.split()) > 1]
    except OSError:
        pass
    for s in servers:
        if "." in s and not s.startswith("127.") and s.count(".") == 3:
            return s
    return "1.1.1.1"


def default_iface():
    out = subprocess.run(["ip", "-4", "route", "show", "default"], capture_output=True, text=True).stdout
    parts = out.split()
    return parts[parts.index("dev") + 1] if "dev" in parts else None


def setup_nat(subnet, iface):
    with open("/proc/sys/net/ipv4/ip_forward", "w") as f:
        f.write("1\n")
    if shutil.which("nft"):
        rules = f"""
table ip gnubox {{
  chain post {{ type nat hook postrouting priority 100; ip saddr {subnet} oifname "{iface}" masquerade; }}
  chain fwd {{ type filter hook forward priority -10; ip saddr {subnet} accept; ip daddr {subnet} ct state established,related accept; }}
}}"""
        subprocess.run(["nft", "delete", "table", "ip", "gnubox"], stderr=subprocess.DEVNULL)
        subprocess.run(["nft", "-f", "-"], input=rules, text=True, check=True)
        return lambda: subprocess.run(["nft", "delete", "table", "ip", "gnubox"], stderr=subprocess.DEVNULL)
    rule = ["POSTROUTING", "-s", subnet, "-o", iface, "-j", "MASQUERADE"]
    fwd1 = ["FORWARD", "-s", subnet, "-j", "ACCEPT"]
    fwd2 = ["FORWARD", "-d", subnet, "-m", "state", "--state", "ESTABLISHED,RELATED", "-j", "ACCEPT"]
    subprocess.run(["iptables", "-t", "nat", "-A"] + rule, check=True)
    subprocess.run(["iptables", "-I"] + fwd1, check=True)
    subprocess.run(["iptables", "-I"] + fwd2, check=True)

    def undo():
        subprocess.run(["iptables", "-t", "nat", "-D"] + rule)
        subprocess.run(["iptables", "-D"] + fwd1)
        subprocess.run(["iptables", "-D"] + fwd2)
    return undo


class SerialProfile(dbus.service.Object):
    def __init__(self, bus, args):
        super().__init__(bus, PROFILE_PATH)
        self.args = args
        self.children = {}

    @dbus.service.method("org.bluez.Profile1", in_signature="", out_signature="")
    def Release(self):
        log("profile released by bluetoothd")

    @dbus.service.method("org.bluez.Profile1", in_signature="oha{sv}", out_signature="")
    def NewConnection(self, device, fd, props):
        fd = fd.take()
        a = self.args
        log(f"connection from {device}, starting pppd {a.local}:{a.remote}")
        cmd = ["pppd", "notty", "nodetach", "noauth", "local", "nocrtscts", "nodefaultroute",
               f"{a.local}:{a.remote}", "ms-dns", a.dns,
               "lcp-echo-interval", "30", "lcp-echo-failure", "4", "nopersist", "maxfail", "1"]
        if a.debug:
            cmd += ["debug", "logfd", "2"]
        proc = subprocess.Popen(cmd, stdin=fd, stdout=fd, pass_fds=(fd,), close_fds=True)
        os.close(fd)
        self.children[str(device)] = proc
        GLib.child_watch_add(proc.pid, self._ppp_exited, str(device))

    def _ppp_exited(self, pid, status, device):
        log(f"pppd for {device} exited (status {status})")
        self.children.pop(device, None)

    @dbus.service.method("org.bluez.Profile1", in_signature="o", out_signature="")
    def RequestDisconnection(self, device):
        proc = self.children.pop(str(device), None)
        if proc:
            proc.terminate()
        log(f"disconnect requested for {device}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--channel", type=int, default=1, help="RFCOMM channel (default 1)")
    p.add_argument("--local", default="10.66.0.1", help="PC address on the PPP link")
    p.add_argument("--remote", default="10.66.0.2", help="phone address on the PPP link")
    p.add_argument("--dns", default=None, help="DNS server handed to the phone (default: host's)")
    p.add_argument("--iface", default=None, help="uplink interface for NAT (default: default route)")
    p.add_argument("--no-nat", action="store_true", help="do not touch forwarding/NAT")
    p.add_argument("--auth", action="store_true", help="require Bluetooth pairing for the service")
    p.add_argument("--debug", action="store_true", help="pppd debug output")
    a = p.parse_args()

    if os.geteuid() != 0:
        sys.exit("run as root (pppd and NAT need it)")
    for tool in ("pppd", "ip"):
        if not shutil.which(tool):
            sys.exit(f"missing {tool} (install ppp / iproute2)")
    a.dns = a.dns or pick_dns()

    undo_nat = None
    if not a.no_nat:
        iface = a.iface or default_iface()
        if not iface:
            sys.exit("no default route - give --iface or --no-nat")
        undo_nat = setup_nat(f"{a.remote}/32", iface)
        log(f"NAT {a.remote} -> {iface}")

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    profile = SerialProfile(bus, a)
    mgr = dbus.Interface(bus.get_object("org.bluez", "/org/bluez"), "org.bluez.ProfileManager1")
    mgr.RegisterProfile(PROFILE_PATH, SPP_UUID, {
        "Name": "GnuBox Serial Port",
        "Role": "server",
        "Channel": dbus.UInt16(a.channel),
        "RequireAuthentication": dbus.Boolean(a.auth),
        "RequireAuthorization": dbus.Boolean(False),
        "AutoConnect": dbus.Boolean(False),
    })
    log(f"Serial Port service on RFCOMM channel {a.channel}, DNS for phone {a.dns}")
    log("make the PC visible/pairable once: bluetoothctl -> discoverable on, pairable on")
    log("waiting for the phone (GnuBox -> Test) ...")

    loop = GLib.MainLoop()

    def stop(*_):
        loop.quit()
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    try:
        loop.run()
    finally:
        for proc in list(profile.children.values()):
            proc.terminate()
        try:
            mgr.UnregisterProfile(PROFILE_PATH)
        except dbus.DBusException:
            pass
        if undo_nat:
            undo_nat()
        log("stopped")


if __name__ == "__main__":
    main()
