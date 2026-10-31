#!/usr/bin/env python3
"""
GnuBox Share - a window for sharing this Windows PC's internet with a Nokia 9300 over
Bluetooth (GnuBox "Other device: PC"). Double-click to start (it is a .pyw: no console).

Everything in one place. Default: direct Bluetooth - the program itself is the Serial Port
service (Win32 Winsock AF_BTH + SDP record), no COM port to create. Fallback: an incoming
Bluetooth COM port (it opens the Windows dialog to create one and notices the new port).
Start/Stop, status, speed, log, start with Windows.
Uses gnubox_winshare.py from the same folder. Needs Python 3 and pyserial (offers to install it).
"""
import json, os, queue, subprocess, sys, threading, time
import tkinter as tk
from tkinter import ttk, messagebox
from tkinter.scrolledtext import ScrolledText

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gnubox_winshare as ws

APP = "GnuBox Share"
CFG_DIR = os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "GnuBoxShare")
CFG = os.path.join(CFG_DIR, "settings.json")
STARTUP_DIR = os.path.join(os.environ.get("APPDATA", ""), r"Microsoft\Windows\Start Menu\Programs\Startup")
STARTUP_FILE = os.path.join(STARTUP_DIR, "GnuBox Share.bat")
TEST_TCP = os.environ.get("GNUBOX_TEST_TCP")          # testing without Bluetooth
TEST_LABEL = "TCP test port"
BT_DIRECT = "BT"
BT_LABEL = "Bluetooth, direct (no COM port needed)"

STATE_TEXT = {
    "stopped": ("Stopped", "#666666"),
    "waiting": ("Waiting for the phone - press Connect in GnuBox", "#b36b00"),
    "negotiating": ("Phone connected, starting PPP...", "#b36b00"),
    "up": ("Sharing the internet with the phone", "#1a7f37"),
    "error": ("Problem with the port - retrying", "#c62828"),
}


def load_cfg():
    try:
        with open(CFG, encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def save_cfg(cfg):
    try:
        os.makedirs(CFG_DIR, exist_ok=True)
        with open(CFG, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=1)
    except Exception:
        pass


def have_pyserial():
    try:
        import serial  # noqa: F401
        from serial.tools import list_ports  # noqa: F401
        return True
    except ImportError:
        return False


def fmt_bytes(n):
    return "%.1f MB" % (n / 1048576.0) if n >= 1048576 else "%d KB" % (n // 1024)


class App:
    def __init__(self, root, autostart):
        self.root = root
        self.cfg = load_cfg()
        self.logq = queue.Queue()
        ws.LOG_SINK = self.logq.put
        self.thread = None
        self.stop_event = None
        self.status = ws.Status()
        self.last = (time.time(), 0, 0)
        self.watch_until = 0
        self.known_ports = set()
        self.label_to_dev = {}

        root.title(APP + " - internet for the Nokia 9300")
        root.geometry("720x560")
        root.minsize(600, 440)
        root.protocol("WM_DELETE_WINDOW", self.on_close)
        pad = {"padx": 8, "pady": 4}

        # --- port
        pf = ttk.LabelFrame(root, text="How the phone connects")
        pf.pack(fill="x", **pad)
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(pf, textvariable=self.port_var, state="readonly", width=40)
        self.port_box.grid(row=0, column=0, padx=6, pady=6, sticky="w")
        ttk.Button(pf, text="Refresh", command=self.refresh_ports).grid(row=0, column=1, padx=3)
        ttk.Button(pf, text="Pair the phone...", command=self.open_pairing).grid(row=0, column=2, padx=3)
        ttk.Button(pf, text="COM port instead...", command=self.create_port).grid(row=0, column=3, padx=3)
        self.port_hint = ttk.Label(pf, text="", foreground="#555555")
        self.port_box.bind("<<ComboboxSelected>>", self.update_hint)
        self.port_hint.grid(row=1, column=0, columnspan=4, padx=6, pady=(0, 6), sticky="w")

        # --- status + start/stop
        sf = ttk.Frame(root)
        sf.pack(fill="x", **pad)
        self.state_lbl = tk.Label(sf, text="Stopped", font=("Segoe UI", 13, "bold"), fg="#666666", anchor="w")
        self.state_lbl.pack(side="left", fill="x", expand=True)
        self.go_btn = ttk.Button(sf, text="Start sharing", command=self.toggle, width=18)
        self.go_btn.pack(side="right")
        self.stats_lbl = ttk.Label(root, text="", font=("Segoe UI", 10))
        self.stats_lbl.pack(fill="x", padx=10)

        # --- options
        of = ttk.Frame(root)
        of.pack(fill="x", **pad)
        self.auto_var = tk.BooleanVar(value=self.cfg.get("autostart_share", False))
        self.boot_var = tk.BooleanVar(value=os.path.exists(STARTUP_FILE))
        self.verbose_var = tk.BooleanVar(value=self.cfg.get("verbose", False))
        ttk.Checkbutton(of, text="Start sharing when this window opens", variable=self.auto_var,
                        command=self.save).pack(side="left")
        ttk.Checkbutton(of, text="Start with Windows", variable=self.boot_var,
                        command=self.toggle_boot).pack(side="left", padx=12)
        ttk.Checkbutton(of, text="Log every connection", variable=self.verbose_var,
                        command=self.set_verbose).pack(side="left")

        # --- phone steps
        hf = ttk.LabelFrame(root, text="On the Nokia 9300 (GnuBox)")
        hf.pack(fill="x", **pad)
        ttk.Label(hf, justify="left", text=(
            "1. Device > choose this PC     2. Settings > Other device: PC (pppd)     "
            "3. Press Connect\n"
            "Apps use it without asking when GnuBox's Default menu is set to GnuBox BT.")).pack(anchor="w", padx=6, pady=4)

        # --- log
        lf = ttk.LabelFrame(root, text="Log")
        lf.pack(fill="both", expand=True, **pad)
        self.log = ScrolledText(lf, height=10, font=("Consolas", 9), state="disabled", wrap="word")
        self.log.pack(fill="both", expand=True, padx=4, pady=4)

        ws.VERBOSE = self.verbose_var.get()
        self.refresh_ports(select_saved=True)
        self.root.after(500, self.poll)
        if autostart or self.auto_var.get():
            self.root.after(800, self.start)

    # ---------------------------------------------------------------- helpers
    def add_log(self, line):
        self.log.configure(state="normal")
        self.log.insert("end", line + "\n")
        if int(self.log.index("end-1c").split(".")[0]) > 2000:
            self.log.delete("1.0", "500.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def save(self):
        self.cfg["autostart_share"] = self.auto_var.get()
        self.cfg["verbose"] = self.verbose_var.get()
        p = self.selected_port()
        if p:
            self.cfg["port"] = p
        save_cfg(self.cfg)

    def set_verbose(self):
        ws.VERBOSE = self.verbose_var.get()
        self.save()

    def offer_pyserial(self):
        if messagebox.askyesno(APP, "The Python package 'pyserial' is needed for the Bluetooth COM port.\n\n"
                               "Install it now (pip install pyserial)?"):
            self.add_log("installing pyserial...")
            self.root.update()
            r = subprocess.run([sys.executable.replace("pythonw.exe", "python.exe"), "-m", "pip", "install", "pyserial"],
                               capture_output=True, text=True)
            self.add_log((r.stdout or "") + (r.stderr or ""))
            if not have_pyserial():
                messagebox.showerror(APP, "pyserial could not be installed. Run:  pip install pyserial")

    # ---------------------------------------------------------------- ports
    def list_ports(self):
        """[(device, label, is_incoming)]"""
        out = []
        if TEST_TCP:
            out.append((TEST_LABEL, TEST_LABEL + " (" + TEST_TCP + ")", True))
        if sys.platform == "win32" or not TEST_TCP:
            out.append((BT_DIRECT, BT_LABEL, True))
        if not have_pyserial():
            return out
        from serial.tools import list_ports
        incoming = set(ws.incoming_bt_ports())
        for p in sorted(list_ports.comports(), key=lambda p: (p.device not in incoming, p.device)):
            bt = "BTHENUM" in (p.hwid or "").upper()
            kind = "incoming Bluetooth" if p.device in incoming else ("outgoing Bluetooth" if bt else p.description)
            out.append((p.device, "%s  (%s)" % (p.device, kind), p.device in incoming))
        return out

    def selected_port(self):
        return self.label_to_dev.get(self.port_var.get(), "")

    def refresh_ports(self, select_saved=False, prefer=None):
        ports = self.list_ports()
        self.label_to_dev = dict((p[1], p[0]) for p in ports)
        self.known_ports = set(p[0] for p in ports)
        labels = [p[1] for p in ports]
        self.port_box["values"] = labels
        current = prefer or (self.cfg.get("port") if select_saved else self.selected_port()) or BT_DIRECT
        pick = None
        for dev, label, inc in ports:
            if dev == current:
                pick = label
        if pick is None:
            for dev, label, inc in ports:
                if inc:
                    pick = label
                    break
        self.port_var.set(pick or "")
        self.update_hint()
        return ports

    def update_hint(self, *_):
        p = self.selected_port()
        if p == BT_DIRECT:
            t = "This program registers a Bluetooth Serial Port service itself; GnuBox finds it. Pair the phone first."
        elif p == TEST_LABEL:
            t = "Test mode: a simulated phone connects over TCP."
        elif p:
            t = "Incoming Bluetooth COM port %s: Windows advertises it as a Serial Port service." % p
        else:
            t = ""
        self.port_hint.configure(text=t)

    def create_port(self):
        if not have_pyserial():
            self.offer_pyserial()
            if not have_pyserial():
                return
        messagebox.showinfo(APP, "Only needed if direct Bluetooth doesn't work on this PC.\n\n"
                            "Windows will open its Bluetooth settings window.\n\n"
                            "1. Open the 'COM Ports' tab\n2. Add...\n3. Choose 'Incoming (device initiates the connection)'\n"
                            "4. OK, OK\n\nThis window notices the new port by itself.")
        try:
            subprocess.Popen(["rundll32.exe", "shell32.dll,Control_RunDLL", "bthprops.cpl,,2"])
        except Exception as e:
            self.add_log("could not open the Bluetooth settings window: %s" % e)
            try:
                os.startfile("ms-settings:bluetooth")
            except Exception:
                pass
        self.watch_until = time.time() + 180

    def open_pairing(self):
        try:
            os.startfile("ms-settings:bluetooth")
        except Exception as e:
            self.add_log("could not open Bluetooth settings: %s" % e)

    # ---------------------------------------------------------------- start/stop
    def toggle(self):
        if self.thread and self.thread.is_alive():
            self.stop()
        else:
            self.start()

    def start(self):
        if self.thread and self.thread.is_alive():
            return
        port = self.selected_port()
        if not port:
            messagebox.showwarning(APP, "Choose how the phone connects first.")
            return
        if port not in (TEST_LABEL, BT_DIRECT) and not have_pyserial():
            self.offer_pyserial()
            if not have_pyserial():
                return
        self.save()
        self.stop_event = threading.Event()
        self.status = ws.Status()
        self.last = (time.time(), 0, 0)
        tcp = TEST_TCP if port == TEST_LABEL else None
        rewrite = dict(x.split("=", 1) for x in os.environ.get("GNUBOX_TEST_REWRITE", "").split(",") if "=" in x)
        self.thread = threading.Thread(target=ws.run, args=(port, rewrite, self.stop_event, self.status, tcp), daemon=True)
        self.thread.start()
        self.go_btn.configure(text="Stop sharing")
        self.port_box.configure(state="disabled")

    def stop(self):
        if self.stop_event:
            self.stop_event.set()
        self.go_btn.configure(text="Stopping...", state="disabled")

    def on_close(self):
        if self.thread and self.thread.is_alive():
            self.stop_event.set()
            self.thread.join(timeout=3)
        self.root.destroy()

    # ---------------------------------------------------------------- start with Windows
    def toggle_boot(self):
        try:
            if self.boot_var.get():
                pyw = sys.executable
                cand = os.path.join(os.path.dirname(pyw), "pythonw.exe")
                if os.path.exists(cand):
                    pyw = cand
                os.makedirs(STARTUP_DIR, exist_ok=True)
                with open(STARTUP_FILE, "w", encoding="utf-8") as f:
                    f.write('@echo off\r\nstart "" "%s" "%s" --autostart\r\n' % (pyw, os.path.abspath(__file__)))
                self.add_log("starts with Windows: " + STARTUP_FILE)
            elif os.path.exists(STARTUP_FILE):
                os.remove(STARTUP_FILE)
                self.add_log("no longer starts with Windows")
        except Exception as e:
            messagebox.showerror(APP, "Could not change the Windows startup setting:\n%s" % e)
            self.boot_var.set(os.path.exists(STARTUP_FILE))

    # ---------------------------------------------------------------- polling
    def poll(self):
        while True:
            try:
                self.add_log(self.logq.get_nowait())
            except queue.Empty:
                break
        running = self.thread is not None and self.thread.is_alive()
        if not running and self.go_btn.cget("text") != "Start sharing":
            self.go_btn.configure(text="Start sharing", state="normal")
            self.port_box.configure(state="readonly")
            self.status.state = "stopped"
        st = self.status.state if running else "stopped"
        text, color = STATE_TEXT.get(st, (st, "#000000"))
        if st == "waiting" and self.status.port:
            text = "Waiting for the phone on %s - press Connect in GnuBox" % self.status.port
        self.state_lbl.configure(text=text, fg=color)
        now = time.time()
        t0, rx0, tx0 = self.last
        if now - t0 >= 1.0:
            dt = now - t0
            down = (self.status.tx - tx0) / dt / 1024.0
            up = (self.status.rx - rx0) / dt / 1024.0
            self.last = (now, self.status.rx, self.status.tx)
            if running and st in ("up", "negotiating"):
                n = self.status.connections
                s = "Down %.1f kB/s   Up %.1f kB/s   |   %d connection%s   |   %s down, %s up" % (
                    down, up, n, "" if n == 1 else "s", fmt_bytes(self.status.tx), fmt_bytes(self.status.rx))
                if self.status.since:
                    secs = int(now - self.status.since)
                    s += "   |   connected %d:%02d:%02d" % (secs // 3600, secs // 60 % 60, secs % 60)
                self.stats_lbl.configure(text=s)
            elif not running:
                self.stats_lbl.configure(text="")
        if self.watch_until and now < self.watch_until and int(now) % 2 == 0:
            before = set(self.known_ports)
            ports = self.refresh_ports()
            new = [p for p in ports if p[2] and p[0] not in before]
            if new:
                self.watch_until = 0
                self.refresh_ports(prefer=new[0][0])
                self.add_log("new incoming Bluetooth port: %s - selected" % new[0][0])
                self.save()
        self.root.after(500, self.poll)


def main():
    root = tk.Tk()
    try:
        root.tk.call("tk", "scaling", 1.25)
    except tk.TclError:
        pass
    App(root, "--autostart" in sys.argv)
    root.mainloop()


if __name__ == "__main__":
    main()
