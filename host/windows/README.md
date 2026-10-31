# Internet for the Nokia 9300 from a Windows PC over Bluetooth

`gnubox_winshare.py` shares the PC's internet (Wi-Fi, Ethernet, VPN, whatever Windows uses)
with the 9300 over Bluetooth, the way BlueDUN++ does on Android. The 9300 side is GnuBox
in **Other device: PC** mode.

How it works: Windows offers an *incoming Bluetooth COM port* (Serial Port Profile). GnuBox
connects to it and starts PPP at once. The program answers the PPP (LCP, PAP, IPCP; gives
the phone `10.64.64.2`, gateway and DNS `10.64.64.1`) and carries the phone's TCP, UDP, DNS
and ping through ordinary Windows sockets (a user-space NAT). No ICS, no drivers, no admin
rights, no Windows network settings change.

## Setup (once)

1. **Pair** the 9300 with the PC (Windows Bluetooth settings, or Control panel > Bluetooth on
   the 9300).
2. **Incoming COM port:** Windows Settings > Bluetooth & devices > Devices > *More Bluetooth
   settings* > **COM Ports** tab > *Add...* > **Incoming** > OK. Note the port, e.g. `COM5`.
   (Windows registers it as a Serial Port service, which GnuBox finds.)
3. **Python 3** from python.org, then `pip install pyserial` (`start-winshare.bat` does it).
4. **On the 9300, in GnuBox:** *Device* > choose the PC; *Settings > Other device: PC (pppd)*;
   *Default > GnuBox BT* if apps should use it without asking.

## Every time

1. On the PC: `start-winshare.bat COM5` (or `python gnubox_winshare.py COM5`).
   It prints `listening on COM5`.
2. On the 9300: GnuBox > **Connect**. The PC prints `PPP: link up`, then
   `PPP: IP up - phone 10.64.64.2`; GnuBox shows *CONNECTED*.
3. Leave the window open. `Ctrl+C` stops it. If the phone disconnects, the program waits for
   it again by itself.

`python gnubox_winshare.py --list` shows the COM ports. `-v` logs every connection.

## What to expect

- Speed: the 9300's Bluetooth 1.1 is the limit, about 30-40 kB/s, the same as via Android.
  On a test link limited to 40 kB/s the program delivered 37.9 kB/s (95%).
- DNS goes through Windows' own resolver (hosts file, VPN DNS and so on included). IPv6-only
  names get an empty answer.
- The phone can open connections out; nothing can connect in to the phone.
- Ping (ICMP echo) to any address is answered by the program itself (only shows the link
  works).

## Tested (2026-10-31, on Linux, not yet with the 9300)

`test/phone_sim.py` plays the phone: LCP with PFC/ACFC, PAP login, a CCP request, IPCP with
VJ compression and zero addresses (like Symbian), then a Linux TUN interface so the real Linux
TCP/IP stack goes through the program (`--tcp 127.0.0.1:7777` instead of a COM port).
Results: PAP accepted, CCP protocol-rejected, VJ rejected, addresses assigned; 5 MB
download and upload byte-identical; 10 parallel downloads identical; DNS A / AAAA / NXDOMAIN
correct; refused port reported; with the link limited to 40 kB/s: 1 MB at 37.9 kB/s, five
parallel downloads identical, DNS answer during a download in 0.8 s; hang-up
(Terminate-Request) answered, no traffic afterwards, a new negotiation works.

Not yet tested: the real Windows Bluetooth COM port and the real 9300.

## Troubleshooting

- *GnuBox: "not found" / Bluetooth error at Connect:* the incoming COM port is missing, or
  GnuBox points at another device (*Device* menu) or is in Android mode (*Settings > Other
  device: PC*).
- *The PC shows nothing:* wrong COM port (the **incoming** one, not outgoing).
- *Connected but no pages:* run with `-v` and look for `TCP open ...` lines.
