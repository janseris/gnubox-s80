# GnuBox for Series 80: project notes

A running record of what we have found out. Each fact is marked:

- **[verified]**: checked against source code, SDK headers, or by running it
- **[docs]**: from a manual or other documentation
- **[unverified]**: inferred or reported by others, not yet tested
- **[device]**: confirmed on a real Nokia 9300 (fill in when tested)

| File | Contents |
|------|----------|
| [01-platform.md](01-platform.md) | Nokia 9300 / Series 80 facts: OS, Bluetooth, CommDb layout, icons, paths |
| [02-mechanism.md](02-mechanism.md) | How the BT access point works: NetDial direct mode, BTCOMM, login scripts |
| [03-hosts.md](03-hosts.md) | The other end of the link: Linux, Windows 11, Android |
| [04-build.md](04-build.md) | Building on Linux: toolchain, SDK quirks, gotchas hit so far |
| [05-open-questions.md](05-open-questions.md) | Unknowns and what to try next |
| [06-test-log.md](06-test-log.md) | Results of real tests (template plus entries) |
| [sources.md](sources.md) | Links and source files used |
