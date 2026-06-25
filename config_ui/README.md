# ESP32 Configuration UI

Modern React-based single-page application for ESP32 device configuration.

## Features

- 8-step configuration wizard
- Slide animations between pages
- WiFi connection testing with live feedback
- Multi-language support (English, Hungarian)
- Mobile-friendly responsive design

## Development

### Prerequisites
- Node.js 18+
- npm or yarn

### Setup

```bash
npm install
```

### Development Server

```bash
npm run dev
```

Open http://192.168.4.1 in your browser when connected to the ESP32 AP.

### Build

```bash
npm run build
```

Builds to `dist/` directory.

### Build and Copy to LittleFS

```bash
npm run build-to-data
```

This will:
1. Build the React app with Vite
2. Copy optimized files to `../data/config-app/`
3. Ready for PlatformIO `buildfs`

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
