# GnuBox Share (C# / .NET 10)

The Windows PC's internet for the Nokia 9300 over Bluetooth, as one small `GnuBoxShare.exe`.
The same thing as the Python version in [`../windows`](../windows/README.md): the program is a
Bluetooth *Serial Port* service (no COM port), runs a PPP server and a user-space NAT. The
9300 side is GnuBox in **Settings > Other device: PC (pppd)** mode.

Extras over the Python version: follows Windows' **light/dark mode**, sits in the **tray**
(the dot on the icon shows the state: grey stopped, orange waiting, green sharing, red
problem), only one copy runs (starting it again shows the window), no Python needed, and:

- **Speed graph:** download (blue, filled) and upload (orange) per second, live. Dashed lines
  are the peaks; the stats line shows them too ("peak" = the best one-second rate since
  Start sharing). Hover over the graph for the exact time and speeds. Right-click: show the
  last 1, 2, 5, 15 minutes or 1 hour; reset the peaks; clear.
- **The phone's requests** (tick *Show the phone's requests*): every HTTP request with
  method, status, size, time, URL, content type and server; click one for the details
  (timing, bytes, request and response headers). HTTPS is encrypted, so for it only the
  server name (if the phone sends it) and the amount of data are shown. Also listed: other
  TCP connections, UDP, and DNS lookups (*Hide DNS* is on by default). Filter box, *Clear*,
  right-click to copy the URL or the details. The list keeps the last 3000 entries and stays
  across Stop/Start.

- **What limits the speed** (1.2): a *Link* line under the speed shows the phone's TCP window
  (and how often it was full), the round trip (and its minimum), how busy Windows' Bluetooth
  send was, and the serial frame size agreed with the phone. While a download runs, a verdict
  says which one is the limit: the phone's 8 KB TCP window (then try GnuBox *Settings > Bigger
  TCP window* and restart the phone), the Bluetooth link itself, or neither. The log also says
  which compression the phone asks for. Request details show the window and round trip of that
  request's connection. *Test: short send queue* keeps 4 instead of 12 packets waiting for
  Bluetooth, to compare.

- **Log files for analysis** (1.3, on by default, *Save logs for analysis*): in a `logs` folder
  next to `GnuBoxShare.exe` (or `%LOCALAPPDATA%\GnuBoxShare\logs` if that isn't writable), one
  set per day, kept 14 days: `gnubox-share-<date>.log` (every log line with milliseconds,
  connection details included), `speed-<date>.csv` (one row per second while connected; every
  10 s when idle: speeds, connections, queued bytes, Bluetooth busy, phone window, round trip,
  window-full / queue-full samples, frame size, test switch), `requests-<date>.jsonl` (every
  finished request: timing, sizes, status, type, its connection's window and round trip,
  headers).

## Run it

1. Needs the **.NET 10 Desktop Runtime** (x64). If it is missing, Windows says so on start and
   offers the download; or get it from <https://dotnet.microsoft.com/download/dotnet/10.0>
   (*.NET Desktop Runtime*).
2. Pair the 9300 with the PC once (*Pair the phone...* opens Windows' Bluetooth settings).
3. Start `GnuBoxShare.exe`, press **Start sharing**. It shows *Waiting for the phone*.
4. On the 9300: GnuBox > **Connect**. The window turns green with live speed, connections,
   data used and connected time.

Closing the window keeps it running in the tray (untick *Keep running in the tray...* to
make closing quit). Tray menu: Open, Start/Stop sharing, Exit.

Options: *Start sharing when GnuBox Share opens*, *Start with Windows (in the tray)*
(registry `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, value `GnuBox Share`),
*Log every connection*. Settings: `%APPDATA%\GnuBoxShare\GnuBoxShare.json`.

The phone reaches this PC itself as `10.64.64.1` or `192.168.137.1` (e.g. `localserver` on
port 8000), the same addresses as over USB.

## Build

`publish.bat` (needs the .NET 10 SDK) makes `publish\GnuBoxShare.exe` (about 0.5 MB):

    dotnet publish GnuBoxShare\GnuBoxShare.csproj -c Release -r win-x64 -p:SelfContained=false -p:PublishSingleFile=true -p:DebugType=none -o publish

Or open `GnuBoxShare.slnx` in Visual Studio 2026. Without the runtime on the target PC, use
`-p:SelfContained=true` instead (about 110 MB).

## Projects

| Project | What |
|---|---|
| `GnuBoxShare.Core` (net10.0) | PPP (`Ppp.cs`: HDLC, LCP, PAP, IPCP), NAT (`Nat.cs`: TCP, UDP, DNS, ping), the requests list (`Traffic.cs`: HTTP/1.x parser incl. keep-alive and chunked, TLS server name), the event loop and reconnect (`Engine.cs`), helpers (`Basics.cs`). No Windows code, so it runs and is tested on Linux. |
| `GnuBoxShare` (net10.0-windows, WinForms) | the window and tray (`MainForm.cs`), speed graph (`SpeedGraph.cs`), requests pane (`RequestsPanel.cs`), single instance (`Program.cs`), Bluetooth (`BluetoothTransport.cs`: Winsock `AF_BTH` RFCOMM socket + `WSASetService` SDP record, P/Invoke to `ws2_32`). |
| `GnuBoxShare.TestServer` (net10.0 console) | the core over TCP for tests: `dotnet run -- 127.0.0.1:7777 -v -r` (`-r` prints every finished request); `../windows/test/phone_sim.py` plays the phone. |

For tests the window itself can serve a simulated phone over TCP instead of Bluetooth:
`GNUBOX_TEST_TCP=127.0.0.1:7777` (and `GNUBOX_TEST_REWRITE=10.99.0.50=127.0.0.1`).

## Tested

- Core on Linux with `../windows/test/phone_sim.py` (Symbian-like PPP, real Linux TCP/IP via
  TUN): the same tests as the Python version, all passed (5 MB down/up identical, 10 parallel
  downloads, DNS A/AAAA/NXDOMAIN, refused port, 40 kB/s throttled link, hang-up and
  reconnect).
- Window under Wine (1.1), against the simulated phone at 40 kB/s: the graph, peaks and hover;
  the requests list with a 1 MB download, HTTP keep-alive (3 requests on one connection,
  one chunked), HEAD, 501, POST upload in progress, HTTPS with the server name
  (`secure.example.org`), raw TCP; the details pane. Requests recorded correctly also by the
  test server: DNS A / IPv6 / not found / cached, refused port.
- 1.2 diagnostics against the simulated phone at 40 kB/s: a normal download is reported as
  link-limited (round trip 855 ms against a 76 ms minimum, window never full); a download with
  a tiny receive window is reported as window-limited (window full 100%, round trip at its
  minimum). "Bluetooth busy" can't be checked there (the test link never blocks the send).
- 1.1 also fixes connections the server closed first staying in the list (and the
  connection count) until a 10-minute idle timeout (fixed in the Python version too).
- **Not yet tested on Windows: the window, the tray and the real Bluetooth with the 9300.**
  The Bluetooth calls and structure sizes are the same as in the Python version, which works.

## Troubleshooting

- *"Bluetooth socket: error ..." in the log:* Bluetooth is off or there is no adapter.
- *The phone never connects:* check GnuBox's *Device* (this PC) and *Settings > Other
  device: PC*; unpair and pair again.
- *Connected but no pages:* tick *Log every connection* and look for `TCP open ...` lines.
