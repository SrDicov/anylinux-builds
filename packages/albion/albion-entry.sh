#!/bin/sh
# Albion Online AppImage entry. The launcher downloads game_x64/ NEXT TO
# itself (sibling of launcher/), but the image is a read-only squashfs, so
# running in place would die on the first game download. Seed a writable
# copy under XDG_DATA_HOME on first run (or when the image ships a newer
# launcher) and run there. Same idea as steam's bootstrap copy.
# Env and steam-flag logic mirror upstream data/Albion-Online one to one.
here="$(dirname "$(readlink -f "$0")")"
base="${SHARUN_DIR:-"$(dirname "$here")"}"
img="$base/share/albion"
# Build-time fallback: link_run seeds share/albion after the bundle trace,
# so while quick-sharun traces this script /opt/albion is the only copy.
[ -d "$img/launcher" ] || img="/opt/albion"
dest="${XDG_DATA_HOME:-$HOME/.local/share}/albiononline"
if [ ! -x "$dest/launcher/Albion-Online" ] || [ "$img/launcher/version.txt" -nt "$dest/launcher/version.txt" ]; then
    rm -rf "$dest"
    mkdir -p "$dest"
    cp -r "$img/." "$dest/"
fi
export QT_QPA_PLATFORM_PLUGIN_PATH="$dest/launcher/plugins/platforms"
export QT_PLUGIN_PATH="$dest/launcher/plugins/"
OSNAME=$(grep '^NAME=' /etc/os-release 2>/dev/null | cut -d= -f2 | tr -d '"')
if [ "$OSNAME" != "SteamOS" ]; then
    export LIBGL_ALWAYS_SOFTWARE=1
    export QSG_INFO=1
else
    export QT_QPA_PLATFORM="xcb;eglfs"
    export __GL_GlslUseCollapsedArrays=0
fi
HASSTEAMFLAG=0
for ARG in "$@"; do
    if [ "$ARG" = "-steam" ]; then HASSTEAMFLAG=1; break; fi
done
if [ "$HASSTEAMFLAG" = "0" ] && [ -n "$SteamGameId" ] && [ "$SteamGameId" != "0" ]; then
    set -- "$@" "-steam"
fi
# The HOME copy runs outside the image: exec it through the bundled loader
# so musl hosts (no /lib64/ld-linux) work. Falls back to a direct exec.
for l in "$base/lib/ld-linux-x86-64.so.2" "$here/../lib/ld-linux-x86-64.so.2"; do
    if [ -x "$l" ]; then exec "$l" "$dest/launcher/Albion-Online" --no-sandbox "$@"; fi
done
exec "$dest/launcher/Albion-Online" --no-sandbox "$@"
