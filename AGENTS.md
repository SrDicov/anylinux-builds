# AGENTS.md

Cloud builder for sharun-based AppImages. One generic pipeline, N declarative recipes. No versions pinned in templates — upstream is resolved at build time.

## What lives where

- `packages/<app>/package.yml` — the only per-app file. Copy `packages/htop`, edit values.
- `builder/build.sh` — canonical flow (pacman + debloat + install to `/usr` + quick-sharun + `--make-appimage` + `--test`). Don't invent another flow.
- `builder/pinned.sh` — single bump point for quick-sharun/debloat URLs + container.
- `builder/check-update.sh` + `builder/discover.py` — version fingerprint vs `versions.json` state. Workflow updates state; hand-edit only to force rebuilds.
- `.github/workflows/build.yml` — cron 24h + manual dispatch + push; rolling `continuous` release.

## Binding packaging rules (from upstream quick-sharun)

- Arch Linux container only (`ghcr.io/pkgforge-dev/archlinux:latest`). Never other distros.
- Install to `/usr` first, pass binaries to quick-sharun. Never copy libs manually; never touch `$APPDIR/shared`; never strip bundled libs.
- Ignore linuxdeploy/AppImageKit/appimage-builder guidance — wrong for this model.
- AUR builds run as non-root user (`makepkg` forbids root); official pkgs as root.
- `url` sources (v1): single executable binary only, needs `main_bin`.
- `git` sources: shallow-clone ref, run `build_run` lines in it, install
  `build_out` to `/usr/bin/$MAIN_BIN`. Version = ls-remote SHA.
- If the app's launcher is a wrapper script with an absolute `exec /opt/...`,
  bundle the REAL binary in `bin:` instead. CI `--test` false-passes when the
  build machine has the app installed (absolute path leaks to the system
  copy); always validate the AppImage on a clean machine.

## Validate locally (Arch)

```sh
bash -n builder/*.sh
python3 builder/parse-recipe.py packages/<app>
sh builder/check-update.sh packages/<app>
python3 builder/discover.py --force   # dry-run matrix without building
```
