# GNUBTCOM.CSY - GnuBox's Bluetooth serial port driver

Symbian's own Bluetooth serial driver **BTCOMM** (the CSY that turns an RFCOMM socket into the
serial port `BTCOMM::0`), taken from the Symbian Foundation sources (`sf/os/bt/bluetooth/btcomm`,
Eclipse Public License v1.0) and ported to Symbian OS 7.0s / Series 80 (EKA1, GCC 2.9).

Why: over Bluetooth the 9300 tops out at about 34-35 kB/s whatever the TCP window or the number
of connections (`notes/06-test-log.md`), well under what the radio can carry. The prime suspect is
BTCOMM itself: one socket read of at most 1 KB at a time, a 4 KB receive buffer that stops reading
the socket at 3 KB, 1 KB writes. The phone's BTCOMM is in ROM and can't be changed; this copy can.

The port is `GNUBTCOM::0`. It finds its PC exactly like `BTCOMM::0`: the Bluetooth registry's
default device for port 0 (what GnuBox writes when you choose the device), then an SDP query for
the service, then an RFCOMM connection.

## Using it

GnuBox 2.9 installs `gnubtcom.csy` to `\System\Libs\`. In GnuBox *Settings*:

- *Bluetooth driver: the phone's* - BTCOMM, as before (default).
- *Bluetooth driver: GnuBox's (test)* - GNUBTCOM with Symbian's buffer sizes. Should behave
  exactly like BTCOMM; this checks the port itself.
- *Bluetooth driver: GnuBox's, big buffers (test)* - GNUBTCOM with 8 KB socket reads, 4 KB
  writes, a 32 KB receive buffer.

The choice is written into the *GnuBox BT* access point (ModemBearer CSYName/PortName) at the
next Connect.

The buffer sizes come from the optional text file `C:\System\Data\gnubtcom.ini`:
`<read> <write> <buffer>` in bytes, e.g. `8192 4096 32768` (GnuBox writes or deletes it). Read
when the port opens. Limits: each at least 128, at most 256 KB, buffer at least twice the read.
The socket is read again until `buffer - read` bytes wait for PPP, and restarts at half the buffer.

## Changes from Symbian's BTCOMM

- `gnubtport.h`: logging compiled out, `NONSHARABLE_CLASS` for GCC 2.9.
- No platform security (`PortPlatSecCapability` removed; 7.0s has none).
- Default device: `CBTRegistry::GetDefaultCommPort()` (7.0s) instead of `RBTRegServ` /
  `RBTCommPortSettings`.
- No security settings on the outgoing RFCOMM connection (7.0s' `TRfcommSockAddr` has none); the
  PC may still require authentication/encryption of the paired link.
- `RThread::Rename` instead of `User::RenameThread`; an `E32Dll` entry point.
- Names: `GNUBTCOM`, panic category `GnuBtCom`; UID3 0x0A5D10A5.
- Buffer sizes configurable (above); Symbian's values are the default.

`sdpkey.h` and `sdpconsts.h` come from `sf/os/bt/bluetoothmgmt/btcommon/inc` (EPL). The test
code (`tsrc`) and the WINSCW/EABI DEF files are not included.

## Build

    cd group
    bldmake bldfiles && abld build armi urel

Builds with no writable static data (20 KB). Then `sis/build_sis.sh` in the GnuBox folder packs
it with GnuBox.

## License

Eclipse Public License v1.0 (<http://www.eclipse.org/legal/epl-v10.html>), as the original; the
notices are in each source file. It is a separate program from GnuBox (GPL), installed next to it.
