# AGENTS.md

Cloud builder for sharun-based AppImages. One generic pipeline, N declarative recipes. No versions pinned in templates — upstream is resolved at build time.

## What lives where

- `packages/<app>/package.yml` — the only per-app file. Copy `packages/vesktop-bin`, edit values.
- `builder/build.sh` — canonical flow (pacman + debloat + install to `/usr` + quick-sharun + `--make-appimage` + `--test`). Don't invent another flow.
- `builder/pinned.sh` — single bump point for quick-sharun/debloat URLs + container.
- `builder/check-update.sh` + `builder/discover.py` — version fingerprint vs `versions.json` state. Workflow updates state; hand-edit only to force rebuilds.
- `.github/workflows/build.yml` — cron 24h + manual dispatch + push; rolling `continuous` release. Push touching `packages/<app>` forces rebuild (`--force-include`) even if version unchanged. CHANGED comes from the push-payload file list (checkout git diffs proved unreliable: empty twice); git ranges are fallback only.
- `builder/recipe.py` — stdlib-only parser + schema check. Restricted YAML only: scalars, one-level `source:`, inline `[a, b]` or block `- item` lists. No anchors/multiline/nesting.

## Binding packaging rules (from upstream quick-sharun)

- Arch Linux container only (`ghcr.io/pkgforge-dev/archlinux:latest`). Never other distros.
- Install to `/usr` first, pass binaries to quick-sharun. Never copy libs manually; never touch `$APPDIR/shared`; never strip bundled libs.
- Ignore linuxdeploy/AppImageKit/appimage-builder guidance — wrong for this model.
- AUR builds run as non-root user (`makepkg` forbids root); official pkgs as root.
- Required keys: `name, bin, icon, desktop`; plus `main_bin` when `desktop: DUMMY`, or `source.type: url` (except `.deb`), or `git`. `git` also needs `repo+ref+build_out`. Archive `url` needs `url_bin`; `.deb` unpacks to `/`, no `main_bin`.
- `url_version`: `github:owner/repo`, `apt <Packages-url> <pkg>`, else ETag/Last-Modified HEAD fingerprint.
- Short-lived CLI/TUI: set `test_args: [--version]`; otherwise `--test` requires the app to stay alive 12s.
- `url` sources: single binary, or archive (needs `url_bin` inner path).
  Needs `main_bin`.
- `git` sources: shallow-clone ref, run `build_run` lines in it, install
  `build_out` to `/usr/bin/$MAIN_BIN`. Version = ls-remote SHA.
- If the app's launcher is a wrapper script with an absolute `exec /opt/...`,
  bundle the REAL binary in `bin:` instead. CI `--test` false-passes when the
  build machine has the app installed (absolute path leaks to the system
  copy); always validate the AppImage on a clean machine.
- Chromium apps: zygote sandbox dies inside the image (FATAL goodbye);
  bake `app_env: [ELECTRON_DISABLE_SANDBOX=1]` (verified working).
- Chromium/Electron GTK tray is loaded conditionally, so the strace in CI
  never sees the dlopen and quick-sharun ships no `gtk3`: on a musl host the
  UI then resolves to the host's Qt6 and aborts ("Could not load the Qt
  platform plugin"). `deploy: [gtk]` in the recipe installs `gtk3 libxss`
  and sets `DEPLOY_GTK=1` so the stack always ships. Use it for every
  Chromium/Electron recipe; `DEPLOY_GTK` is one of the few upstream knobs
  that is not auto-detected.
- Upstream `chrome-wrapper` launcher scripts (brave) use
  `exec > >(exec cat)` (needs `/dev/fd`, absent on musl), `|| true` after
  the final exec (hides the real exit code) and write a host-global
  `mimeapps.list`. Strip all three via `patch_run` when bundling one, and
  add `/usr/bin/bash` to `extra_paths` so the shebang never lands on the
  host's musl bash.
- `extra_paths` deploys ELFs + closures only, NEVER plain data: if the app
  needs files next to the binary (bootstrap tarballs, helper scripts),
  pair it with `data_from` pointing at the same dir (verified: missing
  files pass CI when the build machine has them installed — absolute-path
  leak; always validate on a clean machine).
- 32-bit apps: quick-sharun auto-detects LIB32 and stages drivers under
  `AppDir/lib/32` (not `lib32/`); runtime-downloaded 32-bit binaries need
  an explicit loader fallback (Valve's `/lib/ld-linux.so.2`), and musl
  hosts need a bwrap sandbox injecting both loaders (covers 64-bit
  grandchildren like CEF helpers).
- Chromium dlopens its GL stack + ffmpeg from the exe dir (never deployed
  by quick-sharun): use `runtime_from: /opt/<app>` or the GPU process dies
  and no frame is ever presented (app runs headless, no window).
- quick-sharun flattens helper binaries into `bin/` (`bin/x2t`): if the app
  resolves helpers relative to the exe dir (`bin/converter/x2t`), recreate
  the tree with `link_run` (symlinks to the deployed stubs) or the app
  quits at startup (verified: OnlyOffice).

## Validate locally (Arch)

```sh
bash -n builder/*.sh
python3 builder/parse-recipe.py packages/<app>
sh builder/check-update.sh packages/<app>
python3 builder/discover.py --force   # dry-run matrix without building
```
