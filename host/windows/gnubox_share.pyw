#!/usr/bin/env python3
"""
GnuBox Share - a window for sharing this Windows PC's internet with a Nokia 9300 over
Bluetooth (GnuBox "Other device: PC"). Double-click to start (it is a .pyw: no console).

Direct Bluetooth: the program itself is the Serial Port service (Win32 Winsock AF_BTH + SDP
record), no COM port to create. Start/Stop, status, speed, log, start with Windows.
Uses gnubox_winshare.py from the same folder. Needs only Python 3.
"""
import json, os, queue, sys, threading, time
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

STATE_TEXT = {
    "stopped": ("Stopped", "#666666"),
    "waiting": ("Waiting for the phone - press Connect in GnuBox", "#b36b00"),
    "negotiating": ("Phone connected, starting PPP...", "#b36b00"),
    "up": ("Sharing the internet with the phone", "#1a7f37"),
    "error": ("Bluetooth problem - retrying", "#c62828"),
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
        self.port = TEST_LABEL if TEST_TCP else BT_DIRECT

        root.title(APP + " - internet for the Nokia 9300")
        root.geometry("720x560")
        root.minsize(600, 440)
        root.protocol("WM_DELETE_WINDOW", self.on_close)
        pad = {"padx": 8, "pady": 4}

        # --- Bluetooth
        pf = ttk.LabelFrame(root, text="Bluetooth")
        pf.pack(fill="x", **pad)
        hint = ("Test mode: a simulated phone connects over TCP (%s)." % TEST_TCP if TEST_TCP else
                "This PC offers a Bluetooth Serial Port service; GnuBox on the 9300 connects to it.\n"
                "No COM port needed. Pair the phone with this PC once.")
        ttk.Label(pf, text=hint, justify="left").grid(row=0, column=0, padx=6, pady=6, sticky="w")
        ttk.Button(pf, text="Pair the phone...", command=self.open_pairing).grid(row=0, column=1, padx=6)
        pf.columnconfigure(0, weight=1)

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
        save_cfg(self.cfg)

    def set_verbose(self):
        ws.VERBOSE = self.verbose_var.get()
        self.save()

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
        port = self.port
        self.save()
        self.stop_event = threading.Event()
        self.status = ws.Status()
        self.last = (time.time(), 0, 0)
        tcp = TEST_TCP if port == TEST_LABEL else None
        rewrite = dict(x.split("=", 1) for x in os.environ.get("GNUBOX_TEST_REWRITE", "").split(",") if "=" in x)
        self.thread = threading.Thread(target=ws.run, args=(port, rewrite, self.stop_event, self.status, tcp), daemon=True)
        self.thread.start()
        self.go_btn.configure(text="Stop sharing")

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
