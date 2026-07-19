# EDEN — Smart Dual-Plant Terrarium Dashboard

A progressive web app (PWA) dashboard for the **EDEN** smart terrarium — a
university Interactive Systems project (Team 17, Saarland University). It shows
live sensor readings, per-zone soil trends, and lets you control the pumps, fan
and grow light. Vanilla HTML/CSS/JS, zero dependencies, zero CDN calls.

## What it does

- Photo hero with a live greeting and a **liquid-glass** health panel (health,
  humidity, soil, reservoir).
- **Trends** — a card per metric (temperature, humidity, light, reservoir) with
  its own bar chart, current value and change indicator.
- **Watering zones** — independent per-zone soil moisture with status.
- **Systems** — Auto/Manual control of Pump A, Pump B, Fan and Grow light, with
  plain-language explanations of why automation is acting.
- **History** table and offline-safe handling of a disconnected device.
- Installable PWA, works fully offline once loaded.

## Run locally

```bash
cd dashboard
python3 -m http.server 8080
# open http://localhost:8080/
```

By default the dashboard talks directly to the ESP32S sensor hub at
`http://192.168.0.188`. Change the address in **Settings** if your device gets
a different IP.

## Using it with the real terrarium

The dashboard polls the ESP32S sensor hub directly over the local network
(`GET /api/data`, `GET /control?...`). It needs:

- Your browser to be on the **same WiFi network** as the ESP32S.
- The page to be served over **plain HTTP** — an HTTPS page cannot call a
  plain-HTTP device (mixed content).

See [`dashboard/README.md`](dashboard/README.md) for more on serving the
files, the hero-photo, and calibration notes.

## Acknowledgements

This project was developed by **Team 17** as part of the **Interactive Systems**
course at **Saarland University**.

During development, we used **Claude (Anthropic)** as an AI coding assistant to
support the implementation of several software components. Claude assisted with
code generation, debugging, and iterative development for:

- The EDEN web dashboard (HTML, CSS, and JavaScript).
- The ESP32-based touchscreen interface and its on-screen user interface.
- The ESP32S responsible for sensor integration and communication with
  the dashboard.

All design decisions, system architecture, hardware integration, testing, and
final implementation were carried out by the project team.
