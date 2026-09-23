# anylinux-builds

Daily AppImage builds in GitHub Actions, following the AnyLinux (sharun) model:
each app is installed to `/usr` on Arch Linux and bundled with everything it
needs — no host libc dependency. Only rebuilds when upstream changes.

**Downloads:** [continuous release](../../releases/tag/continuous)

## Add an app

```sh
cp -r packages/htop packages/myapp
$EDITOR packages/myapp/package.yml   # 10 fields, no versions pinned
git add packages/myapp && git commit -m "feat: add myapp"
```

Then trigger it once manually: *Actions → Daily builds → Run workflow*
with `package = myapp`. The daily cron picks it up from then on.

`package.yml` fields: `name`, `source.type` (`pacman`|`aur`|`url`|`git`),
`source.pkg` or `source.url` (+`url_version: github:owner/repo`|`etag`;
archives `.tar.gz/.tgz/.tar.xz/.tar.zst/.zip` need `url_bin`, the inner
binary path)
or `source.repo`+`ref` for git (built from source; needs `build_out` +
`build_run` lines),
`bin`, `icon`/`desktop` (real `/usr` paths or `DUMMY` + `main_bin`),
`build_deps`, `debloat` (`common`|`mesa`|`none`),
`hooks` (e.g. `[fix-namespaces.hook]`), `host_drivers` (GTK/Qt only),
`test_args` (optional: e.g. `[--version]` — direct smoke test for CLIs/TUIs
that can't stay alive 12s under `--test`),
`extra_paths` (optional: extra dirs whose `.so`/binaries get deployed),
`data_from` (optional: dir of runtime DATA copied next to the binary —
Electron `icudtl.dat`/`*.pak`/`locales`/`app.asar`; never ELFs),
`app_env` (optional: `KEY=VALUE` lines baked into the image env),
`runtime_from` (optional: dir whose `*.so*` are staged next to the binary —
Chromium `libEGL`/`libGLESv2`/SwiftShader/`libffmpeg`; skipped if already
deployed, so sharun libs are never shadowed).

## How it works

- `builder/build.sh packages/<app>` — pacman/debloat → install → quick-sharun → `--make-appimage` → `--test`. Arch container only.
- `builder/check-update.sh` — prints the upstream version (repo version, AUR RPC, GitHub tag or HTTP ETag).
- `builder/discover.py` — diffs against `versions.json`, emits the build matrix. Never hand-edit `versions.json` unless forcing a rebuild (delete the key).
- Pinned download URLs live once in `builder/pinned.sh`.
- `publish` uploads to the `continuous` tag (old asset per app replaced) and commits the new `versions.json`.

Credits: packaging model by [Anylinux-AppImages](https://github.com/pkgforge-dev/Anylinux-AppImages) (sharun + uruntime + dwarfs).
