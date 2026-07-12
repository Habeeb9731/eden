# EDEN Web Dashboard (PWA)

Progressive-web-app dashboard for the EDEN smart terrarium. Vanilla HTML/CSS/JS,
zero dependencies, zero CDN calls — everything works on an isolated LAN.

## Files

| File | Purpose |
|---|---|
| `index.html` | The whole app — UI, polling, controls, sparklines |
| `manifest.webmanifest` | PWA manifest (name, icons, standalone display) |
| `sw.js` | Service worker — caches the app shell, never caches `/api/*` or `/control` |
| `icon.svg`, `icon-192.png`, `icon-512.png`, `apple-touch-icon.png` | App icons |
| `hero.jpg` | **Add this yourself** — the home hero photo (see below) |

## Hero photo

The home screen leads with a full-bleed photo card. Drop a JPG named **`hero.jpg`**
into this folder and it becomes the hero background automatically (any aspect ratio —
it's `object-fit: cover`, so a portrait or square shot both work; ~1200px wide is plenty).
Until you add one, the card shows a green "Add terrarium photo" placeholder — the app
is fully functional without it. To use a different filename, change the `<img class="hero-bg" src="hero.jpg">`
line in `index.html`.

## Quick start (local preview)

```bash
cd dashboard
python3 -m http.server 8080
# open http://localhost:8080/
```

On `localhost` the full PWA works (service worker + install prompt), so this is
also how to test installability.

## Talking to the real terrarium

The app polls `GET /api/data` on the ESP32S sensor hub every 2.5 s and sends
`GET /control?...` commands for pumps/fan/LED. By default it points at
`http://192.168.0.188` — change this in **Settings** (gear icon) if your
device's IP differs.

Requirements:

- The ESP32S firmware (`esp32s/esp32s.ino`) must send
  `Access-Control-Allow-Origin: *` on `/api/data` and `/control` — required
  any time the dashboard is served from a different origin than the device
  itself (e.g. a laptop dev server, or opened as a local file). Already wired
  up via `server.sendHeader(...)` in both handlers.
- The dashboard page must be served over **plain HTTP** — an HTTPS page
  cannot fetch a plain-HTTP device (mixed content), and your browser must be
  on the same WiFi network as the ESP32S.

The CYD display board (`CYD_Metro_UI_v4/`) runs its own separate embedded web
UI that proxies to the same ESP32S API — see that sketch's `handleDashboard()`
if you want the on-device touchscreen experience instead of/alongside this app.

## PWA caveats

- **Service workers require a secure context** (HTTPS or `localhost`). Served
  from a device over `http://<ip>`, the SW simply doesn't register (the code
  guards for this) — the app still works fully, it just isn't "installable" on
  Android and won't work offline-from-cache.
- **iOS "Add to Home Screen" works anyway** — Safari uses the
  `apple-touch-icon` + meta tags regardless of HTTPS.
- For the *full* Android install experience you'd need HTTPS, which would
  require a reverse proxy in front of the plain-HTTP device.

## Tunables

All thresholds live in one `TH` object at the top of the `<script>` in
`index.html`, mirroring the constants in `esp32s/esp32s.ino`:

- `SOIL_DRY: 40` — % wetness below which a zone is "dry" (matches firmware's
  `SOIL_PUMP_ON_PCT`). Note the firmware already converts soil readings to a
  0–100% value server-side (`soilRawToPercent`) — the dashboard consumes that
  percentage directly, no raw-ADC math needed on this side.
- `TEMP_MIN/MAX: 18–28°C`, `HUM_MIN/MAX: 40–80%` — comfort band, mirrors firmware.
- `WATER_LOW_CM: 4.5` — reservoir "low" cutoff. **This is tank-specific** —
  measured empty on the current reservoir reads ~5.3cm (a shallow tank), so
  the cutoff sits just below that. If you swap reservoirs, remeasure the
  empty-tank distance and adjust both this value and the matching
  `WATER_LOW_CM` in `esp32s.ino`.
- `LIGHT_MAX: 1000` — used only for the dashboard's mood-score light contribution.

The reservoir fill-bar (`fullness` calc in the `render()` function) currently
assumes a full-tank reading of `1cm` as a placeholder — measure the actual
full-tank distance and update that line for an accurate fill percentage.

## History

Sensor history is kept client-side (the device has no history endpoint):
in-memory per poll, persisted to `localStorage` (thinned to ≥20 s spacing,
24 h retention). Sparklines show the last 30 min; the table view (History tab)
shows the last 80 readings.
