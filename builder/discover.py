#!/usr/bin/env python3
"""Decide which packages need a rebuild.

Compares check-update.sh output per recipe against versions.json.
Prints a GitHub Actions matrix JSON to stdout, and appends
`matrix=` / `count=` / `versions=` to $GITHUB_OUTPUT when present.

Usage: discover.py [--force] [--filter NAME] [--force-include "a b"] [--write JSON]
  --write '{"name": "version"}' merges entries into versions.json (publish job).
"""

import json
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILDER = os.path.join(ROOT, "builder")
PACKAGES = os.path.join(ROOT, "packages")
STATE = os.path.join(ROOT, "versions.json")


def check(recipe_dir):
    # aur.archlinux.org flakea (SSL EOF): reintentar antes de saltar el
    # paquete, o un fallo transitorio lo saca de la matriz del run.
    last = None
    for attempt in (1, 2, 3):
        try:
            out = subprocess.run(
                ["sh", os.path.join(BUILDER, "check-update.sh"), recipe_dir],
                capture_output=True,
                text=True,
                check=True,
            )
            return out.stdout.strip()
        except subprocess.CalledProcessError as e:
            last = e
            print(
                f"WARN: version check failed for {recipe_dir} "
                f"(attempt {attempt}/3): {e.stderr.strip().splitlines()[-1] if e.stderr.strip() else e}",
                file=sys.stderr,
            )
            time.sleep(15)
    raise last


def main():
    force = "--force" in sys.argv
    filt = ""
    includes = set()
    if "--filter" in sys.argv:
        filt = sys.argv[sys.argv.index("--filter") + 1]
    if "--force-include" in sys.argv:
        includes = set(sys.argv[sys.argv.index("--force-include") + 1].split())
    if "--write" in sys.argv:
        built = json.loads(sys.argv[sys.argv.index("--write") + 1])
        state = json.load(open(STATE)) if os.path.exists(STATE) else {}
        state.update(built)
        with open(STATE, "w") as f:
            json.dump(state, f, indent=2, sort_keys=True)
            f.write("\n")
        print(f"updated {STATE}")
        return

    state = json.load(open(STATE)) if os.path.exists(STATE) else {}
    matrix, versions = [], {}
    for name in sorted(os.listdir(PACKAGES)):
        recipe = os.path.join(PACKAGES, name)
        if not os.path.isdir(recipe) or not os.path.exists(
            os.path.join(recipe, "package.yml")
        ):
            continue
        if filt and name != filt:
            continue
        try:
            version = check(recipe)
        except subprocess.CalledProcessError as e:
            print(f"WARN: version check failed for {name}: {e.stderr.strip()}", file=sys.stderr)
            continue
        if not version:
            print(f"WARN: empty version for {name}", file=sys.stderr)
            continue
        if force or name in includes or state.get(name) != version:
            matrix.append({"name": name, "dir": f"packages/{name}"})
            versions[name] = version
    payload = json.dumps(matrix)
    print(payload)
    out = os.environ.get("GITHUB_OUTPUT")
    if out:
        with open(out, "a") as f:
            f.write(f"matrix={payload}\n")
            f.write(f"count={len(matrix)}\n")
            f.write(f"versions={json.dumps(versions)}\n")


if __name__ == "__main__":
    main()
