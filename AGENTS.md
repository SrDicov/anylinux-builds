# AGENTS.md

Cloud builder for sharun-based AppImages. One generic pipeline, N declarative recipes (`packages/<app>/package.yml`). No versions pinned in templates — upstream is resolved at build time.

## What lives where

- `packages/<app>/package.yml` — The only per-app file. Copy `packages/vesktop`, edit values.
- `builder/recipe.py` — Stdlib-only parser and schema validator for `package.yml` (no PyYAML). Can validate a recipe file directly: `python3 builder/recipe.py packages/<app>/package.yml`.
- `builder/parse-recipe.py` — Generates shell-safe KEY='val' assignments for `build.sh` or extracts lists (`--get <key>`).
- `builder/build.sh` — Canonical build flow (Arch container only): pacman + debloat + install to `/usr` + quick-sharun + `--make-appimage` + `--test`.
- `builder/pinned.sh` — Single bump point for quick-sharun/debloat URLs and container image.
- `builder/check-update.sh` — Resolves upstream version fingerprint (pacman repo, AUR RPC, GitHub release tag, APT repo, git ref, or HTTP ETag).
- `builder/discover.py` — Diffs `check-update.sh` output against `versions.json` to emit the GitHub Actions matrix.
- `versions.json` — State file tracking deployed versions. Workflow updates it on publish; hand-edit or delete a key only to force rebuilds.
- `.github/workflows/build.yml` — Cron 24h + manual dispatch (`workflow_dispatch`) + push to `main`; rolling `continuous` release tag.

## Recipe Schema (`package.yml`)

Parsed by custom minimal parser (`builder/recipe.py`), NOT PyYAML. Restricted YAML only: scalars, single-level `source:`, inline `[a, b]` or block `- item` lists. No anchors, multiline scalars (`|`, `>`), or arbitrary nesting.

- **Required keys**: `name`, `bin`, `icon`, `desktop`.
- **`main_bin` required when**: `desktop: DUMMY`, or `source.type: url` (unless `.deb`), or `source.type: git`.
- **Source types**:
  - `pacman`: requires `source.pkg`.
  - `aur`: requires `source.pkg`. Builds as unprivileged `builder` user (`makepkg` forbids root).
  - `url`: requires `source.url`. Archives (`.tar.*`, `.zip`) require `source.url_bin` (inner binary path). `.deb` unpacks payload directly to `/` and needs no `main_bin`.
  - `git`: requires `source.repo`, `source.ref`, `build_out` (binary path relative to repo root), and `main_bin`.
- **`source.url_version`**: `github:owner/repo`, `apt <Packages-url> <pkg>` (strips epoch), or fallback HTTP ETag / Last-Modified.
- **Optional recipe keys**:
  - `build_deps`: pacman packages installed before building/bundling.
  - `debloat`: `common` (default), `mesa`, or `none`.
  - `hooks`: e.g. `[fix-namespaces.hook]`.
  - `deploy`: subset of `[gtk, notify, xss, qt]`. `gtk` installs `gtk3 libxss` and sets `DEPLOY_GTK=1`.
  - `test_args`: e.g. `[--version]` for short-lived CLI/TUI apps. GUI apps omit this to run quick-sharun's 12s `--test`.
  - `patch_run`: block list of shell commands run after install, before bundling.
  - `build_run`: block list of shell commands run inside cloned git repo for `source.type: git`.
  - `extra_paths`: extra directories whose ELFs and closures get deployed. ELFs only, never plain data.
  - `data_from`: directory of non-ELF runtime data (`icudtl.dat`, `*.pak`, `locales/`, `app.asar`) copied beside the binary in `$APPDIR/bin`.
  - `runtime_from`: directory whose `*.so*` and `vk_swiftshader_icd.json` are staged beside the binary (skips already deployed libs; required for Chromium GL/ffmpeg).
  - `app_env`: `KEY=VALUE` lines baked into `$APPDIR/.env`.
  - `link_run`: shell commands run with `$APPDIR` set before `--make-appimage` (recreate helper binary trees flattened by quick-sharun).

## Binding Packaging Rules (quick-sharun)

- **Arch Linux container only**: Builds MUST run in `ghcr.io/pkgforge-dev/archlinux:latest`.
  - **CRITICAL**: Never run `builder/build.sh` directly on the host machine. It modifies `/etc/pacman.conf`, runs `pacman -Syu`, and installs payloads directly to `/usr` and `/`.
- **Install to `/usr` first, pass binaries to quick-sharun**: Never copy libs manually; never touch `$APPDIR/shared`; never strip bundled libs. Ignore linuxdeploy/AppImageKit instructions.
- **Wrapper scripts**: If the app launcher is a script with absolute paths (`exec /opt/...`), bundle the REAL binary in `bin:` instead. CI `--test` false-passes when the build machine has the app installed (absolute path leaks to host copy); always validate on a clean machine.
- **Chromium / Electron apps**:
  - Zygote sandbox dies inside container/AppImage: set `app_env: [ELECTRON_DISABLE_SANDBOX=1]`.
  - GTK tray/notification is loaded conditionally, escaping strace in CI: set `deploy: [gtk]` so `gtk3` and `libxss` always ship. Otherwise musl hosts abort resolving host Qt6.
  - GL stack and ffmpeg are dlopened from the exe dir: set `runtime_from: /opt/<app>` or GPU process dies and app runs headless.
  - Launcher scripts (`chrome-wrapper`) on brave use `exec > >(exec cat)` (needs `/dev/fd`) and `|| true`: musl-hostile, but do NOT patch blind (strip attempt caused intermittent SIGILL on glibc).
- **`extra_paths` vs `data_from`**: `extra_paths` deploys ELFs + closures only, NEVER plain data. Pair with `data_from` for non-ELF assets next to the binary.
- **Helper binary trees**: quick-sharun flattens helper binaries into `bin/` (`bin/x2t`). If the app resolves helpers relative to the exe dir (`bin/converter/x2t`), recreate symlinks with `link_run` (verified: OnlyOffice).
- **32-bit apps**: quick-sharun auto-detects LIB32 and stages drivers under `AppDir/lib/32` (not `lib32/`). Runtime-downloaded 32-bit binaries require an explicit loader fallback (Valve's `/lib/ld-linux.so.2`).

## Local Verification Commands

```sh
# 1. Shell script syntax check
bash -n builder/*.sh

# 2. Recipe parsing and schema validation
python3 builder/parse-recipe.py packages/<app>
python3 builder/recipe.py packages/<app>/package.yml
python3 -c 'from builder.recipe import load_recipe; import glob; [load_recipe(y) for y in glob.glob("packages/*/package.yml")]'

# 3. Upstream version check & matrix discovery (safe on host)
sh builder/check-update.sh packages/<app>
python3 builder/discover.py --filter <app> --force

# 4. Full build test (CONTAINER ONLY - NEVER on bare host)
docker run --rm -v "$PWD:/work" -w /work ghcr.io/pkgforge-dev/archlinux:latest sh -c "pacman -Sy --noconfirm python3 && sh builder/build.sh packages/<app>"
```
