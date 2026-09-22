#!/usr/bin/env python3
"""Print package.yml as shell-safe KEY='value' assignments for build.sh."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recipe import load_recipe  # noqa: E402


def sh(value):
    return "'" + str(value).replace("'", "'\\''") + "'"


def main():
    recipe_dir = sys.argv[1]
    data = load_recipe(os.path.join(recipe_dir, "package.yml"))
    src = data.get("source", {})
    out = {
        "RECIPE_NAME": data["name"],
        "SOURCE_TYPE": src.get("type", ""),
        "SOURCE_PKG": src.get("pkg", ""),
        "SOURCE_URL": src.get("url", ""),
        "SOURCE_URL_VERSION": src.get("url_version", ""),
        "RECIPE_BIN": data["bin"],
        "RECIPE_ICON": data["icon"],
        "RECIPE_DESKTOP": data["desktop"],
        "RECIPE_MAIN_BIN": data.get("main_bin", ""),
        "RECIPE_BUILD_DEPS": " ".join(data.get("build_deps", []) or []),
        "RECIPE_DEBLOAT": data.get("debloat", "common"),
        "RECIPE_HOOKS": ":".join(data.get("hooks", []) or []),
        "RECIPE_HOST_DRIVERS": "1"
        if str(data.get("host_drivers", "false")).lower() in ("1", "true", "yes")
        else "0",
    }
    for key, value in out.items():
        print(f"{key}={sh(value)}")


if __name__ == "__main__":
    main()
