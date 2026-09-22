#!/usr/bin/env python3
"""Minimal parser for package.yml (stdlib only, no PyYAML needed).

Supports the restricted schema used by packages/*/package.yml:
scalars, one-level `source:` mapping, inline [a, b] / [] lists,
block `- item` lists, true/false booleans. Exits non-zero on error.
"""

import sys


def parse(text):
    root, current = {}, None
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split(" #", 1)[0].rstrip()
        if not line.strip() or line.strip().startswith("#"):
            continue
        indent = len(line) - len(line.lstrip(" "))
        content = line.strip()
        if indent == 0:
            current = None
            if content.startswith("- "):
                raise ValueError(f"line {lineno}: unexpected list item at top level")
            key, _, value = content.partition(":")
            key, value = key.strip(), value.strip()
            if not value:
                if key == "source":
                    root[key] = {}
                    current = root[key]
                else:
                    raise ValueError(f"line {lineno}: key '{key}' needs a value")
            else:
                root[key] = _scalar(value)
        elif current is not None:
            if content.startswith("- "):
                raise ValueError(f"line {lineno}: block lists only valid for known list keys")
            key, _, value = content.partition(":")
            current[key.strip()] = _scalar(value.strip())
        else:
            key, _, value = content.partition(":")
            key, value = key.strip(), value.strip()
            if value.startswith("- ") or value == "-":
                raise ValueError(f"line {lineno}: inline '-' not supported, use [a, b]")
            if key in ("build_deps", "hooks"):
                raise ValueError(f"line {lineno}: '{key}' must be top-level")
            raise ValueError(f"line {lineno}: unexpected indentation")
    # block-style lists: rescan for "- item" under build_deps/hooks
    for key in ("build_deps", "hooks"):
        items = _block_list(text, key)
        if items is not None:
            root[key] = items
    return root


def _scalar(value):
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    if value.startswith("[") and value.endswith("]"):
        inner = value[1:-1].strip()
        if not inner:
            return []
        return [_scalar(v.strip()) for v in inner.split(",")]
    return value


def _block_list(text, key):
    items, inside = None, False
    for raw in text.splitlines():
        line = raw.split(" #", 1)[0].rstrip()
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip(" "))
        content = line.strip()
        if indent == 0:
            k, _, v = content.partition(":")
            inside = k.strip() == key and not v.strip()
            if inside:
                items = []
            continue
        if inside:
            if content.startswith("- "):
                items.append(_scalar(content[2:].strip()))
            else:
                inside = False
    return items


def load_recipe(path):
    with open(path) as f:
        data = parse(f.read())
    data.setdefault("build_deps", [])
    data.setdefault("hooks", [])
    data.setdefault("debloat", "common")
    data.setdefault("host_drivers", "false")
    for key in ("name", "bin", "icon", "desktop"):
        if key not in data:
            raise ValueError(f"{path}: missing required key '{key}'")
    src = data.get("source", {})
    if src.get("type") not in ("pacman", "aur", "url"):
        raise ValueError(f"{path}: source.type must be pacman|aur|url")
    if src["type"] in ("pacman", "aur") and not src.get("pkg"):
        raise ValueError(f"{path}: source.pkg required for pacman|aur")
    if src["type"] == "url" and not src.get("url"):
        raise ValueError(f"{path}: source.url required for url")
    if data["desktop"] == "DUMMY" and "main_bin" not in data:
        raise ValueError(f"{path}: main_bin required when desktop is DUMMY")
    if src["type"] == "url" and "main_bin" not in data:
        raise ValueError(f"{path}: main_bin required for url (install target)")
    return data


if __name__ == "__main__":
    print(load_recipe(sys.argv[1]))
