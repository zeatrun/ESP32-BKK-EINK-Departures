# Config UI — React Configuration Wizard

React + TypeScript single-page application that runs on the ESP32 as an in-browser configuration wizard. It is served from LittleFS when the device is in AP (config) mode.

## What It Does

- 8-step configuration wizard served at `http://192.168.4.1`
- Animated slide transitions between pages
- Live WiFi connection test with real feedback from the device
- Weather and departures data source testing
- Location search with Open-Meteo geocoding
- Multi-language UI (English / Hungarian) — language selection is the first step
- Mobile-friendly layout (designed for phone browser on AP join)

## Project Structure

```
config_ui/
  src/
    types.ts                  — shared TypeScript types and all UI translations
    App.tsx                   — wizard state machine, page routing, slide animation
    main.tsx                  — React entry point
    api/                      — API calls to the ESP32 HTTP endpoints
    components/
      pages/
        LanguagePage.tsx      — step 1: language selection
        GeneralPage.tsx       — step 2: timezone and general settings
        WiFiPage.tsx          — step 3: WiFi SSID / password
        WiFiTestPage.tsx      — step 4: live WiFi connection test
        WeatherPage.tsx       — step 5: weather source, provider, location
        DeparturesPage.tsx    — step 6: departures source, provider, stop IDs
        LayoutPage.tsx        — step 7: display layout options
        SummaryPage.tsx       — step 8: review and save
    styles/                   — CSS
  scripts/
    copy-to-data.js           — post-build script: copies dist/ → ../data/config-app/
  vite.config.ts              — Vite config (output: ../data/config-app/, minifier: terser)
  package.json
  tsconfig.json
```

## Prerequisites

- Node.js 18 or newer
- npm

## Setup

Install dependencies (only needed once, or after `package.json` changes):

```bash
cd config_ui
npm install
```

## Development Server

```bash
npm run dev
```

Vite starts a local dev server (default: `http://localhost:5173`). API calls to `/api/*` are proxied to `http://192.168.4.1`, so you can develop while connected to the ESP32 AP.

## Build

```bash
npm run build
```

Produces a minified build directly in `../data/config-app/` (configured in `vite.config.ts`). The output is immediately ready for PlatformIO `buildfs` — no separate copy step needed.

> **Note:** `terser` is required for minification (`npm install` covers this). If you see a "terser not found" error, run `npm install` again.

## Integration with PlatformIO

The PlatformIO build script (`scripts/build_react.py`) runs `npm run build` automatically before `buildfs`. You do not need to build the React app manually before a filesystem upload.

Full filesystem update flow from the project root:

```bash
platformio run --target buildfs
platformio run --target uploadfs
```

Or use the **PlatformIO: Build Filesystem Image** task in VS Code, which triggers both the React build and the LittleFS image creation.

## Translations / i18n

All UI strings live in `src/types.ts` in the `translations` object, keyed by language (`hu` / `en`). Each language object must contain the same set of keys — TypeScript enforces this via `TranslationKey`.

Key naming conventions:
- WiFi-test-specific labels use the `wifiTest*` prefix (`wifiTesting`, `wifiTestSuccess`, `wifiTestFailed`) to avoid collision with the generic test labels used on the Weather and Departures pages (`testing`, `testSuccess`, `testFailed`).

To add a new language:
1. Add a new entry to the `translations` object in `types.ts`.
2. Add the language option to `LanguagePage.tsx` and the `Language` type.


## Configuration Steps

1. **Language Selection** - Choose EN or HU
2. **General Settings** - Timezone selection
3. **WiFi Settings** - Enter SSID and password
4. **WiFi Test** - Live connection feedback
5. **Weather Settings** - API or MQTT source
6. **Departures Settings** - BKK or MQTT source
7. **Layout Settings** - Display configuration
8. **Summary** - Review and save

## API Integration

The app communicates with ESP32 endpoints:

- `GET /api/geocode?q=query` - Location search
- `POST /api/wifi-test` - Test WiFi connection
- `POST /api/config/save` - Save configuration
- `POST /api/config/reset` - Factory reset
- `GET /` - Serve the app (redirect or fallback to old HTML)

## Building for Production

The Vite build minifies and optimizes all assets. Total size should be < 500KB for LittleFS.

## ESP32 Configuration Mode Requirements

When the app is running:
- ESP32 must run in AP mode (access point)
- SSID: `ESP32-CONFIG` or similar
- Optional: Also connect to WiFi (STA) for testing
