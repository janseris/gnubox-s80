# GnuBox Share (C# / .NET 10)

The Windows PC's internet for the Nokia 9300 over Bluetooth, as one small `GnuBoxShare.exe`.
The same thing as the Python version in [`../windows`](../windows/README.md): the program is a
Bluetooth *Serial Port* service (no COM port), runs a PPP server and a user-space NAT. The
9300 side is GnuBox in **Settings > Other device: PC (pppd)** mode.

Extras over the Python version: follows Windows' **light/dark mode**, sits in the **tray**
(the dot on the icon shows the state: grey stopped, orange waiting, green sharing, red
problem), only one copy runs (starting it again shows the window), no Python needed.

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
| `GnuBoxShare.Core` (net10.0) | PPP (`Ppp.cs`: HDLC, LCP, PAP, IPCP), NAT (`Nat.cs`: TCP, UDP, DNS, ping), the event loop and reconnect (`Engine.cs`), helpers (`Basics.cs`). No Windows code, so it runs and is tested on Linux. |
| `GnuBoxShare` (net10.0-windows, WinForms) | the window and tray (`MainForm.cs`), single instance (`Program.cs`), Bluetooth (`BluetoothTransport.cs`: Winsock `AF_BTH` RFCOMM socket + `WSASetService` SDP record, P/Invoke to `ws2_32`). |
| `GnuBoxShare.TestServer` (net10.0 console) | the core over TCP for tests: `dotnet run -- 127.0.0.1:7777 -v`; `../windows/test/phone_sim.py` plays the phone. |

## Tested

- Core on Linux with `../windows/test/phone_sim.py` (Symbian-like PPP, real Linux TCP/IP via
  TUN): the same tests as the Python version, all passed (5 MB down/up identical, 10 parallel
  downloads, DNS A/AAAA/NXDOMAIN, refused port, 40 kB/s throttled link, hang-up and
  reconnect).
- Window under Wine: opens, layout correct.
- **Not yet tested on Windows: the window, the tray and the real Bluetooth with the 9300.**
  The Bluetooth calls and structure sizes are the same as in the Python version, which works.

## Troubleshooting

- *"Bluetooth socket: error ..." in the log:* Bluetooth is off or there is no adapter.
- *The phone never connects:* check GnuBox's *Device* (this PC) and *Settings > Other
  device: PC*; unpair and pair again.
- *Connected but no pages:* tick *Log every connection* and look for `TCP open ...` lines.
