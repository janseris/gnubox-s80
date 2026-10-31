# Internet for the Nokia 9300 from a Windows PC over Bluetooth

Shares the PC's internet (Wi-Fi, Ethernet, VPN, whatever Windows uses) with the 9300 over
Bluetooth, the way BlueDUN++ does on Android. The 9300 side is GnuBox in
**Other device: PC** mode.

**Start it:** double-click **`GnuBox Share.bat`** (or `gnubox_share.pyw`). Everything is in
that window.

## How it works

- **Direct Bluetooth (default, nothing to set up in Windows):** the program itself becomes
  a Bluetooth *Serial Port* service. It uses the documented Win32 Winsock Bluetooth API
  (an `AF_BTH` RFCOMM listening socket plus an SDP record registered with `WSASetService`,
  the same calls .NET Bluetooth libraries make with P/Invoke; here through Python `ctypes`).
  GnuBox looks up exactly that service and connects. No COM port, no admin rights.
- **COM port (fallback):** if direct Bluetooth doesn't work on a PC, Windows' *incoming
  Bluetooth COM port* does the same job. Windows has no public API to create that port, so
  the window opens the Windows dialog for it and picks up the new port by itself.
- On top of either: a PPP server (LCP, PAP, IPCP; the phone gets `10.64.64.2`, gateway and
  DNS `10.64.64.1`) and a user-space NAT that carries the phone's TCP, UDP, DNS and ping
  through ordinary Windows sockets. No ICS, no drivers, no Windows network settings change.

## Setup (once)

1. **Python 3** from python.org (tick *Add python.exe to PATH*). Tk comes with it.
   `pyserial` is only needed for the COM-port fallback; the window offers to install it.
2. **Pair** the 9300 with the PC (*Pair the phone...* in the window opens Windows'
   Bluetooth settings).
3. **On the 9300, in GnuBox:** *Device* > choose the PC; *Settings > Other device: PC (pppd)*;
   *Default > GnuBox BT* if apps should use it without asking.

## Every time

1. Open **GnuBox Share**, keep *Bluetooth, direct (no COM port needed)* selected, press
   **Start sharing**. It shows *Waiting for the phone*.
2. On the 9300: GnuBox > **Connect**. The window turns green: *Sharing the internet with the
   phone*, with live speed, connections, data used and connected time.
3. **Stop sharing** or closing the window ends it. If the phone disconnects, it waits for
   the phone again by itself.

Options in the window: *Start sharing when this window opens*, *Start with Windows* (a small
`.bat` in your Startup folder), *Log every connection*. Settings:
`%APPDATA%\GnuBoxShare\settings.json`.

Console version: `start-winshare.bat` (direct) or `start-winshare.bat COM5`;
`python gnubox_winshare.py --list` shows the COM ports.

## What to expect

- Speed: the 9300's Bluetooth 1.1 is the limit, about 30-40 kB/s, the same as via Android.
  On a test link limited to 40 kB/s the program delivered 37.9 kB/s (95%; control bytes are
  not escaped because the phone negotiates ACCM 0).
- DNS goes through Windows' own resolver (hosts file, VPN DNS included). IPv6-only names get
  an empty answer.
- The phone can open connections out; nothing can connect in to the phone.
- Ping to any address is answered by the program itself (only shows the link works).

## Tested (2026-10-31, on Linux; not yet on Windows with the 9300)

`test/phone_sim.py` plays the phone (LCP with PFC/ACFC, PAP, CCP, IPCP with VJ and zero
addresses, like Symbian) and connects a Linux TUN interface, so the real Linux TCP/IP stack
goes through the program (`--tcp 127.0.0.1:7777`). PAP accepted, CCP and VJ rejected,
addresses assigned; 5 MB download and upload identical; 10 parallel downloads identical;
DNS A / AAAA / NXDOMAIN correct; refused port reported; at 40 kB/s: 1 MB at 37.9 kB/s, five
parallel downloads identical, DNS during a download 0.8 s; hang-up answered and a new
negotiation works. The window (`test/gui_test.sh`, Xvfb): start, status, live speed, a 1 MB
download through it identical, stop. The Win32 structures for direct Bluetooth have the
sizes Windows x64 expects (SOCKADDR_BTH 30, CSADDR_INFO 40, WSAQUERYSETW 120 bytes).

**Not yet tested: the real Windows Bluetooth (direct and COM port) with the real 9300.**

## Troubleshooting

- *"Bluetooth socket: error ..." in the log:* Bluetooth is off or there is no adapter.
- *GnuBox: Bluetooth error at Connect:* GnuBox points at another device (*Device* menu), is
  in Android mode (*Settings > Other device: PC*), or the PC isn't paired.
- *Direct Bluetooth never shows the phone connecting:* try *COM port instead...* and select
  the new port.
- *Connected but no pages:* tick *Log every connection* and look for `TCP open ...` lines.
