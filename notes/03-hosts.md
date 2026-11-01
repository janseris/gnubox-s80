# The other end of the Bluetooth link

## Linux (GnuBox mode: PC) [script logic verified; no live BT test yet]
- `host/gnubox-host.py` registers an SPP server through the BlueZ `ProfileManager1`
  (`RegisterProfile`, Role=server, Channel=N). BlueZ creates the SDP record itself, and
  `NewConnection` hands over an fd, which becomes pppd's stdin/stdout (`notty`).
- The pppd options parse correctly, checked with two pppd instances on a socketpair.
  The container has no `/dev/ppp`, so no link came up there.
- The link uses 10.66.0.1 (PC) and 10.66.0.2 (phone), with NAT through nft (or iptables
  as fallback) and ms-dns set to the host's first non-loopback resolver (or 1.1.1.1).
- `host/gnubox-host.sh` is the classic alternative: `sdptool add SP` + `rfcomm watch` +
  pppd. It needs the deprecated BlueZ tools and `bluetoothd -C`.
- The 9300 uses legacy PIN pairing: in bluetoothctl run `agent on`,
  `discoverable on`, `pairable on`, then `trust`.

## Windows 11 (GnuBox mode: PC) [works with the 9300: 31 kB/s, test log #13]
- Windows has no usable PPP server on a Bluetooth COM port (incoming direct-cable RAS is gone),
  so `host/windows/gnubox_winshare.py` does it in user space. Transport: **direct Bluetooth**
  (default) = Win32 Winsock `AF_BTH` RFCOMM listening socket + SDP record for the Serial Port
  class via `WSASetService` (ctypes), no COM port (the console script can still use an
  *incoming* Bluetooth COM port; the GUI dropped it, never needed),
  PPP server (LCP, PAP accept, IPCP 10.64.64.2 / DNS 10.64.64.1, rejects VJ and CCP), and a
  user-space NAT (TCP termination + real sockets, UDP, DNS via Windows' resolver, ping).
  Python 3, no admin rights. GUI: `GnuBox Share.bat` (`gnubox_share.pyw`, Tk).
  Setup: `host/windows/README.md`.
- C# / .NET 10 port: `host/windows-cs` (WinForms, follows Windows' dark mode, tray icon, one
  instance, start with Windows via HKCU Run). Same PPP/NAT logic, Bluetooth via P/Invoke to
  ws2_32. `GnuBoxShare.exe` is 0.5 MB and needs the .NET 10 Desktop Runtime. Core tested on
  Linux against the same phone simulator; window checked under Wine.
- Uses the phone's ACCM (Symbian asks for 0): control bytes are not escaped, ~12% more speed
  on binary data. 37.9 kB/s on a 40 kB/s test link.
- Older notes: WSL2 can't use the laptop's Bluetooth; an Ubuntu live USB with gnubox-host.py
  also works.

## Android (GnuBox mode: Dial-up server) [docs; not yet tested with the 9300]
- Google Play: **BlueDUN++** (`com.bluedunz`, about $3.99), updated 2025-08-19. This is the
  "plus" version, made for Android 12+. There is no trial, and the license expires one year
  after purchase. **[docs, Play listing 2026-10-19]**
  The older `com.bluedunx` "BlueDUN+" listing exists, but it is the pre-Android-12 build.
- What it does: it emulates the phone's modem and serves the Bluetooth **DUN** service,
  sharing Wi-Fi or mobile data. No root is needed. The developer tested it with Windows
  PCs, car head units, TomTom and the BB PlayBook. Nokia/Symbian is not listed. **[docs]**
- Known problems: some Motorola, Sony Xperia and HTC phones don't work (firmware). Some
  clients need "BT Queue Length" reduced from 10 to 2. Disable Smart Lock or mark the
  device as trusted. Some reviews report license-check trouble after buying. **[docs]**
- Setup per the listing: the client's number, account and DNS settings can be ignored. So
  GnuBox's `ATD*99#` plus empty PPP auth should be acceptable. **[unverified]**
- What GnuBox expects: a DUN SDP record (UUID 0x1103), `AT` → `OK`, `ATD*99#` →
  `CONNECT`, then PPP with IP and DNS assigned.

### Android: BlueDUN++ stops 60 s after screen-off (FIXED, tested 2026-10-19)
BlueDUN++ 6.8 runs BlueDUNService as a plain background service, so Android 8+ stops it
("Stopping service due to app idle") 60 s after the screen goes off, and refuses to let
it restart. Fix, once, from a PC with USB debugging:
`adb shell dumpsys deviceidle whitelist +com.bluedunz` (check: `adb shell dumpsys
deviceidle whitelist | findstr bluedun`). Redo after reinstalling BlueDUN++.
`host/adb-bluedun-test.bat` collects the evidence (service state, standby bucket,
allowlist, logcat at the drop).
