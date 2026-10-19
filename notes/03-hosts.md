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

## Windows 11 [docs/unverified]
- Windows can offer an incoming Bluetooth COM port (More Bluetooth settings > COM Ports),
  but has no usable PPP server on it. Incoming direct-cable RAS connections no longer
  work in modern Windows.
- WSL2: the stock kernel can't use the laptop's Bluetooth. **[unverified]**
- Workable on that laptop: an Ubuntu live USB ("Try Ubuntu") running gnubox-host.py, or a
  Linux VM with the Bluetooth USB adapter passed through.

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
