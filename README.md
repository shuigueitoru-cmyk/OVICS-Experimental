# OVICS Experimental

**Open World Interface Computer System**

OVICS is an experimental 32-bit x86 operating system built from scratch without the Linux kernel. It includes a command-line interface, a graphical desktop, a RAM file system, an RTL8139-based TCP/IP stack, and the experimental **OVICS Horizon** web browser.

## Latest experimental version

**OVICS 0.2.2 Experimental** integrates Horizon into the desktop. Horizon can fetch basic HTTP pages and render a limited subset of HTML. Wikipedia article **summaries** are available through an optional Python HTTPS relay running on the host computer.

This is experimental software, not a production operating system.

## Getting started (Windows)

### Requirements

- Windows 10 or 11
- QEMU with `qemu-system-i386` available on PATH (for example through MSYS2)
- Python 3, only for the optional Wikipedia relay

### Download and run

1. Download and extract the **OVICS 0.2.2 Experimental** package once it is published.
2. Open the extracted directory containing `ovics.elf` and `Start-OVICS.ps1`.
3. Run `Start-OVICS.ps1` from PowerShell. If Windows opens the script in an editor, open PowerShell in that directory and run `./Start-OVICS.ps1`.
4. At the `OVICS>` prompt, enter `desktop`.
5. Double-click the **Horizon** desktop icon.

Alternatively, from a shell with QEMU available, run this in the directory containing `ovics.elf`:

```sh
qemu-system-i386 -m 128 -vga std -kernel ovics.elf -nic user,model=rtl8139
```

This package is for QEMU experimentation, **not installation onto a physical computer**.

### Wikipedia summary relay (optional)

1. On the Windows host, run `Start-Wikipedia-Relay.cmd` from the extracted package (requires Python 3).
2. Leave the relay window open.
3. Start OVICS, enter `desktop`, and open Horizon.
4. Click **Wiki relay** or enter a URL such as:

```text
http://10.0.2.2:8088/wiki/Computer
```

The host-side relay fetches English Wikipedia article summaries using HTTPS and serves simplified HTML to OVICS over HTTP. **OVICS itself does not implement HTTPS/TLS.** The relay is restricted to Wikipedia summaries and is not a general HTTPS proxy.

### Horizon controls

- Enter an `http://` URL and press Enter or click **Go**.
- Use **Up** and **Down** to scroll.
- **Ctrl+U** clears the address input.
- **Esc** returns to the OVICS command-line interface.
- **Ctrl+Alt+G** releases the QEMU mouse grab.

## Current limitations

- No native HTTPS/TLS
- No JavaScript or complete CSS engine
- Limited HTML support; modern websites may fail
- HTTP chunked transfer encoding is not supported
- HTTP response body size is limited (up to 8192 bytes)
- Wikipedia relay provides article summaries, not full articles
- Physical hardware support is not established

See the version-specific `BUILD.md`, `REPORT.md`, and `LIMITATIONS.md` in the release package for implementation details and test evidence.

## License

MIT License. See [LICENSE](LICENSE).

Please verify that any bundled third-party code and assets are compatible with the repository license before distributing release packages.
