# How GnuBox gets IP over Bluetooth

```
app (browser...) -> IAP "GnuBox BT" -> CSD.agt (NetDial) -> ModemBearer BTCOMM::0
   -> BTCOMM.CSY -> SDP lookup on default device -> RFCOMM -> other side
```

## NetDial direct mode [verified, Symbian source csdagt]
- `Nd_Agent.cpp`: for an outgoing connection, `IsDefaultTelNumL()` reads
  `DialOutISP.DefaultTelNum`. If it is **empty**, the agent uses `CDirCtAgXSM` (direct
  connection) and never dials.
- `ND_DIRCT.CPP` / `ND_DBACC.CPP`: direct mode takes the CSY and port from the
  ModemBearer record (`GetCommPortForDirectConnectL`) and opens it through C32, so the TSY
  is not involved. The comm settings come from the modem record, with
  DCD/DSR/CTS handshaking masked off.
- A login script still runs in direct mode when `UseLoginScript` is TRUE (that is how
  the 6600 GnuBox did the Windows `CLIENT` handshake).
- Progress stages in direct mode: 3000 StartingConnect (port being opened) →
  2650/2750 script → 3100 FinishedConnect → 3500 ConnectionOpen (agent done) →
  7000 LinkLayerOpen (PPP up). There is no 2500 "dialling" stage.

## BTCOMM [verified, Symbian source btcomm/states.cpp]
- For `BTCOMM::n` it reads the registry default service for port n (BDAddr + UUID).
- It runs SDP on that device, looks for the UUID in the ServiceClassIDList, takes the
  RFCOMM channel from the protocol descriptor list, and connects.
- Security comes from the stored settings: `IsSecuritySet()` means authenticate,
  `IsEncryptionSet()` means encrypt.
- So: **UUID 0x1101** (Serial Port) for a PC running pppd, and **UUID 0x1103** (DUN) for
  dial-up modem emulators.

## NetDial login script language [verified, csdagt/script + TS_CsdAgt tests]
- Commands: `SEND`, `WAIT`, `LOOP`, `GOTO`, `EXIT`, `SET`, `DTR`, `RAISE`, `DROP`,
  `READ`, `CHARMAP`.
- Example: `SEND "AT"+<0x0d>`, then `WAIT 5` followed by `{` newline `"OK" label` newline
  `}`. A line `label:` defines a label (forward references are fine), and `!` starts a
  comment. Lines are separated by `\n`. If a WAIT times out, the script falls through to
  the next line.
- `EXIT` alone means success. Error variables: `KErrTimeOut$`, `KErrLoginFail$`,
  `KErrNoModem$`, `KErrModemError$`, `KErrNoAnswer$`, `KErrNoCarrier$`,
  `KErrNoDialTone$`, `KErrBusy$`, `KErrModemInitError$`.
- GnuBox's dial-up script: `AT` (two tries) → `ATD*99#` → wait 45 s for `CONNECT`, with
  separate exits for NO CARRIER / BUSY / ERROR.

## Records GnuBox creates [verified, code]
- ModemBearer "GnuBox BT": a copy of the CSD modem with PortName=`BTCOMM::0`,
  CSYName=`BTCOMM`, TSY=`PHONETSY`, Agent=`CSD.agt`, IfName=`PPP`, 8N1, 115200, no
  handshaking, CommRole DTE.
- DialOutISP "GnuBox BT": DefaultTelNum empty, IP and DNS from the server, PPP user and
  password `gnubox`, no compression, BearerType CSD. In PC mode UseLoginScript is FALSE;
  in dial-up mode it is TRUE with the script above.
- IAP "GnuBox BT" links the above plus the default network and the data location.
- The phone's own records are never changed. This differs from the original gnubox,
  which rewrote the shared modem record.

## History
- The original gnubox (Mal Minhas, 2002, 7650/Symbian 6.1) rewrote the first Modem and
  DialOutISP rows. Its call to `CBTRegistry::SetDefaultCommPort` was **commented out**.
  On the 6600, opening BTCOMM brought up a device picker instead. **[verified, LukeUsher/gnubox]**
- The 6600 version (Mika Raento) looked rows up by name ("Bt") and needed a
  `CLIENT`/`CLIENTSERVER` login script for Windows RAS. **[docs]**
- No Series 80 build of gnubox ever turned up. **[unverified: searched, not found]**

## Series 80 access point UI [verified, strings in SDK WINS binaries]
- `IAPIAP.DLL` (the S80 IAP engine behind Control panel > Internet setup) reads IAP
  records directly with SQL, including `Hidden`, and keeps an order through
  `IAPNetworkWeighting` ("set iap weight").
- `APENGINE.DLL` (Nokia AP engine, used by the SyncML apps) queries `Hidden = 0` and
  orders by `IAPNetworkWeighting`. It also knows the WAPAccessPoint/WAPIPBearer tables.
- Opera (`OPERA.APP`, `oprbridge.dll`) imports AGENTDIALOG/COMMDB and reads
  `IAP\Id`, `IAP\IAPService`, `IAP\IAPServiceType` from the connection.
- The emulator CommDb has IAP weightings of 1 and one ConnectionPreferences row per IAP
  (ranks 1, 2, DialogPref=PROMPT).
- CommDb's `OpenIAPTableViewMatchingBearerSetLC` maps bearers to service types
  (CSD ↔ DialOutISP) and filters on direction from ConnectionPreferences. [Symbian source]
- No GSMBSC.TSY on S80. The emulator has MM.TSY and DMM.TSY, but the device may not, so
  direct mode stays.

## Progress stages seen in practice [verified, nifvar.h]
| Stage | Meaning |
|---|---|
| 1000 / 2000 | choosing / chose the access point |
| 3000 | agent starts the connection (opens BTCOMM::0) |
| 2650 / 2750 | login script running / done (dial-up mode) |
| 3100 | serial link up, PPP starting |
| 3500 | agent finished: connection open |
| 7000 | PPP up (IP configured) |
| 4000 | data temporarily blocked |
| 4250 | starting to close (hang-up) |
| 4500 | agent closed |
| 8000 | PPP link closed (an error code is the reason; -33 = idle timeout) |
| 0 | back to "uninitialised" = fully closed |

## Idle timeouts [verified, cdbcols.h + ced cfg]
- ModemBearer `LastSocketActivityTimeout`, `LastSocketClosedTimeout`,
  `LastSessionClosedTimeout` are in seconds, and -1 (0xFFFFFFFF) means never. The CSD
  record has 300/3/300; the GPRS record has -1/3/-1.
