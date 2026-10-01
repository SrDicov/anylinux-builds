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
    echo "albion-online: seeding $dest from image (first run or launcher update)" >&2
    rm -rf "$dest"
    mkdir -p "$dest"
    cp -r "$img/." "$dest/"
fi
# The launcher singleton-guards via launcher.lock holding its pid. A killed
# container/CI phase (trace then test share one HOME) leaves a STALE pid
# behind and every later start exits 0 "already running". Reap only when
# the owner is verifiably dead; a live owner keeps its protection.
lockdir="${XDG_DATA_HOME:-$HOME/.local/share}/Sandbox Interactive GmbH/Albion Online Launcher"
lock="$lockdir/launcher.lock"
if [ -f "$lock" ]; then
    owner="$(head -n 1 "$lock" 2>/dev/null)"
    case "$owner" in
        ''|*[!0-9]*) ;;
        *) if ! kill -0 "$owner" 2>/dev/null; then
               echo "albion-online: reaping stale launcher.lock (pid $owner dead)" >&2
               rm -f "$lock"
           fi ;;
    esac
fi
export QT_QPA_PLATFORM_PLUGIN_PATH="$dest/launcher/plugins/platforms"
export QT_PLUGIN_PATH="$dest/launcher/plugins/"
# Via loader (exec "$loader" "$elf"): Qt resuelve applicationDirPath() por
# AT_EXECFN = el loader, no el ELF, asi que QtWebEngineProcess no aparece
# junto al "exe" y aborta. Ruta explicita (el propio FATAL la sugiere).
export QTWEBENGINEPROCESS_PATH="$dest/launcher/QtWebEngineProcess"
export QTWEBENGINE_RESOURCES_PATH="$dest/launcher/resources"
export QTWEBENGINE_LOCALES_PATH="$dest/launcher/translations/qtwebengine_locales"
# AT_EXECFN follows the execve path, not argv[0]: with exec "$loader" "$elf"
# Qt believes the exe lives next to the LOADER (env vars above paper over
# WebEngine, but translations and any self-respawn still resolve wrong).
# Point the copy's INTERP at the bundled loader once, then exec directly:
# kernel loads via image ld, AT_EXECFN is the real ELF everywhere (glibc
# and musl alike). patchelf ships in the image (extra_paths).
LD_CAND=""
for l in "$base/lib/ld-linux-x86-64.so.2" "$here/../lib/ld-linux-x86-64.so.2"; do
    if [ -x "$l" ]; then LD_CAND="$l"; break; fi
done
if [ -n "$LD_CAND" ]; then
    for b in "$dest/launcher/Albion-Online" "$dest/launcher/QtWebEngineProcess" "$dest/launcher/xdelta3"; do
        if [ -f "$b" ]; then
            "$here/patchelf" --set-interpreter "$LD_CAND" "$b" 2>/dev/null || true
        fi
    done
fi
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
# INTERP of the copy now points at the bundled loader (or the host one on
# glibc when the image loader is absent): direct exec works everywhere and
# AT_EXECFN is the real ELF.
exec "$dest/launcher/Albion-Online" --no-sandbox "$@"
