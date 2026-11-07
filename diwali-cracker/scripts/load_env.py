"""Inject WIFI_SSID / WIFI_PASS from the workspace-root .env into build flags."""

Import("env")  # type: ignore[name-defined]  # noqa: F821 — PlatformIO SCons

from pathlib import Path

ROOT = Path(env["PROJECT_DIR"]).resolve().parent  # type: ignore[name-defined]
ENV_PATH = ROOT / ".env"


def load_dotenv(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


vars = load_dotenv(ENV_PATH)
ssid = vars.get("WIFI_SSID", "")
password = vars.get("WIFI_PASS", "")

if not ssid or ssid == "your-network" or not password or password == "your-password":
    print()
    print("ERROR: set WIFI_SSID and WIFI_PASS in the workspace-root .env")
    print(f"       expected file: {ENV_PATH}")
    print("       copy .env.example → .env and fill in your home Wi-Fi.")
    print()
    env.Exit(1)  # type: ignore[name-defined]

env.Append(  # type: ignore[name-defined]
    CPPDEFINES=[
        ("WIFI_SSID", env.StringifyMacro(ssid)),  # type: ignore[name-defined]
        ("WIFI_PASS", env.StringifyMacro(password)),  # type: ignore[name-defined]
    ]
)
print(f"Wi-Fi credentials loaded from {ENV_PATH.name} (SSID={ssid!r})")
