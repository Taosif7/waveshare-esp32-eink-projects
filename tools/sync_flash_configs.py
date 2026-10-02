#!/usr/bin/env python3
"""Rewrite the workspace Flash run configs from top-level PlatformIO projects.

A project is any directory next to this script's parent that contains
platformio.ini. Pass --check to fail when the committed JSON is stale.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VSCODE = ROOT / ".vscode"


def projects():
    names = []
    for path in sorted(ROOT.iterdir()):
        if path.is_dir() and (path / "platformio.ini").is_file():
            names.append(path.name)
    return names


def task(name):
    return {
        "label": f"flash: {name}",
        "type": "process",
        "command": "${env:HOME}/.platformio/penv/bin/pio",
        "args": ["run", "-t", "upload"],
        "options": {"cwd": "${workspaceFolder}/" + name},
        "group": "build",
        "problemMatcher": [],
        "presentation": {
            "echo": True,
            "reveal": "always",
            "focus": True,
            "panel": "shared",
            "showReuseMessage": False,
            "clear": True,
        },
    }


def configuration(name, order):
    return {
        "name": name,
        "type": "node",
        "request": "launch",
        "preLaunchTask": f"flash: {name}",
        "runtimeArgs": ["-e", "process.exit(0)"],
        "console": "internalConsole",
        "internalConsoleOptions": "neverOpen",
        "presentation": {"group": "Flash", "order": order},
    }


def render(names):
    tasks = {"version": "2.0.0", "tasks": [task(name) for name in names]}
    launch = {
        "version": "0.2.0",
        "configurations": [
            configuration(name, order) for order, name in enumerate(names, start=1)
        ],
    }
    return {
        VSCODE / "tasks.json": json.dumps(tasks, indent=2) + "\n",
        VSCODE / "launch.json": json.dumps(launch, indent=2) + "\n",
    }


def main():
    names = projects()
    files = render(names)
    if "--check" in sys.argv:
        stale = [
            path.relative_to(ROOT).as_posix()
            for path, text in files.items()
            if not path.is_file() or path.read_text() != text
        ]
        if stale:
            print("Flash run configs are stale: " + ", ".join(stale), file=sys.stderr)
            print("Run: python3 tools/sync_flash_configs.py", file=sys.stderr)
            return 1
        return 0

    VSCODE.mkdir(exist_ok=True)
    for path, text in files.items():
        path.write_text(text)
    print(f"Wrote flash run configs for {len(names)} projects")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
