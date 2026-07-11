# EDEN Web Dashboard (PWA)

Progressive-web-app dashboard for the EDEN smart terrarium. Vanilla HTML/CSS/JS,
zero dependencies, zero CDN calls — everything works on an isolated LAN.

## Files

| File | Purpose |
|---|---|
| `index.html` | The whole app — UI, polling, controls, sparklines, demo mode |
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
line in `index.html`. If you serve the app from the CYD, remember to upload `hero.jpg` there too.

## Quick start (local preview)

```bash
cd dashboard
python3 -m http.server 8080
# open http://localhost:8080/?demo=1   ← demo mode, simulated terrarium
```

On `localhost` the full PWA works (service worker + install prompt), so this is
also how to test installability. Demo mode simulates a live terrarium including
closed-loop pump/fan/LED behaviour, so every control is exercisable without
hardware. Toggle it any time in Settings (gear icon).

## Talking to the real terrarium

The app polls `GET /api/data` every 2.5 s and sends `GET /control?...` commands,
exactly per `EDEN_API_Documentation.md`. Two ways to run it:

1. **Served from the CYD (intended setup).** Upload these files to the CYD and
   serve them at `/`. All fetches are same-origin — no configuration needed.
2. **Served from anywhere else** (laptop dev server, phone). Open Settings and
   set the terrarium address, e.g. `http://192.168.2.50`. This requires the CYD
   to send `Access-Control-Allow-Origin: *` on `/api/data` and `/control`
   (one `server.sendHeader(...)` line per handler in the CYD firmware), and the
   page must be served over **plain HTTP** — an HTTPS page cannot fetch an HTTP
   device (mixed content).

### Serving from the CYD

The files are small (~45 KB total, ~15 KB gzipped for `index.html`). Options:

- **LittleFS**: upload the folder as a filesystem image and serve with
  `server.serveStatic("/", LittleFS, "/");`
- **PROGMEM**: gzip `index.html`, embed as a byte array, and send with
  `Content-Encoding: gzip`. Do the same for `sw.js` + `manifest.webmanifest` + icons,
  or skip them (see the PWA caveat below — they're only useful on a secure origin).

## PWA caveats — read before demoing

- **Service workers require a secure context** (HTTPS or `localhost`). Served
  from the CYD over `http://<ip>`, the SW simply doesn't register (the code
  guards for this) — the app still works fully, it just isn't "installable" on
  Android and won't work offline-from-cache.
- **iOS "Add to Home Screen" works anyway** — Safari uses the
  `apple-touch-icon` + meta tags regardless of HTTPS. You get a full-screen,
  home-screen EDEN app on an iPhone even when served plain-HTTP from the CYD.
  This is the recommended demo path.
- For the *full* Android install experience you'd need HTTPS, which an ESP32
  can't realistically serve — a reverse proxy on a Pi/laptop would do it.

## Tunables

All thresholds live in one `TH` object at the top of the `<script>` in
`index.html`, mirroring the API doc §3 (soil dry point 2500, comfort band
18–28 °C / 40–80 %, reservoir low >10 cm, LED curve 0→1000).

⚠️ **Soil direction**: the firmware waters when `soil < 2500` (low = dry), but
the Milestone-2 bench logs (dry ≈ 3120, wet ≈ 1680) imply the opposite
direction. The dashboard follows the firmware so its labels always match actual
pump behaviour. If the firmware gets fixed, swap `SOIL_RAW_DRY`/`SOIL_RAW_WET`
in `TH` — one edit, nothing else changes.

## History

Sensor history is kept client-side (the device has no history endpoint):
in-memory per poll, persisted to `localStorage` (thinned to ≥20 s spacing,
24 h retention). Sparklines show the last 30 min; the table view (☰ button)
shows the last 80 readings.
