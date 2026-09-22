#!/bin/sh
# Generic AnyLinux builder. Reads one packages/<app>/ dir and produces
# dist/<app>-x86_64.AppImage. Must run on Arch Linux
# (ghcr.io/pkgforge-dev/archlinux:latest). Usage: build.sh packages/<app>
set -eu

RECIPE_DIR="${1:?usage: build.sh packages/<app>}"
BUILDER_DIR="$(dirname "$(readlink -f "$0")")"
# shellcheck disable=SC1091
. "$BUILDER_DIR/pinned.sh"

eval "$(python3 "$BUILDER_DIR/parse-recipe.py" "$RECIPE_DIR")"

ARCH="$(uname -m)"
export ICON="$RECIPE_ICON"
export DESKTOP="$RECIPE_DESKTOP"
export OUTPATH="$PWD/dist"
export OUTNAME="$RECIPE_NAME-$ARCH.AppImage"
[ -n "$RECIPE_MAIN_BIN" ] && export MAIN_BIN="$RECIPE_MAIN_BIN"
[ -n "$RECIPE_HOOKS" ] && export ADD_HOOKS="$RECIPE_HOOKS"
[ "$RECIPE_HOST_DRIVERS" = 1 ] && export USE_HOST_DRIVERS_EXPERIMENTAL=1

echo "=== installing base build deps ==="
pacman -Syu --noconfirm \
	base-devel git patchelf wget xorg-server-xvfb python3 \
	$RECIPE_BUILD_DEPS

case "$RECIPE_DEBLOAT" in
common)
	wget --retry-connrefused --tries=30 "$DEBLOAT_URL" -O ./get-debloated-pkgs.sh
	chmod +x ./get-debloated-pkgs.sh
	./get-debloated-pkgs.sh --add-common --prefer-nano
	;;
mesa)
	wget --retry-connrefused --tries=30 "$DEBLOAT_URL" -O ./get-debloated-pkgs.sh
	chmod +x ./get-debloated-pkgs.sh
	./get-debloated-pkgs.sh --add-mesa --prefer-nano
	;;
none) echo "=== debloat skipped ===" ;;
*)
	echo "ERROR: debloat must be common|mesa|none" >&2
	exit 1
	;;
esac

echo "=== installing application (source: $SOURCE_TYPE) ==="
case "$SOURCE_TYPE" in
pacman)
	pacman -S --noconfirm "$SOURCE_PKG"
	;;
aur)
	# makepkg refuses to run as root; build as an unprivileged user.
	command -v useradd >/dev/null 2>&1 || pacman -S --noconfirm shadow
	id builder >/dev/null 2>&1 || useradd -m builder
	echo "builder ALL=(ALL) NOPASSWD: /usr/bin/pacman" >/etc/sudoers.d/builder-pacman
	chmod 440 /etc/sudoers.d/builder-pacman
	rm -rf /tmp/aur-build
	mkdir -p /tmp/aur-build
	chown builder:builder /tmp/aur-build
	su builder -c "git clone https://aur.archlinux.org/$SOURCE_PKG.git /tmp/aur-build/$SOURCE_PKG"
	su builder -c "cd /tmp/aur-build/$SOURCE_PKG && makepkg -si --noconfirm"
	;;
url)
	# v1 contract: the URL is a single executable binary.
	wget --retry-connrefused --tries=30 "$SOURCE_URL" -O /tmp/payload-bin
	install -Dm755 /tmp/payload-bin "/usr/bin/$RECIPE_MAIN_BIN"
	;;
*)
	echo "ERROR: unknown source.type '$SOURCE_TYPE'" >&2
	exit 1
	;;
esac

echo "=== bundling AppImage ==="
wget --retry-connrefused --tries=30 "$QUICK_SHARUN_URL" -O ./quick-sharun
chmod +x ./quick-sharun
./quick-sharun "$RECIPE_BIN"

./quick-sharun --make-appimage

echo "=== testing ==="
if [ -n "$RECIPE_TEST_ARGS" ]; then
	# Short-lived CLI: must exit 0 with the given args (missing
	# bundled libs fail here). Same extract-and-run env as --test.
	export APPIMAGE_TARGET_DIR="$PWD"/_test-app
	export APPIMAGE_EXTRACT_AND_RUN=1
	# shellcheck disable=SC2086
	./dist/*.AppImage $RECIPE_TEST_ARGS
else
	# Long-running GUI/TUI: quick-sharun requires it to stay alive
	# 12s. Force a sane TERM (CI sets TERM=unknown; ncurses aborts).
	# ELECTRON_DISABLE_SANDBOX covers Chromium apps as container root.
	TERM=xterm ELECTRON_DISABLE_SANDBOX=1 ./quick-sharun --test ./dist/*.AppImage
fi
echo "=== built: $OUTPATH/$OUTNAME ==="
