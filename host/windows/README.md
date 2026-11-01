# Internet for the Nokia 9300 from a Windows PC over Bluetooth

Shares the PC's internet (Wi-Fi, Ethernet, VPN, whatever Windows uses) with the 9300 over
Bluetooth, the way BlueDUN++ does on Android. The 9300 side is GnuBox in
**Other device: PC** mode.

**Start it:** double-click **`GnuBox Share.bat`** (or `gnubox_share.pyw`). Everything is in
that window. There is also a C# / .NET 10 version with a tray icon and dark mode, a single
`GnuBoxShare.exe` without Python: see [`../windows-cs`](../windows-cs/README.md).

## How it works

- **Direct Bluetooth (nothing to set up in Windows):** the program itself becomes
  a Bluetooth *Serial Port* service. It uses the documented Win32 Winsock Bluetooth API
  (an `AF_BTH` RFCOMM listening socket plus an SDP record registered with `WSASetService`,
  the same calls .NET Bluetooth libraries make with P/Invoke; here through Python `ctypes`).
  GnuBox looks up exactly that service and connects. No COM port, no admin rights.
- On top of it: a PPP server (LCP, PAP, IPCP; the phone gets `10.64.64.2`, gateway and
  DNS `10.64.64.1`) and a user-space NAT that carries the phone's TCP, UDP, DNS and ping
  through ordinary Windows sockets. No ICS, no drivers, no Windows network settings change.

## Setup (once)

1. **Python 3** from python.org (tick *Add python.exe to PATH*). Tk comes with it.
2. **Pair** the 9300 with the PC (*Pair the phone...* in the window opens Windows'
   Bluetooth settings).
3. **On the 9300, in GnuBox:** *Device* > choose the PC; *Settings > Other device: PC (pppd)*;
   *Default > GnuBox BT* if apps should use it without asking.

## Every time

1. Open **GnuBox Share**, press **Start sharing**. It shows *Waiting for the phone*.
2. On the 9300: GnuBox > **Connect**. The window turns green: *Sharing the internet with the
   phone*, with live speed, connections, data used and connected time.
3. **Stop sharing** or closing the window ends it. If the phone disconnects, it waits for
   the phone again by itself.

Options in the window: *Start sharing when this window opens*, *Start with Windows* (a small
`.bat` in your Startup folder), *Log every connection*. Settings:
`%APPDATA%\GnuBoxShare\settings.json`.

Console version: `start-winshare.bat`. (It can still serve an incoming Bluetooth COM port,
`start-winshare.bat COM5`, which needs `pip install pyserial`; not needed in practice.)

## What to expect

- Speed: the 9300's Bluetooth 1.1 is the limit, about 30-40 kB/s, the same as via Android.
  On a test link limited to 40 kB/s the program delivered 37.9 kB/s (95%; control bytes are
  not escaped because the phone negotiates ACCM 0).
- DNS goes through Windows' own resolver (hosts file, VPN DNS included). IPv6-only names get
  an empty answer.
- **The PC itself** (its local servers, e.g. `localserver` on port 8000 or the FileFetch
  server on 8010) is reachable from the phone as **`10.64.64.1`** or **`192.168.137.1`**: both
  go to this PC. So apps set up for the USB link (AudioProbe, FileFetch, Instagram9300 with
  `192.168.137.1:8000`) work unchanged over Bluetooth, even without the USB cable.
- The phone can open connections out; nothing can connect in to the phone.
- Ping to any address is answered by the program itself (only shows the link works).

## Tested

`test/phone_sim.py` plays the phone (LCP with PFC/ACFC, PAP, CCP, IPCP with VJ and zero
addresses, like Symbian) and connects a Linux TUN interface, so the real Linux TCP/IP stack
goes through the program (`--tcp 127.0.0.1:7777`). PAP accepted, CCP and VJ rejected,
addresses assigned; 5 MB download and upload identical; 10 parallel downloads identical;
DNS A / AAAA / NXDOMAIN correct; refused port reported; at 40 kB/s: 1 MB at 37.9 kB/s, five
parallel downloads identical, DNS during a download 0.8 s; hang-up answered and a new
negotiation works. The window (`test/gui_test.sh`, Xvfb): start, status, live speed, a 1 MB
download through it identical, stop. The Win32 structures for direct Bluetooth have the
sizes Windows x64 expects (SOCKADDR_BTH 30, CSADDR_INFO 40, WSAQUERYSETW 120 bytes).

**On Windows with the real 9300 (2026-10-31): works.** 1 MB from `localserver` at 31 kB/s,
256 KB at 28 kB/s (about 1.5 times BlueDUN++). The window (direct Bluetooth) is what was
used; the COM-port path was never needed and the window no longer offers it.

## Troubleshooting

- *"Bluetooth socket: error ..." in the log:* Bluetooth is off or there is no adapter.
- *GnuBox: Bluetooth error at Connect:* GnuBox points at another device (*Device* menu), is
  in Android mode (*Settings > Other device: PC*), or the PC isn't paired.
- *The phone never connects:* unpair and pair again; in GnuBox check *Device* and
  *Settings > Other device: PC*.
- *Connected but no pages:* tick *Log every connection* and look for `TCP open ...` lines.
