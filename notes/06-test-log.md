# Test log

Copy this block for each test.

```
Date:
GnuBox version:            (About)
Mode:                      PC / Dial-up server
Other side:                Linux distro + BlueZ version / Android model + version + app
Phone profile:             General / Offline
Steps done:
Last progress stages seen: (from the GnuBox screen)
Result:
Files:                     C:\Documents\GnuBox\gnubox-log.txt, commdb.txt, host --debug output
Notes:
```

## Entries

### 2026-10-19 #1: Android (BlueDUN++), GnuBox 1.1
- Pairing the 9300 with the Android phone in the 9300's Bluetooth settings: **OK**.
- The 9300's Bluetooth settings only offer "pair". There is no "connect", which is
  expected: the connection is made later by the GnuBox access point (BTCOMM).
- GnuBox "Select device": **the Android phone could not be selected**. Probable cause:
  the S80 device picker only lists devices found by a fresh inquiry, and Android is
  discoverable only while its Bluetooth settings screen is open. Not confirmed.
- Fix in 1.2: if the picker returns nothing, GnuBox lists the paired devices from
  CommDb `BTDeviceTable` (also in the menu as "Choose from paired devices"). Highlight one
  and press the selection key.

### 2026-10-19 #2: Android (BlueDUN++), GnuBox 1.2: **SUCCESS** [device]
- The device was chosen (via the paired-devices list or a fresh search), the access
  point was created, and Test ran.
- Screen output: "Connection is up. Resolving example.com", "example.com =
  104.20.32.154, connecting to port 80", "Server answered: HTTP/1.1 200 OK",
  "SUCCESS - Internet over Bluetooth works."
- This confirms on the device:
  - CSD.agt direct mode works with BTCOMM::0 on the 9300.
  - The registry default comm port (DUN UUID 0x1103) is honoured without a device prompt.
  - The NetDial login script (AT / ATD*99# / CONNECT) works with BlueDUN++.
  - PPP gets an IP address and DNS from BlueDUN.
  - TCP and DNS go through the RConnection.
- Next: use the "GnuBox BT" access point from other apps (browser, mail, Java).

### 2026-10-19 #3: GnuBox 1.2 usability [device]
- The highlight in the paired-device list is not visible (CEikTextListBox has no
  visible cursor in our view). In 1.3 it is replaced by a real **Device** menu (first
  menu title; the "Select device" button opens it). It lists paired devices from
  BTDeviceTable, ticks the current one, and keeps "Search for a new device..." for
  the system dialog.
- The system dialog (Select device) on S80 is a search/pair dialog, not a picker.
- **The browser does not list "GnuBox BT"**, even though Test works with it.
  1.3 adds checks under Show settings: the IAP list without hidden records, CommDb's CSD
  bearer filter, and the connection preferences. It also adds "Make GnuBox BT the
  preferred access point" (ConnectionPreferences rank 1) and adoption of an access
  point named "GnuBox BT" created in Control panel.

### 2026-10-19 #4: GnuBox 1.3 [device]
- Control panel lists "GnuBox BT" with a **D** (data call) icon. Opening Edit and
  leaving requires a phone number, so the Nokia UI treats an empty number as
  invalid. The browser probably filters it out for the same reason (unconfirmed).
  **Don't save it in Control panel**: a number would make NetDial dial a real
  GSM data call (dial-up state machine instead of direct).
- The link dropped by itself: stage 4250 (hanging up) → 8000 PPP link closed **-33
  timed out** → 4500 closed → 0. Cause: the ModemBearer timeouts copied from the CSD
  record (LastSocketActivityTimeout=300 s), which is the idle timer.
- Fixes in 2.0: all three Last*Timeout values = -1 (never). A connection manager with
  auto-reconnect (10 s, then 30 s) runs until Disconnect. A Connect/Disconnect toggle
  button and a status header. Connect always rewrites the records (repairing a
  Control-panel edit). "Use GnuBox BT for all apps" sets ConnectionPreferences rank 1
  to GnuBox BT with DoNotPrompt, and remembers and restores the previous value.
  Attaching to a connection left running uses EnumerateConnections + Attach.

### 2026-10-19 #5: Details output, connection preferences [device]
- Outgoing ConnectionPreferences on the user's 9300: IAP 14 "IP Passthrough" and
  IAP 5 "INTERNET" (the GPRS/2G access point). Rank 1 is IAP 14.
- So the 9300 ships with an IP passthrough IAP, which has no phone number and is a
  valid AP for the Nokia UI. Next: dump it (Settings > Save diagnostics) and copy its
  record shape for GnuBox BT, so the browser lists it. Still to ask: does the browser
  list "IP Passthrough"?

### 2026-10-19 #6: browser prompt [device]
- The browser always shows the system "select access point" prompt. **IP Passthrough
  is listed there, GnuBox BT (data call, "D" icon, no number) is not.**
- After "Use GnuBox BT for all apps" (2.1), Details still showed IAP 14 and IAP 5 at
  the top. The rank-1 update did not take effect, and the cause is unknown: the
  Nokia factory records may be read-only, or CommDb's ValidateIapL may have refused
  it. 2.2 shows the error and the resulting preference list on screen.
- Plan: make GnuBox BT the same *type* as IP Passthrough. 2.2's Details prints, for
  every IAP, the service and bearer records it points to (agent, CSY, port, TSY,
  number, LDD/PDD), so we can copy IP Passthrough's shape.

### 2026-10-19 #7: Details output (2.2) [device]
- BTCOMM::0 → the Android phone, service "GnuBox", security none, UUID 0x1103.
- GnuBox BT = IAP 16 → DialOutISP 4 (empty number, login script) + ModemBearer 4
  (CSD.agt, PPP, BTCOMM::0, PHONETSY, Rate=18 = 115200, CommRole 0). Not hidden, and in
  CommDb's data-call (CSD) list.
- Other IAPs: 1 "T-Mobile WAP" and 5 "INTERNET" are OutgoingGPRS + ModemBearer 2 "GPRS"
  (weighting 3 for INTERNET). 14 "ip passthrough" = **LANService 4 + LANBearer 2
  "RndisBearer"** (rndisagt.agt, rndisnif, LDD/PDD "not used"), i.e. USB RNDIS, not
  a serial modem. So "make it look like IP passthrough" is not possible:
  NetDial/CSD.agt needs DialOutISP + ModemBearer.
- ConnectionPreferences: rank 1 = IAP 16 (GnuBox BT), rank 2 = IAP 14, both
  **DialogPref 1 = PROMPT**, bearers 0x17. The user had set GnuBox BT to priority 1 in
  Control panel, which most likely rewrote the dialog preference to PROMPT. The prompt
  dialog filters out data-call APs without a number, so the browser never sees it.
- Next: rank 1 with DialogPref 3 (DoNotPrompt), re-applied by Connect / the toggle; 2.3
  prints it in words. Then retest the browser.

### 2026-10-19 #9: speed reference [device: user]
- USB cable, IP passthrough (RNDIS): max download **107 kB/s (~856 kbit/s)** on this
  9300. So the phone's own CPU and TCP stack manage at least that much; at that rate
  they are not the bottleneck.
- Bluetooth 1.1 can't reach that: the radio's best case is 723 kbit/s ≈ 90 kB/s in one
  direction. With RFCOMM + PPP overhead, expect roughly **25–55 kB/s** over GnuBox BT.
  To be measured.

### 2026-10-19 #10: real speed over GnuBox BT [device: user]
- Map app, one HTTPS request per tile (ssladaptor reuses connections): average
  **~20 kB/s**, peak 30 kB/s, ~26 kB/s sustained for some requests. Speed rises gradually.
- Explanation: Symbian's default TCP receive window is **8192 bytes** (in_sock.h
  KSoTcpRecvWinSize), and per-connection throughput is at most window / RTT. 26 kB/s
  means RTT ≈ 0.3 s (Bluetooth + PPP + Android NAT). Over USB the RTT is lower, so 8 KB
  still gave 107 kB/s. The gradual rise is TCP slow start on the server side.
- TCPIP6.PRT reads `tcp_recv_buf`, `tcp_send_buf`, `tcp_initial_cwnd`, `tcp_mss`, and
  more, from tcpip.ini (strings in the SDK binary). 2.5 adds Settings > "Bigger TCP
  window": C:\System\Data\tcpip.ini with [tcp] tcp_recv_buf=32768, tcp_send_buf=16384,
  with backup and restore. Needs a phone restart. The section name "[tcp]" is assumed
  (Symbian tcpip6 convention); if it's wrong, the setting does nothing.

### 2026-10-19 #11: Default connection menu (2.6) [not yet tested on device]
- The user wants no connection prompt ever, while still using USB (IP passthrough) and
  GPRS. The new "Default" menu lists every visible IAP with a type (Bluetooth / GPRS /
  USB / data call) plus "Ask every time". Picking one sets ConnectionPreferences rank 1
  = that IAP, DialogPref DoNotPrompt, bearer set CSD|WCDMA|LAN|Virtual (all types are
  allowed). "Ask" keeps the IAP and sets DialogPref Prompt.
- Replaces Settings > "Use GnuBox BT for all apps". Connect still re-applies GnuBox BT
  as default when that is the chosen default (Control panel edits can reset it to
  prompt).

## #12 BlueDUN++ stops ~70 s after the Android screen turns off
- Report: with the screen off, BlueDUN++ stops sharing after about 1 min 10 s. With the
  screen on it keeps going (BlueDUN shows "discoverable for 120 s", which is unrelated).
- Diagnosis (not confirmed yet): Android power management stops the app or its
  Bluetooth socket. ~1 min after screen-off is the typical timing of OEM "clean up on
  lock" app killers (Xiaomi/HyperOS, Huawei, Oppo/Realme, Samsung sleeping apps), not of
  AOSP Doze (which needs much longer and lets foreground services run).
- Suggested fixes: Battery = Unrestricted / no restrictions; Autostart on; lock the app
  in Recents; Samsung "Never sleeping apps"; if needed via adb:
  `dumpsys deviceidle whitelist +com.bluedunz`,
  `cmd appops set com.bluedunz RUN_ANY_IN_BACKGROUND allow`. Fallback: charger +
  Developer options "Stay awake".
- To tell app-killed from link-dropped: GnuBox log on the 9300 (stage/error at the drop)
  and `adb logcat | grep -i -E "bluedun|killing"` on Android.
- Follow-up: no battery option for the app in Settings, battery saver off, all
  permissions on; another app's GPS-sharing service survives screen-off. So it is not an
  OEM killer but most likely Android's background-service limit (8.0+): a service of an
  app without a foreground-service notification is stopped ("Stopping service due to
  app idle") ~60 s after its uid leaves the foreground. Screen off = top activity becomes
  TOP_SLEEPING = background, hence ~70 s. Fix: put com.bluedunz on the device-idle
  allowlist (`adb shell dumpsys deviceidle whitelist +com.bluedunz`), which exempts it
  from the limit; or keep the screen on (charger + Stay awake).
- adb report (2026-10-19, Motorola/MediaTek, BlueDUN++ 6.8 targetSdk 37): CONFIRMED.
  BlueDUNService runs as a plain background service (startForegroundCount=0, no
  isForeground). Screen off 22:28:04 -> `Stopping service due to app idle:
  com.bluedunz/.BlueDUNService` at 22:29:04 (exactly 60 s); its restart attempt is
  refused ("Background start not allowed", "startForegroundService() not allowed").
  GPS NMEA Tether is a real foreground service (isForeground=true, types=0x18
  connectedDevice|location), notification exists but is hidden. Neither app was on
  the deviceidle allowlist. Fix being tested: `adb shell dumpsys deviceidle whitelist
  +com.bluedunz` (allowlisted uids are exempt from the background-service idle stop).
- RESULT: after `dumpsys deviceidle whitelist +com.bluedunz` the connection stayed up
  >4 min with the screen off (map downloads continued). FIXED. Survives reboot; must be
  redone after reinstalling BlueDUN++ or clearing its data.

## #13 Windows PC sharing over Bluetooth (2026-11-01): WORKS
- GnuBox Share (host/windows, direct Bluetooth: Win32 AF_BTH socket + SDP Serial Port record, no
  COM port) on the user's Windows PC; 9300 GnuBox in *Other device: PC (pppd)* mode: connected
  and working.
- First attempt failed because GnuBox was still in Android mode (Dial-up Networking UUID +
  AT login script): the Device menu and Settings > Other device are separate settings.
- Added: the PC itself is reachable from the phone as 10.64.64.1 and 192.168.137.1 (both
  mapped to 127.0.0.1), so localserver apps work over Bluetooth too.
- Speed (2026-11-01, FileFetch 1.0 (a small HTTP download test app, since deleted) over GnuBox Share, random data from the FileFetch server on
  the same PC): 1 MB at 31 kB/s (peak 32), 256 KB at 28 kB/s (connection setup weighs more on a
  small file). BlueDUN++ on Android gave ~20 kB/s average (26-30 peak) for map tiles, so the
  Windows route is about 1.5x faster. With PPP/TCP/IP overhead the Bluetooth link carries
  ~33 kB/s, close to what the 9300's Bluetooth 1.1 RFCOMM manages.

## #14 GnuBox 2.7: new icon (2026-11-01)
- New AIF icon (laptop, Nokia 9300, modern phone, radio waves; generated by
  aif/make_gnubox_icon.py, made in another chat). Masks are black where the icon is drawn, like
  the user's other apps (maps9300, instagram9300, mp4player9300); the old GnuBox mask was the
  other way round. To check on the device: icon on the Desk and in the task list.

## #15 GnuBox 2.8: Bluetooth error texts (2026-11-01)
- After a whole day of use over GnuBox Share (Python), Connect failed every time:
  stages 1000, 2000, 3100, 4250, 4500, 100, then *Access point chosen* -6003, stage 2000
  (*Opening Bluetooth link*), *Could not connect -6003*. -6003 = KHCIErrorBase (-6000) - 0x03
  *Hardware failure*: the phone's own Bluetooth controller, before it even pages the PC.
  Turning Bluetooth off and on on the 9300 (a few times) fixed it; no phone restart needed.
- 2.8 names the Bluetooth chip (HCI, -6000 - code), L2CAP (-6300...) and RFCOMM (-6350...)
  errors in plain words with a hint (e.g. "-6003 (BT hardware failure - turn Bluetooth off
  and on)"). -6305 was wrongly shown as "service not found"; per bt_sock.h it is
  KErrHCILinkDisconnection, now "BT: link lost". Error text buffer 64 -> 96 characters.

## #16 Speed: what limits it? GnuBox Share 1.2 diagnostics (2026-11-02) [to test on the device]
- Candidates: (1) the phone's TCP receive window, 8 KB by default: at most 8 KB per round
  trip per connection; 31 kB/s would mean a ~0.26 s round trip; (2) the Bluetooth link itself
  (radio max ~90 kB/s one way, RFCOMM frame size, the phone's stack); (3) data queued on the PC
  adding delay; (4) compression (little gain: most traffic is HTTPS or images).
- The SDK's TCPIP6.PRT (WINS build) has the ini section `tcp` followed by `tcp_mss`,
  `tcp_recv_buf`, `tcp_send_buf`, ... (also `tcp_winscale`), so GnuBox's *Bigger TCP window*
  (C:\System\Data\tcpip.ini, [tcp] tcp_recv_buf=32768) uses the right names. Not yet
  confirmed on the device that the stack reads the C: copy.
- GnuBox Share 1.2 shows the window the phone advertises (so the setting can be verified
  directly), the round trip and its minimum, Bluetooth send busy %, the RFCOMM frame size, and
  a verdict. The simulated phone asks for Stac LZS data compression and VJ header compression.
- Seen in the simulation: when the link is the limit, our 32 KB in-flight cap per connection
  puts ~0.8 s of data in front of the link (round trip 855 ms vs 76 ms). With the phone's 8 KB
  window this doesn't arise; with 32 KB it would add latency for other connections.
- Test plan: (a) as now: download the 1 MB file, note speed, window, round trip, verdict;
  (b) GnuBox Settings > Bigger TCP window, restart the phone, connect, same download;
  (c) *Test: short send queue* on, same download; (d) two downloads at once.
- Correction (2026-11-03): the ~35 kB/s ceiling is not "Bluetooth's limit". The same ~35 kB/s
  came from two different hosts (Android + BlueDUN++, Windows + GnuBox Share), ~39% of the
  Bluetooth 1.x radio's 723 kbit/s. The likely cause is the phone's BTCOMM.CSY (Symbian source,
  2009 version): one socket read of at most 1 KB outstanding, a 4 KB circular buffer, reading
  stops at 3 KB until PPP drains it to 2 KB. With 666-byte RFCOMM frames (the phone's L2CAP 672
  minus headers), 35 kB/s = one read cycle per ~19 ms. GnuBox's ModemBearer rate (115200) is
  not enforced. Unproven until a different serial driver is tried.

## #17 Test session: alone vs. CPU-heavy app, compression offer (2026-11-03, 22:21-23:03) [device: user]
GnuBox Share 1.3 logs (speed per second, requests), maps9300 1.47 log, browser9300 log.

| Phase | Link avg (active s) | Phone window full | Round trip |
|---|---|---|---|
| maps9300.sis self-update (16 KB window) | 34-38 kB/s | 100% | 120-490 ms |
| Browser 1 MB alone | 34.0 kB/s | 13% | 180-980 ms |
| Maps panning (aerial, 2 TLS connections) | 25.5 kB/s (17.6 overall) | 100% | 290-740 ms |
| Browser 1 MB + Maps panning | 24.7 kB/s (14.0 overall) | 73% | up to 2.8 s |
| Browser 1 MB, GnuBox Compression on (refused) | 32.5 kB/s | 14% | 390-1050 ms |

- **Alone, every app gets ~34 kB/s** (window rarely the limit for the browser's 48 KB; Maps'
  16 KB window was full but still reached the same speed).
- **Under Maps the phone, not the link, is the limit:** the phone's window was full 100% of the
  time, i.e. Maps didn't read fast enough. Its log: disk-cache saves average 1371 ms with 34
  waiting, tiles take 1.5-3.6 s each at 6-10 kB/s per thread, decoding only 46-156 ms. With the
  browser downloading at the same time, both apps together got only ~14 kB/s and the round trip
  reached 2.8 s: CPU / file-server contention on the phone (Bluetooth itself takes ~45% CPU).
- **Compression:** with GnuBox Settings > Compression on, the phone offers **Stac LZS** (CCP)
  and **Van Jacobson** header compression. GnuBox Share refuses both, so no change. Little to
  gain: browser9300 already gets HTML/JS/CSS gzip'd ("gzip 1"), almost everything is HTTPS, and
  pictures (Google's GIFs: 0.2-0.9 MB each) and tiles don't compress. VJ would save ~3%.
- The Google start page: 16 s of scripts on the phone's CPU before the first layout.
- Conclusion: a faster serial driver (GnuBox CSY) is the only lever left for single downloads,
  and saving CPU in the Bluetooth path would also help CPU-heavy apps. PPP compression is not
  worth building for today's traffic.

## #18 Browser downloads: no idle seconds at the question (2026-11-04, 00:01-00:18) [device: user]
GnuBox Share 1.3 logs (speed per second, requests), browser9300 0.44-0.48 logs. The same files
from localserver over GnuBox BT; browser9300's TCP window 48 KB, Maps 9300's 16 KB.

| Download | Browser | Whole download | Seconds at 0 kB/s | Steady |
|---|---|---|---|---|
| browser9300.sis 327 KB, 00:01 | 0.44 (stops reading at the "download it?" question) | 10.0 s | 2 (window 5 KB, then 0) | 34-37 kB/s |
| maps9300.sis 264 KB, 00:04 | 0.45 (reads on into RAM at the question) | 7.7 s | 2 (after the question: "Replace the file?") | 34-37 kB/s |
| browser9300.sis 327 KB, 00:17 | 0.47 (no second question in the temporary folder) | 7.3 s by the request record, ~9.2 s of data by the per-second rows | **0** | **34-37 kB/s every second** |
| maps9300.sis 264 KB (Maps 9300 self-update, no question) | - | 5.9 s | 0 | 34-37 kB/s |

- **Not a new top speed of the link:** the steady rate is the same ~35 kB/s ceiling as in #17
  (BTCOMM, see the correction there). New: **the browser loses no time to its own dialogs any
  more** - every second of the 0.47 download carried data, as Maps 9300's self-update does.
  Before, the transfer stood still while the question was up (0.44) or during the second
  "Replace the file?" question (0.45).
- The two times differ: the request record says 327 579 B in 7314 ms (44.8 kB/s), but the
  per-second rows show data from 00:17:40 to 00:17:49 (341 KB with headers and PPP, ~35.5 kB/s).
  The record's time probably ends when GnuBox Share has handed the last bytes on, not when the
  phone has them. Compare downloads by the per-second rows.
- Browser changes behind it: 0.45 reads the body into RAM while the question is up (512 KB at
  most); 0.47 replaces a file in the temporary folder without asking; 0.45 also redraws the
  progress text every 300 ms instead of every 2 KB (~17 times a second at 35 kB/s); 0.48 says
  "Writing the file" during the final flash write (the progress looked stuck a few KB short).

## #19 NetTest 1.0 Quick test: windows and parallel connections (2026-11-04, 00:22-00:27) [device: user]
NetTest (new, `nettest/`) downloads random test files from the PC's localserver (no TLS, no
internet server, no decoding) with the sizes of a Maps 3D download (40 of the 118 files, 755 KB a
run, 12-41 KB each), once per setting. NetTest's kB/s counts from the first connect to the last
byte; "link" is GnuBox Share's per-second average while data flowed. GnuBox Share saw the phone
advertise exactly the windows NetTest set (8160 / 16320 / 32640 / 65280 B).

| Run | Connections | Window | NetTest | Link | Round trip avg | Window full |
|---|---|---|---|---|---|---|
| 1 | 2 | 8 KB | 29.3 kB/s | 32.9 kB/s | 358 ms | 100% |
| 2 | 2 | 16 KB | 30.9 | 33.8 | 530 ms | 100% |
| 3 | 2 | 32 KB | 31.1 | 33.8 | 612 ms | 0% |
| 4 | 2 | 64 KB | 31.1 | 34.2 | 624 ms | 0% |
| 5 | 3 | 16 KB | 30.7 | 34.4 | 842 ms | 100% |
| 6 | 3 | 32 KB | 30.7 | 34.4 | 1145 ms | 0% |
| 7 | 1 | 16 KB | 28.5 | 30.0 | 379 ms | 100% |
| 8 | 1 | 64 KB | 28.6 | 30.4 | 146 ms | 0% |

- **~34 kB/s whatever the TCP settings.** With 2-3 connections every window from 8 to 64 KB gives
  the same link speed; even with the 8 KB windows "full" all the time the speed didn't drop, so
  something below TCP limits first (the BTCOMM explanation, #16). No TLS, server or app here.
- Bigger windows / more connections only add delay: 2 connections 358 ms (8 KB) -> 624 ms (64 KB);
  3 connections with 32 KB 1.1 s - anything else loading at the time waits that long.
- 1 connection loses ~10% to the pause between files (here short: the local server answers in
  ~100 ms, first byte median 109 ms).
- For Maps over Bluetooth: 2 connections, 8-16 KB. Maps reached only 23-25 kB/s with 2 x 8 KB in
  the real 3D run (#17 session), 33 here: the difference is Mapy.com over HTTPS (server time and
  TLS on the phone); 16 KB or 3 connections can hide a slower first byte, more only adds delay.
- The "slow start" seen in apps is not the link: GnuBox Share sends at full speed from the first
  second (its TCP has no slow start; first second ~30 kB/s with 2 connections). It is each new
  download's fixed costs: DNS, TCP connect (~0.1-0.3 s), and for HTTPS the TLS handshake
  (1.7-11 s over Bluetooth, Maps benchmarks). NetTest 1.2 adds a small-file test (2-50 KB, kept
  vs. new connections) to measure that.
