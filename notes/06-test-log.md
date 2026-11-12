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

## #20 NetTest 1.2 small files: kept-open vs. new connections (2026-11-04, 00:37-00:41) [device: user]
Plain HTTP from the PC, 40 files a run (8 each of 2 / 5 / 10 / 20 / 50 KB, 702 KB), 16 KB window.
Median time per file on the phone: from the request, or with a new connection per file from its
connect.

| Run | Connections | Per file | Overall | 2 KB | 5 KB | 10 KB | 20 KB | 50 KB |
|---|---|---|---|---|---|---|---|---|
| 1 | 1 | kept open | 27.7 kB/s | 125 ms | 203 | 390 | 671 | 1578 |
| 2 | 1 | new for each file | 14.3 kB/s | 734 ms | 812 | 1000 | 1312 | 2203 |
| 3 | 2 | kept open | 30.7 kB/s | 562 ms | 656 | 453 | 1406 | 2406 |
| 4 | 2 | new for each file | 29.0 kB/s | 984 ms | 390 | 546 | 1484 | 2328 |
| 5 | 3 | kept open | 30.1 kB/s | 750 ms | 765 | 953 | 2015 | 3859 |

- A new TCP connection costs ~0.6 s over GnuBox Bluetooth (not the 0.1-0.3 s assumed in #19);
  with one connection, small files go half as fast. HTTPS adds its TLS handshake on top.
- Kept open, a file takes ~0.1 s (one round trip) + its size at ~32 kB/s.
- 2 connections kept open give the most in total and hide most of a new connection's cost; 3 only
  make each file wait longer. For what the user waits for, fewer connections finish each item
  sooner.

## #21 The upload stall: the phone's whole uplink freezes (2026-11-04, 00:57-01:05) [device: user]
NetTest 1.3's upload test (40 KB POSTs, 16 KB windows as Maps) with GnuBox Share 1.4 logging every
TCP segment. Reproduces Maps 9300's report (big POSTs over GnuBox, `symbian-s80v2-dev-tips-and-
tricks/networking.md` section 11).

- POST 1 on a fresh connection: 40 KB in ~1.3 s, every segment acked at once.
- POST 2 on the same connection (after our 172-byte answer): the phone sent **exactly 16 384 bytes**
  (its send window), GnuBox Share acked every byte at once with a 65 535-byte window, the phone
  **never retransmitted** (it knew all had arrived) - and then sent nothing more.
- **Nothing at all came from the phone for the next 5 minutes**: no retransmissions, no new
  connections (NetTest's later POSTs on fresh connections never reached the PC: no SYN), no PPP
  echo; 0 bytes either way in the per-second log. The Bluetooth link stayed up, and when the user
  disconnected in GnuBox the phone did send its PPP hang-up (146 B).
- So it isn't one stuck TCP connection: **the phone's outgoing IP path froze**, below TCP: most
  likely the PPP NIF / BTCOMM.CSY send path (BTCOMM: 1 KB send buffer, one write at a time) never
  signalled "can send" again, and the IP stack waits for ever. Maps 9300's workaround (a fresh
  connection per big upload) worked for 1.5 MB on 3 Nov, so the trigger is more specific; the
  difference here: the phone was receiving at the same moment (our 172-byte answer, then two ACKs
  per segment while it uploaded).
- On our side GnuBox Share acks every phone segment twice (at once, then a window update when the
  data has gone to the server). A receive arriving while BTCOMM writes is a classic way to wedge
  a simple serial driver. GnuBox Share 1.5 adds *Test: one ACK per segment* (half the packets to
  the phone during uploads; checked with the simulated phone: 4 x 40 KB up, 1 MB down identical).
  Next: the upload test with it on; and, if possible, the same test over Android BlueDUN++ (a
  normal TCP stack with delayed ACKs) to see whether the freeze depends on the host.

## #22 Upload freeze, second run: not the double ACK, not connection reuse (2026-11-04, 01:14-01:19) [device: user]
GnuBox Share 1.5 with *Test: one ACK per segment* on, segments logged; NetTest 1.3's upload test.

- The freeze came **during the first POST on a fresh connection**: the phone sent 27.8 KB in ~1 s
  (~28 kB/s), every segment acked at once (one ACK each, 64 KB window), then nothing more for 5+
  minutes, no retransmission, no new connection. All 8 POSTs counted as stalled.
- So neither the double ACK nor a reused connection is the trigger. What both freezes share: the
  phone uploading steadily with up to its 16 KB send window in flight, i.e. its PPP / BTCOMM send
  queue full. With #21 (the phone could still send its PPP hang-up later) the likely mechanism:
  the PPP NIF flowed the IP stack off when its queue to BTCOMM was full, and the "can send again"
  never came. Maps' fresh-connection uploads (1.5 MB on 3 Nov) got through by luck or timing.
- PC-side workaround to test: GnuBox Share 1.6 *Test: small upload window* advertises only 2720 B
  (2 segments) to the phone, so it never has more in flight and its queue never fills. Checked with
  the simulated phone: uploads 22-26 kB/s (31 without), downloads unchanged (1 MB identical).

## #23 GnuBox 2.9: own Bluetooth serial driver (GNUBTCOM.CSY) - to test [device: user]

Built, not yet run on the phone. `btcsy/` is Symbian's BTCOMM ported to 7.0s (see its README); GnuBox
*Settings > Bluetooth driver* switches the access point between `BTCOMM::0` and `GNUBTCOM::0`, the
second with Symbian's buffers (1 KB reads/writes, 4 KB receive buffer) or big ones (8 KB / 4 KB / 32 KB,
from `C:\System\Data\gnubtcom.ini`). Plan, PC = GnuBox Share (C#), NetTest Quick each time:
1. *the phone's* - baseline (~34 kB/s expected).
2. *GnuBox's (test)* - must connect and match the baseline; if it doesn't connect, the port is wrong.
3. *GnuBox's, big buffers* - the real question: faster than ~35 kB/s?
Untested parts of the port: the SDP query structures (from Symbian^3's `sdpkey.h`), the registry lookup.

### #23, first run (2026-11-11 ~23:40) [device: user]
*GnuBox's (test)* (Symbian's buffer sizes): the phone **crashed after stage 2750** (KCsdScannedScript; next
would be 3000, the agent opening the port). So GNUBTCOM.CSY was loaded and something in opening or
connecting the port brought down the comms server. No log in that build. GnuBox 2.9.1 + NetTest 1.4: the
driver logs every function to `C:\Logs\GnuBtCom\gnubtcom.txt`; NetTest sends it to the PC after the reboot.
Suspects: the ESOCK session made in a helper thread and shared (EKA1 handle ownership), the SDP query
structures, the registry lookup from the comms thread.

### #23, second run with the log (2026-11-11 23:54) [device: user]
The phone again restarted (lines disappear from the screen, then reboot). GNUBTCOM's log
(`uploads/2026-11-11T22-58-19-858Z_gnubtcom.txt`): module loaded, port 0 opened (buffers 1024/1024/4096),
SetRole, SetConfig; C32 loads the module a second time and drops the copy (factory created and destroyed,
normal); then GetRole, GetConfig, SetConfig, SetReceiveBufferLength, ResetBuffers, the first StartWrite
(NetDial) -> Idle::Write -> `TBTPortStateIdle::SockServConnect` = last line. So the restart happens while
creating/starting the helper thread that connects to ESOCK (Symbian^3 design). GnuBox 2.9.2: connects to
ESOCK directly instead (7.0s has ESOCK in its own thread, and this runs on an asynchronous RComm::Write).

### #23, third run: GnuBox 2.9.2 connects (2026-11-12 00:14-00:21) [device: user]
Phone clock ~51 s behind the PC. GNUBTCOM log (`uploads/2026-11-11T23-20-08-159Z_gnubtcom.txt`):
- 1st try: ESOCK connect OK, L2CAP loaded, SDP found the service and channel, RFCOMM connect **-34**
  (KErrCouldNotConnect) - at that moment GnuBox Share had just dropped the previous link and was only
  re-listening (PC 00:14:57), so most likely a race, not the driver.
- 2nd try: **RFCOMM connected, PPP up** (PC: "connected, frame size 666", LCP, IP up within 1 s), data in
  both directions through the driver for 2 min 20 s, then the phone hung up; the driver closed cleanly.
- But no app traffic: every DNS lookup was answered by GnuBox Share (example.com, A + "no IPv6") and the
  phone asked again every ~4 s (AAAA, AAAA, A x4), then gave up (-5120). **The same then happened on the
  phone's own BTCOMM** (00:18, 00:20) while TCP to an IP address worked (NetTest sent the log, 200 OK).
  DNS had worked minutes before (00:11). So the phone's resolver no longer accepts answers since the
  GNUBTCOM session - phone state, not the link. To check: restart the phone, BTCOMM + a web page; then
  GNUBTCOM + NetTest Quick (by IP, no DNS).

### #23, fourth run: 2.9.2 with the log on (2026-11-12 00:24-00:31) [device: user]
- First Connect -34 again (GnuBox Share was listening this time), second Connect works.
- NetTest Quick: every file -25, ~4 kB/s, phone window full 100%, round trips 2-5 s; requests "no response
  / connection closed" in between. The driver log shows why: 50-100 log lines a second, ~10 lines per
  1 KB read or write -> the logging itself is the limit. The log has no errors.
- Google in the browser **works** on GNUBTCOM (slowly); DNS did not break this time.
GnuBox 2.9.3: log only while *Settings > GnuBox driver log* is on (checked once when the driver loads;
off = no logger calls); SDP closed before the RFCOMM connect and up to 3 connect tries (for the -34).

### #23, fifth run: 2.9.3, GNUBTCOM with Symbian's buffers, log off (2026-11-12 00:44-00:50) [device: user]
NetTest 1.4 Quick, same 40 files (755 KB a run) as #19 (phone's BTCOMM):

| Run | Conn. | Window | GNUBTCOM kB/s | BTCOMM #19 kB/s |
|---|---|---|---|---|
| 1 | 2 | 8 KB | 28.5 | 29.3 |
| 2 | 2 | 16 KB | 26.9 | 30.9 |
| 3 | 2 | 32 KB | 30.1 | 31.1 |
| 4 | 2 | 64 KB | 31.1 | 31.1 |
| 5 | 3 | 16 KB | 30.0 | 30.7 |
| 6 | 3 | 32 KB | 30.9 | 30.7 |
| 7 | 1 | 16 KB | 26.9 | 28.5 |
| 8 | 1 | 64 KB | 28.1 | 28.6 |

0 errors. **The port works and matches the phone's own driver** (within a few %, runs 2 and 7 lower).
Then *big buffers* (8192 / 4096 / 32768): Connect **stuck at stage 2750** (port open, no Bluetooth
connection reached GnuBox Share). Cause unknown yet: needs the driver log.
After ~3 min stuck at 2750 GnuBox stopped responding and could not be closed -> phone restart. That
fits ESOCK left in *exclusive mode* for the driver's session (BTCOMM's "locker",
`RSocketServ::SetExclusiveMode`): then no other ESOCK client gets served. BTCOMM leaves the lock on when
a locked action fails (e.g. opening the RFCOMM socket -> Error() without StopLocker). 2.9.4: unlock on
every move to the error state. The actual cause of the stall still needs the log.

### #23, sixth run: 2.9.3, GNUBTCOM **big buffers** (8192 / 4096 / 32768), log off (2026-11-12 01:05-01:09) [device: user]
Connected at the first try after a phone restart (the earlier stall was probably a Connect within the
driver's 6-second close delay after a Disconnect, which reuses the old port). NetTest 1.4 Quick:

| Run | Conn. | Window | big buffers | Symbian's buffers (5th run) | BTCOMM (#19) |
|---|---|---|---|---|---|
| 1 | 2 | 8 KB | 29.5 | 28.5 | 29.3 |
| 2 | 2 | 16 KB | 30.4 | 26.9 | 30.9 |
| 3 | 2 | 32 KB | 30.8 | 30.1 | 31.1 |
| 4 | 2 | 64 KB | 31.0 | 31.1 | 31.1 |
| 5 | 3 | 16 KB | 30.4 | 30.0 | 30.7 |
| 6 | 3 | 32 KB | 30.8 | 30.9 | 30.7 |
| 7 | 1 | 16 KB | 28.7 | 26.9 | 28.5 |
| 8 | 1 | 64 KB | 28.7 | 28.1 | 28.6 |

GnuBox Share, seconds with data: 36.3 kB/s average (big) vs 35.2 (Symbian's buffers); Bluetooth busy 1%.
**BTCOMM's buffer sizes are not the limit** (1 KB reads / 4 KB buffer vs 8 KB / 32 KB: the same). The
suspect from #16 is ruled out. What's left below TCP: RFCOMM frames of 666 B (~53 frames/s at 35 kB/s,
each needing credit), PPP/HDLC on the phone's CPU, the radio link. GnuBox 2.9.5 adds *big frames*: the
driver asks for 990-byte RFCOMM frames (L2CAP inbound MTU and RFCOMM max MTU set before connecting);
GnuBox Share's "frame size" line shows what was agreed.

### #23, seventh run: 2.9.5 *big frames* (2026-11-12 01:17-01:21) [device: user]
NetTest Quick 30.0 / 30.6 / 31.1 / 30.9 / 30.8 / 31.0 / 28.3 / 29.4 kB/s (runs 1-8), 0 errors; GnuBox Share
35.5 kB/s average in seconds with data. **Frame size still 666** on the PC side, so the request for 990-byte
frames didn't take (the phone's SetOpt results are only in the driver log, not taken this time). Browser
download also ~30 kB/s.
Next, all in one go (GnuBox 2.9.6 + GnuBox Share 1.7): the driver log of a *big frames* connect (did the
phone accept the frame size request?), GnuBox Share logs the phone's LCP options (is ACCM 0, or does all
binary data carry ~12.5% escapes?) and accepts frames up to 1011; *big frames* now also asks the link
manager for all ACL packet types (3/5-slot packets); *no credit flow* turns RFCOMM credit-based flow
control off.

### #23, driver log of a *big frames* connect (2.9.5, 2026-11-12 01:27-01:29) [device: user]
`frame size: ... set L2CAP inbound MTU -5, RFCOMM max MTU -6`: the L2CAP option is not supported on an
RFCOMM socket (KErrNotSupported), and the RFCOMM maximum MTU was refused with KErrArgument - the TInt
option was the wrong size (the get failed too). So nothing was asked for; that's why the frame stayed 666.
Also: the connect retries ran in the same second and all failed with -34 (one Connect needed a manual
second try). 2.9.7: the MTU as a TUint16 (logs the value before and after), retries 2 s apart.

### #23, 2.9.7 *big frames* (990-byte frames + all packet types) with the log (2026-11-12 01:47) [device: user]
Driver log: RFCOMM max MTU was 0 (= default), set to 990 OK, reads back 990; link manager packet-type
ioctl (all DM/DH 1-5) OK. GnuBox Share 1.7 (accepts up to 1011): **frame size still 666** - so the
size is capped below the driver's setting (most likely the phone's L2CAP MTU of 672 for the RFCOMM
channel; the L2CAP option is not supported on an RFCOMM socket). And the link didn't work: the phone sent an
LCP Configure-Request every second (GnuBox Share answered each, "the phone restarted the link") for 30+ s;
the driver log shows reads completing and data handed to PPP, so what arrived was unusable or lost. Same at
01:33 (first 2.9.7 connect) and with *no credit flow* (which also set both). The 2.9.5-driver connects at
01:37/01:40 (neither setting effective) worked. GnuBox Share 1.7: ACCM 0 - escaping costs <1%, not a factor.
No credit flow: the RFCOMM connection never reached the PC. 2.9.8 splits the two: *all packet types* alone,
*big frames only* alone.

### #23, 2.9.8 *all packet types* alone (2026-11-12 01:58-02:02) [device: user]
Connects and works (so 990-byte frames were what broke the 2.9.7 link). NetTest Quick 29.1 / 26.8 / 23.1 /
29.9 / 30.2 / 27.2 / 25.0 / 22.9 kB/s, 0 errors; GnuBox Share 33.5 kB/s in seconds with data. **No gain**
(if anything noisier/lower); the link manager accepted the request, so 3/5-slot packets were allowed and
the speed is not set by the packet types the phone allows.
Next (2.9.9): *big socket buffers* (32 KB KSORecvBuf/KSOSendBuf on the RFCOMM socket - does the phone grant
more RFCOMM credits?), and with the log on, the RFCOMM receive/transmit credits once a second.
