# UPI QR

Host a form on the **Waveshare ESP32-C6-ePaper-1.54**. Submit UPI details; the board draws a personal `upi://pay` QR on the e-paper (board URL above the code).

## Link shape (personal / P2P)

```
upi://pay?pa=9696326406@cnrb&pn=Homoeocare&am=100.00&cu=INR
```

| Param | Required | Notes |
|-------|----------|--------|
| `pa` | yes | VPA; `@` left unencoded |
| `pn` | no | Payee name if provided; spaces → `%20` |
| `am` | no | Decimal **INR** (e.g. `100.00`), **not paise** |
| `tn` | no | Short note; spaces → `%20` |
| `cu` | yes | Always `INR` |

**Omit `mc` and `mode` for personal VPAs.** Merchant codes need a real MCC from the PSP; `mc=0000` / `mode=01` often break scanning apps on individual handles.

## Wi-Fi

```bash
cp .env.example .env   # repo root — set WIFI_SSID / WIFI_PASS
```

## Build & flash

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd upi-qr && pio run -t upload && pio device monitor
```

Or **Flash: upi-qr**.
