#!/usr/bin/env python3
"""Print package.yml as shell-safe KEY='value' assignments for build.sh."""

import os
import shlex
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recipe import load_recipe  # noqa: E402


def sh(value):
    return shlex.quote(str(value))


def main():
    recipe_dir = sys.argv[1]
    data = load_recipe(os.path.join(recipe_dir, "package.yml"))
    if len(sys.argv) > 3 and sys.argv[2] == "--get":
        key = sys.argv[3]
        vals = data.get(key, []) or []
        if key == "app_env":
            for line in vals:
                assert "=" in line, line
        sys.stdout.write("\n".join(vals) + ("\n" if vals else ""))
        return
    src = data.get("source", {})
    out = {
        "RECIPE_NAME": data["name"],
        "SOURCE_TYPE": src.get("type", ""),
        "SOURCE_PKG": src.get("pkg", ""),
        "SOURCE_URL": src.get("url", ""),
        "RECIPE_URL_BIN": src.get("url_bin", ""),
        "SOURCE_REPO": src.get("repo", ""),
        "SOURCE_REF": src.get("ref", ""),
        "SOURCE_URL_VERSION": src.get("url_version", ""),
        "RECIPE_BIN": data["bin"],
        "RECIPE_ICON": data["icon"],
        "RECIPE_DESKTOP": data["desktop"],
        "RECIPE_MAIN_BIN": data.get("main_bin", ""),
        "RECIPE_BUILD_DEPS": " ".join(data.get("build_deps", []) or []),
        "RECIPE_DEBLOAT": data.get("debloat", "common"),
        "RECIPE_HOOKS": ":".join(data.get("hooks", []) or []),
        "RECIPE_TEST_ARGS": " ".join(data.get("test_args", []) or []),
        "RECIPE_EXTRA_PATHS": " ".join(data.get("extra_paths", []) or []),
        "RECIPE_DATA_FROM": data.get("data_from", ""),
        "RECIPE_RUNTIME_FROM": data.get("runtime_from", ""),
        "RECIPE_BUILD_OUT": data.get("build_out", ""),
        "RECIPE_HOST_DRIVERS": "1"
        if str(data.get("host_drivers", "false")).lower() in ("1", "true", "yes")
        else "0",
        # Force deployment of libs quick-sharun only ships when its strace
        # happens to catch the dlopen. Chromium loads GTK (tray/notifications)
        # conditionally, so on CI it is never traced and the app then resolves
        # its UI stack from the HOST (musl Qt/GTK -> abort on glibc images).
        "RECIPE_DEPLOY": " ".join(data.get("deploy", []) or []),
    }
    for key, value in out.items():
        print(f"{key}={sh(value)}")


if __name__ == "__main__":
    main()
