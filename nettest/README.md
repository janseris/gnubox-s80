# NetTest for Series 80

A small test app for the Nokia 9300: how fast files arrive from the PC over the connection that
is up (GnuBox Bluetooth, or the USB cable), for different numbers of parallel connections and
TCP receive windows. Made to tune Maps' 3D download settings and to see what the Bluetooth path
carries; it downloads only from your own PC.

- **Test files:** `localserver\nettest\f000.bin` ... `f117.bin` - random data with the sizes of
  the 118 files of a Maps 3D download (meshes and textures, 2.5 MB; 10-41 KB each, median 22 KB),
  served by `ota_server.js` at `http://192.168.137.1:8000/nettest/fNNN.bin`.
- **Runs** (Full test: all 118 files each; Quick test: the first 40): 2 connections with an 8 /
  16 / 32 / 64 KB window, 3 connections with 16 / 32 KB, 1 connection with 16 / 64 KB. Each
  connection asks for files one after another (HTTP/1.1 keep-alive); the window is set with
  `SetOpt(KSoTcpRecvWinSize)` before `Connect`, as Maps and browser9300 do. 3 s pause between
  runs.
- **Results** per run: files, KB, seconds, kB/s, median time per file (request to last byte),
  median time to the first byte, slowest connection setup, errors. On the screen, appended to
  `C:\Data\NetTest\results.txt`, and POSTed to `http://192.168.137.1:8000/results?name=nettest`
  (saved as `localserver\uploads\<time>_nettest.txt`).
- Every request carries `X-NetTest-Run: <run> P<connections> W<window>`, so GnuBox Share's
  `requests-<date>.jsonl` (with its per-second `speed-<date>.csv`) can be matched to the runs.
- A connection without data for 30 s, or with an error, is reconnected and the file asked again
  (at most 20 errors a run).

UID `0x0A5D10A4`, version 1.1, `release/nettest-1.1.sis`. 1.1: the status lines update once a
second without redrawing the log below them (1.0 redrew the whole screen). Build: like GnuBox
(`notes/04-build.md`): `bldmake bldfiles && abld build armi urel` in `group/`, then `makesis` on
`sis/nettest.pkg` with the built `nettest.app`, `.rsc` and `.aif` next to it. Checked: builds with
no writable static data; the server side (keep-alive, Content-Length, the results POST) tested
against a copy of `ota_server.js`. Not yet run on the phone.
