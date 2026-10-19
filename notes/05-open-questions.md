# Open questions / next steps

1. ~~Does the 9300 accept CSD.agt direct mode with BTCOMM?~~ **Yes (test #2).** It should, since the same
   NetDial ran on the 6600. Untested on Series 80.
2. **Does nifman's bearer availability check (`BearerAvailabilityCheckTSY=phonetsy`)
   block a CSD-type IAP** in Offline profile or with no SIM? If Test fails before stage
   3000, try the normal profile.
3. ~~Does the 9300 BTCOMM accept the registry default without a device prompt?~~ **Yes (test #2).** BTCOMM
   source says yes; check on the device.
4. **BlueDUN on a current Android**: does it still run, which UUID/channel does it
   advertise (check with `sdptool browse <phone>` from Linux), does it accept
   `ATD*99#`, and does it need PPP auth?
5. Does the 9300 browser (Opera) list the "GnuBox BT" IAP, or does it need WAP AP records?
6. Throughput over the Bluetooth 1.1 serial link (RAE-6): expect roughly 200–450 kbit/s. Measure it.
7. If a direct-mode login script fails with BlueDUN: try `ATZ` first, or a longer wait,
   and capture the exchange with Tools > Save CommDb dump plus the log.
8. **Why doesn't Opera list "GnuBox BT"?** Candidates: (a) weighting 0 (fixed in 1.3
   for new records), (b) the UI's list comes from ConnectionPreferences/weight order,
   (c) Nokia's UI hides data-call APs with an empty number, or with an unknown bearer
   record. Next data: the 1.3 Show settings output, plus a CommDb dump after creating a
   normal data-call AP in Control panel for comparison.
9. Does Opera use the ConnectionPreferences default ("Use GnuBox BT for all apps"), or
   only its own AP list? If only its own list, compare a Control-panel data-call AP with
   ours (dump) and look for an IP passthrough-type AP, which has no phone number but is
   still listed.
10. **Speed.** Measure by hand: time a download of a known size in the browser (KB × 8 ÷ s
    = kbit/s). Compare with Settings > Compression on and off (2.4), and BlueDUN's "BT Queue
    Length" at 10 and 2. Expected ceiling: Bluetooth 1.1 serial link (RAE-6), roughly 200–450
    kbit/s; HTTPS pages can be CPU-bound on the 150 MHz OMAP.
