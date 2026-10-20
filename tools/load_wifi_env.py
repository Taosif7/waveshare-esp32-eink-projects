"""Inject WIFI_SSID / WIFI_PASS from the repo-root .env (or the shell).

Shared by every PlatformIO project that sets:
  extra_scripts = pre:../tools/load_wifi_env.py

Setup once at the repo root:
  cp .env.example .env
  # edit WIFI_SSID / WIFI_PASS

Shell exports override the file (useful for CI).
"""

Import("env")  # type: ignore  # PlatformIO SCons

import os
from pathlib import Path


def parse_env_file(path: Path) -> dict:
    values = {}
    if not path.is_file():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export ") :].strip()
        if "=" not in line:
            continue
        key, val = line.split("=", 1)
        key = key.strip()
        val = val.strip()
        if len(val) >= 2 and val[0] == val[-1] and val[0] in "\"'":
            val = val[1:-1]
        values[key] = val
    return values


# PROJECT_DIR is the sketch folder (e.g. upi-qr/); repo root is its parent.
# (__file__ is not set in PlatformIO SCons extra scripts.)
repo_root = Path(env["PROJECT_DIR"]).resolve().parent
env_path = repo_root / ".env"
file_vals = parse_env_file(env_path)

ssid = os.environ.get("WIFI_SSID") or file_vals.get("WIFI_SSID", "")
password = os.environ.get("WIFI_PASS") or file_vals.get("WIFI_PASS", "")

placeholder = not ssid or ssid == "YOUR_WIFI_SSID" or password == "YOUR_WIFI_PASSWORD"
if placeholder:
    print(
        "\n"
        "ERROR: Wi-Fi credentials missing.\n"
        f"  Expected file: {env_path}\n"
        "  1. cp .env.example .env   (from the repo root)\n"
        "  2. Edit WIFI_SSID and WIFI_PASS\n"
        "  Or export WIFI_SSID / WIFI_PASS in your shell before flashing.\n"
    )
    env.Exit(1)

env.Append(
    CPPDEFINES=[
        ("WIFI_SSID", env.StringifyMacro(ssid)),
        ("WIFI_PASS", env.StringifyMacro(password)),
    ]
)
print(f"Wi-Fi macros loaded from {env_path.name} (SSID={ssid!r})")
