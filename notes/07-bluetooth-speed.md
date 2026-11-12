# What limits Bluetooth on the 9300 (results, Nov 2026)

Over GnuBox's Bluetooth link the Nokia 9300 gets **~34-36 kB/s of payload** (NetTest: 28-31 kB/s
counting the gaps between files), with any TCP window, any number of connections and either host. This
note collects what was measured to find out why. Details and raw numbers: `06-test-log.md` #9, #16-#23.

## The measurements

All with NetTest Quick (`nettest/`: 40 files, 755 KB per run, 8 runs of different TCP windows and
parallel connections, plain HTTP from the PC) and GnuBox Share's per-second log (Windows host).

| Phone side | NetTest kB/s | Link kB/s (seconds with data) |
|---|---|---|
| The phone's BTCOMM.CSY | 28.5-31.1 | ~34 |
| GNUBTCOM.CSY (BTCOMM ported, `btcsy/`), Symbian's buffers | 26.9-31.1 | 35.2 |
| GNUBTCOM, 8 KB socket reads, 32 KB receive buffer (8x) | 28.7-31.0 | 36.3 |
| + every ACL packet type allowed (DM/DH 1-5, link manager ioctl OK) | 22.9-30.2 | 33.5 |
| + 990-byte RFCOMM frames asked for (accepted locally; negotiated 666 anyway) | 28.6-31.2 | 35.8 |
| + 32 KB socket buffers | 28.3-30.9 | 35.6 |
| + no credit-based flow control (log: credits 0 / 0, i.e. really off) | 27.4-29.8 | 34.3 |

Earlier, with the phone's BTCOMM:
- TCP window 8-64 KB, 1-3 connections: the same ~34 kB/s; bigger windows only add delay (#19).
- Android + BlueDUN++ as the host (a different radio and stack): Maps' byte counter 34.6-35.7 kB/s,
  the same as Windows + GnuBox Share (34-37) (#16).
- Over the USB cable the same phone does **220 kB/s HTTP** (#9 had 107 kB/s), so its CPU, TCP/IP and
  HTTP are not the limit.

## Ruled out

| Suspect | How |
|---|---|
| TCP window, number of connections | NetTest #19: same speed from 8 to 64 KB, 1-3 connections |
| PPP compression | the phone offers LZS + VJ; pages and tiles don't compress (#17) |
| PPP escaping | the phone asks for ACCM 0x00000000: only 0x7E/0x7D escaped, < 1 % (GnuBox Share 1.7) |
| The phone's CPU / TCP stack | 220 kB/s over USB |
| The PC sending | GnuBox Share's Bluetooth send is busy ~1 % of the time, nothing queued |
| The host's radio / stack | Android + BlueDUN++ and Windows give the same ~35 kB/s |
| BTCOMM's buffers (1 KB reads, 4 KB buffer) | our own copy with 8x the buffers: same |
| RFCOMM credit window | the phone grants at most 7 credits (Symbian source: `KRfcommSAPCBFCBufferMultiple = 7`, buffer = 7 x MTU), measured 0-7 in the log; with credit-based flow control switched off (credits 0/0, buffer 50 frames): same speed |
| Socket buffer sizes | 32 KB: same |
| Allowed ACL packet types | all of DM1-DH5 allowed: same |

## Not changeable from an app or driver

- **The RFCOMM frame size, 666 bytes.** Set by the phone's L2CAP MTU of 672 for the RFCOMM channel. The
  RFCOMM socket accepts a bigger maximum (`KRFCOMMMaximumSupportedMTU`, a TUint16; a TInt gives
  KErrArgument), Windows accepts up to 1011 (`SO_BTH_MTU_MAX`), yet 666 is negotiated; the L2CAP MTU
  option is not supported on an RFCOMM socket (KErrNotSupported).

## What's left

Everything above RFCOMM and the host side have been varied without effect, so the limit is in the
9300 below RFCOMM: L2CAP/HCI in its Bluetooth stack, the link between the CPU and the Bluetooth chip, or
the chip/radio itself. ~35 kB/s is ~280 kbit/s of payload, ~39 % of Bluetooth 1.x's 723 kbit/s. Which of
those it is was not measured (one guess, not checked: the speed of the serial line between the CPU and
the Bluetooth chip). Nothing that runs as an app or a CSY can change it.

## Practical consequences

- Use the phone's own driver (*Bluetooth driver: the phone's*, the default). GNUBTCOM is no faster and
  has an intermittent fault (sometimes the phone's PPP never accepts what arrives, stuck at stage 3500,
  #23).
- For apps: 2 connections, 8-16 KB TCP window, keep connections open (each new one costs ~0.6 s), reuse
  TLS sessions. Bigger windows or more connections only add delay.
- For a fast transfer use the USB cable (~6x).

## GNUBTCOM: what was learned porting BTCOMM to 7.0s

See `btcsy/README.md`. In short: a helper thread to connect to ESOCK restarted the phone (the comms
process); connecting directly from the asynchronous RComm::Write works; BTCOMM's "locker"
(`RSocketServ::SetExclusiveMode`) must always be released or every ESOCK client hangs; logging every
function through RFileLogger limits the driver to ~4 kB/s; the first RFCOMM connect after SDP failed
with -34 until the SDP session was closed first.
