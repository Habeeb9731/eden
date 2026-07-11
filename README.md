# EDEN — Smart Dual-Plant Terrarium Dashboard

A progressive web app (PWA) dashboard for the **EDEN** smart terrarium — a
university Interactive Systems project (Team 17, Saarland University). It shows
live sensor readings, per-zone soil trends, and lets you control the pumps, fan
and grow light. Vanilla HTML/CSS/JS, zero dependencies, zero CDN calls.

## 🌿 Live demo

**https://habeeb9731.github.io/eden/**

Runs in **demo mode** (a fully simulated terrarium with working closed-loop
pump/fan/light behaviour), so every control is interactive without any hardware.

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
# open http://localhost:8080/?demo=1
```

## Using it with the real terrarium

The dashboard talks to the CYD display board over the local network
(`GET /api/data`, `GET /control?...`). See [`dashboard/README.md`](dashboard/README.md)
for deployment onto the device and the hero-photo / fonts notes.

> Note: the public demo above **cannot** control a real terrarium — the device
> lives on a private LAN and an HTTPS page can't call a plain-HTTP device. The
> demo is for showing the interface; the real control path runs on the LAN.
