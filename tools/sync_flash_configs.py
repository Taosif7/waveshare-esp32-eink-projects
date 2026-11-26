#!/usr/bin/env python3
"""Rewrite the workspace Flash run configs from top-level PlatformIO projects.

A project is any directory next to this script's parent that contains
platformio.ini. A project with more than one [env:...] gets one Flash item
per environment, and that item passes -e so upload does not walk every board.
Pass --check to fail when the committed JSON is stale.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VSCODE = ROOT / ".vscode"


def env_names(project):
    names = []
    for line in (project / "platformio.ini").read_text().splitlines():
        stripped = line.strip()
        if stripped.startswith("[env:") and stripped.endswith("]"):
            names.append(stripped[len("[env:") : -1].strip())
    return names


def targets():
    items = []
    for path in sorted(ROOT.iterdir()):
        if not path.is_dir() or not (path / "platformio.ini").is_file():
            continue
        envs = env_names(path)
        if len(envs) > 1:
            items.extend((path.name, env) for env in envs)
        else:
            items.append((path.name, None))
    return items


def label(name, env):
    if env is None:
        return name
    return f"{name} ({env})"


def task(name, env):
    title = label(name, env)
    args = ["run", "-t", "upload"] if env is None else ["run", "-e", env, "-t", "upload"]
    return {
        "label": f"flash: {title}",
        "type": "process",
        "command": "${env:HOME}/.platformio/penv/bin/pio",
        "args": args,
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


def configuration(name, env, order):
    title = label(name, env)
    return {
        "name": title,
        "type": "node",
        "request": "launch",
        "preLaunchTask": f"flash: {title}",
        "runtimeArgs": ["-e", "process.exit(0)"],
        "console": "internalConsole",
        "internalConsoleOptions": "neverOpen",
        "presentation": {"group": "Flash", "order": order},
    }


def render(items):
    tasks = {"version": "2.0.0", "tasks": [task(name, env) for name, env in items]}
    launch = {
        "version": "0.2.0",
        "configurations": [
            configuration(name, env, order)
            for order, (name, env) in enumerate(items, start=1)
        ],
    }
    return {
        VSCODE / "tasks.json": json.dumps(tasks, indent=2) + "\n",
        VSCODE / "launch.json": json.dumps(launch, indent=2) + "\n",
    }


def main():
    items = targets()
    files = render(items)
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
    print(f"Wrote flash run configs for {len(items)} targets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
