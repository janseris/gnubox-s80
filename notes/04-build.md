# Building on Linux (no Windows)

## Setup [verified 2026-10-19, Ubuntu 24.04, gcc 13, perl 5.38]
1. `setup_eka1_toolchain.sh <dir with gcc-539-2aeh-source.tar.bz2 + S60_SDK_2_1_NET.zip> ~/sym`
   builds GnuPoc (commit d3ddaf58), Symbian GCC 2.9-psion-98r2 and the EKA1 tools, then
   installs the S60 2.1 SDK.
2. `setup_s80_sdk.sh S80_DP_2_0_SDK.zip ~/sym` unshields the S80 SDK and reuses the
   S60 2.1 tools.
3. `export EPOCROOT=~/sym/s80_20/ PATH=~/sym/wrap:$PATH`, then
   `bldmake bldfiles && abld build armi urel`.
4. `sis/build_sis.sh` (needs `~/sym/gcc539/bin` on PATH for makesis).

Packages actually needed (checked on 2026-10-19):
- `build-essential` (gcc/g++/make): builds GCC 2.9 and the EKA1 tools, and runs abld.
- `flex`, `bison`: the GCC 2.9 tarball ships only `.l`/`.y` sources for binutils/ld/gas.
- `zlib1g-dev`: makesis links libz.
- `perl`, `patch`, `git` (clone gnupoc), `unzip`.
- `cabextract`: gnupoc's `unpack21.sh` (S60 SDK).
- `unshield`: `setup_s80_sdk.sh` (S80 SDK InstallShield cabs).
- Not needed: `libncurses-dev` (no tool links it), and `p7zip-full` (only for the split
  `.7z` form of the S60 SDK; the `.zip` was used).
- On a fresh container, run `apt-get update` first.

## Gotchas hit
- SDK examples have **CRLF** line ends, and rcomp fails with "Unknown character 0xd".
  Run `sed -i 's/\r$//'` on them.
- **Case sensitivity**: bld.inf/mmp names, `#include` names and `LIBRARY x.LIB` must
  match the lowercased SDK. The GnuPoc install lowercases headers and libs.
- `petran` and `makesis` live in `~/sym/gcc539/bin`, not in the wrapper dir.
- GCC 2.9: `TDes::AppendNum(TUint32)` is ambiguous, so use `AppendNum((TUint)x, EDecimal)`.
  The `TRAP(err, { ...; ...; })` form works as long as there are no top-level commas.
- No writable static data in apps. Keep scratch buffers in `Dll::Tls()`. `static const
  T* const arr[]` is fine. Check with `petran -v file.app`, which should show Data
  Size 0.
- Generated header from a shell heredoc: write `\t` as a real tab, not `\\t`.
- `abld build` prints harmless perl "uninitialized value" warnings; filter them out.

## Outputs
- `epoc32/release/armi/urel/gnubox.app`, `epoc32/data/z/system/apps/gnubox/gnubox.{rsc,aif}`
- The app imports only stock S80 DLLs: apparc, bafl, bluetooth, btextnotifiers,
  btregistry, commdb, cone, efsrv, eik*, esock, estor, euser, insock.
