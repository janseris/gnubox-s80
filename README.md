# GnuBox for Series 80

Internet over Bluetooth for the **Nokia 9300 / 9300i / 9500** (Series 80, Symbian OS 7.0s).
The internet comes from an **Android phone** running a Bluetooth dial-up app (BlueDUN++),
or from a **Linux PC** running `pppd`. No GSM data or GPRS is used.

GnuBox is a new Series 80 port of the idea behind [gnubox](https://github.com/LukeUsher/gnubox)
(Mal Minhas, 2002; 6600 version by [Mika Raento](https://mikie.iki.fi/symbian/bt-ap.html)).
No GnuBox build for Series 80 existed before.

**Status:** working on a real Nokia 9300 with an Android phone running BlueDUN++. The
browser loads pages over HTTP and HTTPS, without asking for an access point.

Download the ready-made package: [`release/gnubox-2.6.sis`](release/gnubox-2.6.sis)

## How it works

```
 Browser / Mail / Java on the 9300
        │  access point "GnuBox BT" (default connection, no prompt)
 CSD.agt (NetDial) in direct mode        ← empty phone number = no GSM call
        │  ModemBearer "GnuBox BT": BTCOMM::0
 BTCOMM.CSY ── SDP lookup on the chosen device ── RFCOMM ── Bluetooth
        │
 Android: BlueDUN++ (Dial-up Networking, UUID 0x1103)   – login script AT / ATD*99# / CONNECT
 or PC:   BlueZ Serial Port (UUID 0x1101) + pppd + NAT
```

GnuBox only writes system settings and then keeps the connection up. Specifically:

- **The "GnuBox BT" access point.** It writes three CommDb records: an IAP, a DialOutISP
  with an empty phone number (which makes NetDial skip dialling and start PPP over the
  port) and a ModemBearer on `BTCOMM::0`. The idle timeouts are set to *never*. The
  phone's own GPRS and data-call settings are not touched.
- **Bluetooth target.** `CBTRegistry::SetDefaultCommPort` points `BTCOMM::0` at the
  chosen device's Dial-up Networking or Serial Port service.
- **Default connection.** The **Default** menu sets ConnectionPreferences rank 1 to the
  access point you choose (GnuBox BT, USB IP passthrough, GPRS...) with *DoNotPrompt*,
  so apps whose access point is "ask"/"default" connect without a prompt. The Nokia
  selection dialog hides data-call access points without a phone number, so this is
  how the browser gets to use GnuBox BT.
- **The connection.** GnuBox shows the progress stages in plain words, checks that the
  internet works with a DNS lookup, reconnects automatically after a drop, and keeps
  running until you press **Disconnect**.

Details, with the Symbian source references, are in [`notes/02-mechanism.md`](notes/02-mechanism.md).

## Use

1. **Android:** install BlueDUN++ (`com.bluedunz`, the version for Android 12+), start it,
   and pair the phone with the 9300 (Control panel → Bluetooth on the 9300).
   **PC instead (not tested end to end yet):** run `sudo host/gnubox-host.py` (needs
   `bluez ppp python3-dbus python3-gi` and `nft` or `iptables`), then pair the PC with
   the 9300 using a PIN.
2. Install `gnubox.sis` on the 9300 and start **GnuBox**.
3. **Device** → choose the phone or PC from the paired devices. GnuBox remembers it.
4. **Settings:** set *Other device* to *Android phone (BlueDUN)* or *PC (pppd)*.
5. **Default** menu: choose **GnuBox BT (Bluetooth)**. Apps then connect without asking.
   To use the USB cable (IP passthrough) or GPRS instead, choose that entry in the same
   menu. *Ask every time* brings back the phone's own list.
6. **Connect.** The status line shows *CONNECTED since hh:mm*, and the browser, mail and
   so on now go through Bluetooth.

Tips:

- **Speed:** turn on Settings → *Bigger TCP window (faster downloads)*, then restart
  the 9300. The default 8 KB window limits each download to about 26 kB/s over
  Bluetooth. Also try *Compression (may be faster)*, and a different "BT Queue Length"
  in BlueDUN. To measure, time a download of known size in the browser
  (KB × 8 ÷ seconds = kbit/s). The 9300 has Bluetooth 1.1, so a few hundred kbit/s is the limit.
- Don't save "GnuBox BT" in Control panel. It insists on a phone number, which would
  turn it into a real GSM call. **Connect** repairs the access point anyway.
- Changing access point priorities in Control panel can reset the default connection to
  "ask". Choose the connection again in the **Default** menu (or press **Connect**), then
  check **Details** for *connects without asking*.
- **Exit** leaves the connection up. Open GnuBox again and press **Disconnect** to stop it.
- The log is at `C:\Documents\GnuBox\gnubox-log.txt`. **Settings → Save diagnostics**
  writes `commdb.txt` (passwords and Bluetooth keys masked).

## Build (Linux, no Windows needed)

Tested on Ubuntu 24.04 (gcc 13, perl 5.38). These are exactly the inputs and tools the
build used.

**Inputs (not included: Nokia/Symbian files)**

| Input | Used for |
|---|---|
| `S80_DP_2_0_SDK.zip` (Series 80 DP 2.0 SDK) | headers and libraries GnuBox is built against (`EPOCROOT`) |
| `S60_SDK_2_1_NET.zip` (S60 2nd Ed. FP1 SDK) | only its build scripts (bldmake/abld/makmake…), copied into the S80 SDK |
| `gcc-539-2aeh-source.tar.bz2` (Symbian GCC 2.9-psion-98r2) | the ARM compiler, assembler, linker, dlltool |
| [gnupoc-package](https://github.com/mstorsjo/gnupoc-package) (commit d3ddaf58) | builds GCC and the EKA1 tools, unpacks and fixes the SDKs |
| `tools/*.patch` | bmconv build fix; perl fixes for the SDK scripts |

**Ubuntu packages:** `build-essential` (gcc, g++, make), `flex`, `bison` (binutils'
lexers and parsers have no pre-generated sources), `zlib1g-dev` (makesis), `perl`,
`patch`, `git`, `unzip`, `cabextract` (unpacking the S60 SDK), `unshield` (unpacking the
S80 SDK installer).

**Symbian tools that run during the build** (all from the steps above):
`arm-epoc-pe-gcc`/`cpp`/`as`/`ld`/`dlltool`/`ar`/`objcopy`, `petran`, `rcomp` (resource
file), `bmconv` + `aiftool` (icons/AIF), `makesis` (`.sis`), and the SDK perl scripts
(`bldmake`, `abld`, `makmake`, `makedef`, `epocrc`, `epocaif`).

```sh
sudo apt-get update
sudo apt-get install build-essential flex bison zlib1g-dev perl patch git unzip cabextract unshield

tools/setup_eka1_toolchain.sh <dir with gcc-539-2aeh-source.tar.bz2 and S60_SDK_2_1_NET.zip> ~/sym
tools/setup_s80_sdk.sh S80_DP_2_0_SDK.zip ~/sym

export EPOCROOT=~/sym/s80_20/ PATH=~/sym/wrap:$PATH
cd group && bldmake bldfiles && abld build armi urel
cd ../sis && PATH=~/sym/gcc539/bin:$PATH ./build_sis.sh     # -> sis/gnubox.sis
```

Build gotchas (CRLF files, case sensitivity, no writable static data, GCC 2.9 quirks)
are listed in [`notes/04-build.md`](notes/04-build.md).

## Repository layout

| Path | Contents |
|------|----------|
| `src/`, `inc/` | app source: UI, connection manager, CommDb records, Bluetooth |
| `group/` | `bld.inf`, `gnubox.mmp`, resource file |
| `aif/` | icons (25×20 and 64×50) and AIF resource |
| `sis/` | package file and SIS build script |
| `host/` | PC side for Linux: BlueZ Profile API script, classic rfcomm script |
| `tools/` | toolchain/SDK setup scripts and patches for Linux |
| `notes/` | everything found along the way: platform facts, mechanism, hosts, build, open questions, test log |
| `release/` | prebuilt `.sis` |

## License

GPL v2 or later ([LICENSE](LICENSE)), like the original gnubox.
`inc/gnubox_cols.h` contains only the CommDb column-name strings from the SDK's
`cdbcols.h`; GnuBox uses them for its diagnostics dump.
