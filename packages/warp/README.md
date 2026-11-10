# Cloudflare WARP AppImage

Standalone AnyLinux AppImage packaging official Cloudflare WARP client binaries (`warp-svc` + `warp-cli`). Built to run natively on musl-based distributions (such as Alpine Linux 3.20) without host glibc or FUSE dependencies.

## Architecture & musl Compatibility

1. **Self-contained glibc closure**: Bundled via `quick-sharun` (`sharun` + `uruntime` + `dwarfs`). The bundled dynamic linker (`ld-linux-x86-64.so.2`) and exact required libraries (`libtss2-esys`, `libnss3`, `libdbus-1`, `libc`, `libm`, `libgcc_s`) execute seamlessly on any Linux host.
2. **IPC Mechanism**: `warp-cli` communicates with `warp-svc` over a UNIX domain socket at `/run/cloudflare-warp/warp_service` (no system D-Bus daemon required).
3. **Debloated footprint**: Discards the Flutter desktop GUI (`warp-taskbar`, webview, assets), delivering a minimal CLI+daemon footprint.

## Usage

```sh
# Show version
./warp-x86_64.AppImage --version

# Run daemon in foreground (or launch as background job)
./warp-x86_64.AppImage svc &

# Execute CLI commands
./warp-x86_64.AppImage cli --accept-tos status
./warp-x86_64.AppImage cli --accept-tos registration new
./warp-x86_64.AppImage cli --accept-tos connect
./warp-x86_64.AppImage cli --accept-tos settings
./warp-x86_64.AppImage cli --accept-tos disconnect
./warp-x86_64.AppImage cli --accept-tos registration delete
```

## Drop-in Compatibility for `rotator.py`

When symlinked as `warp-cli`, the AppImage inspects `argv[0]` and forwards all arguments directly to the bundled `warp-cli`:

```sh
ln -s /path/to/warp-x86_64.AppImage /usr/local/bin/warp-cli

# Start daemon in container entrypoint:
/path/to/warp-x86_64.AppImage svc &

# rotator.py executes without changes:
warp-cli --accept-tos status
```
