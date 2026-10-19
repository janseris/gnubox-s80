# Nokia 9300 / Series 80 platform facts

## Device
- Nokia 9300: Symbian OS 7.0s, Series 80 Developer Platform 2.0, TI OMAP 1510 at 150 MHz,
  640×200 inner screen. Same platform as 9300i and 9500. **[docs]**
- No Wi-Fi on the 9300; the 9300i and 9500 have 802.11b. **[docs]**
- Bluetooth **1.1** on the 9300 RAE-6 **[device: user]**. Max 723 kbit/s one way (DH5 packets), 433 kbit/s both ways; less over RFCOMM + PPP. Profiles in the user guide: Generic Access, Serial Port, Dial-Up Networking
  (phone acts as the modem), GOEP, Object Push, File Transfer, Hands-free, SIM Access.
  **There is no PAN or LAN Access profile**, so PPP over a serial link is the only way to
  get IP over Bluetooth. **[docs]**
- "IP passthrough" (use the PC's Internet) exists natively, but **only over the DKU-2 USB
  cable** (Control panel > Cable setup) and it needs Nokia's Windows-only software. **[docs]**
- Series 80 platform UID for .sis files: `0x101F8ED2` "Series80ProductID". **[verified, SDK example]**
- App icons (AIF): 25×20 and 64×50, 16-bit, each with a mask. **[verified, parsed stock AIFs]**
- Documents folder visible to the user: `C:\Documents\`. **[verified, emulator tree]**
- Apps are DLLs on EKA1, so they **may not have writable static data** (petran
  rejects it). **[verified]**

## Networks (Czechia)
- All 3G networks are shut down (Vodafone 3/2021, O2 and T-Mobile 11/2021). **[docs]**
- 2G remains. ČTÚ requires O2 to run it at least until 2028 (voice/SMS obligation,
  unless 2G users fall below 5%); a general switch-off is expected around mid-2028. **[docs]**

## CommDb (Symbian 7.0s)
- The modem table is `ModemBearer` (renamed from `Modem` in 7.0s). Each IAP has its own
  bearer: `IAPBearer` + `IAPBearerType`. **[verified, cdbcols.h]**
- The 9500 emulator CommDb (`epoc32/wins/c/ced_npacket.cfg`) contains: ModemBearer 1 "CSD"
  (CSD.agt, PPP, DATAPORT::0, PHONETSY, 115200), ModemBearer 2 "GPRS", Network 1
  "Internet", Location 1 "Mobile", DialOutISP 1 "CSD", IAP 2 "GSM Data". The GlobalSettings
  have `LocationForDataAndFax=1`, `DefaultNetwork=1`,
  `BearerAvailabilityCheckTSY=phonetsy`. **[verified, SDK file; real phone may differ]**
- `CCommsDbTableView::InsertCopyRecord()` exists in 7.0s and copies the current
  record. **[verified, commdb.h]**
- Bluetooth default serial port: `CBTRegistry::SetDefaultCommPort(TBTCommPortSettings)` in
  `btregistry.lib`; `TBTCommPortSettings(name, UUID, port, security, BDAddr)` in
  `btdefcommport.h`. **[verified, SDK headers]**
- Paired devices are stored in CommDb `BTDeviceTable`: `DeviceAddress` (8-bit text,
  normally the 6 raw bytes), `DeviceDefaultName` / `DeviceFriendlyName` (long text), and
  `DeviceLinkKey`. This table has no `Name` column. **[verified, cdbcols.h]**
- The 9300's Bluetooth settings have pair but no "connect" for other phones. **[device]**
