# 🌍 Disaster Prevention System

> A real-time natural disaster monitoring built in C, with a live web dashboard.

The system continuously collects data from public APIs, processes it through domain-specific detection modules (seismic, meteorological, hydrological), and serves the results to a browser-based interactive map.

---

## 📋 Table of Contents

- [What Is This?](#what-is-this)
- [Features](#features)
- [Project Structure](#project-structure)
- [Getting Started](#getting-started)
- [Configuration](#configuration)
- [Data Sources](#data-sources)
- [Dependencies](#dependencies)
- [Output](#output)
- [Motivation](#motivation)

---

## What Is This?

A local backend + web frontend system for monitoring natural hazard risks over any configurable geographic area. A C backend polls external data sources every 10 seconds, analyzes the readings through specialized detection algorithms, and exposes a JSON API on `localhost:8080`. The frontend — a single HTML page — fetches that data and renders events on an interactive map with color-coded risk levels.

> **Core principle: detect problems early, present them clearly.**

---

## ✨ Features

| Feature | Description |
|---|---|
| 🌋 Multi-hazard monitoring | Tracks earthquakes, floods, tornadoes, droughts, and tsunamis from a single unified pipeline |
| 📡 Live seismic detection | STA/LTA P-wave detector with configurable gain and sample rate; feeds real USGS earthquake data |
| 🌪️ Meteorological analysis | Tornado risk scoring from wind, pressure drop, temperature gradients, and dewpoint spread; drought detection from rainfall history |
| 💧 Hydrological modeling | Runoff and discharge estimation using a soil-storage model; flood alerting with configurable thresholds |
| 🚦 Risk classification | Every event is automatically rated: `normal`, `elevated`, `high`, or `critical` |
| 🗺️ Interactive map | Leaflet.js map with colored circles (affected radius) and icon markers; click any event for details |
| 🔽 Hazard type filters | Toggle buttons to isolate earthquakes, floods, tornadoes, drought, or tsunami events |
| 🔄 Auto-refresh | Frontend polls the backend every 5 seconds — no page reload needed |
| 🧵 Threaded HTTP server | Server runs on a background thread so monitoring is never blocked |
| 🌐 CORS-enabled API | JSON endpoint can be queried from any local web client |

---

## 📁 Project Structure

```
disaster-prevention-system/
│
├── functions/
│   ├── main.c              # Entry point: init, server thread, polling loop
│   ├── seismic.c           # STA/LTA P-wave detector, magnitude estimation
│   ├── meteorological.c    # Tornado and drought detection from weather readings
│   ├── hydro.c             # Flood (runoff/discharge model) and tsunami detection
│   ├── dsp.c               # Signal processing (EWMA, biquad filters, STA/LTA, wind averager)
│   └── predictor.c         # Multi-hazard risk aggregator and alert escalation
│
├── Data/
│   ├── data_ingestor.c     # HTTP GET helper using libcurl
│   ├── data_parser.c       # JSON parsers for Open-Meteo and USGS feeds
│   └── globals.c           # Shared GlobalEvent[200] array and mutex
│
├── headers/
│   ├── global.h            # GlobalEvent struct and extern declarations
│   ├── types.h             # event_type, risk_level, and callback typedefs
│   ├── seismic.h
│   ├── meteorological.h
│   ├── hydro.h
│   ├── predictor.h
│   └── dsp.h
│
├── frontend/
│   └── index.html          # Single-file web dashboard (Leaflet + filters + auto-refresh)
│
├── server.c / server.h     # Lightweight HTTP server; serializes events to JSON
├── input_handler.c / .h    # Input handling utilities
├── geologie.json           # Geological reference data
├── compile.bat             # Windows build script
└── main.exe                # Pre-compiled Windows binary
```

---

## 🚀 Getting Started

### Prerequisites

| Dependency | Purpose |
|---|---|
| GCC (MinGW on Windows) | Compiler |
| libcurl | HTTP requests to external APIs |
| cJSON | JSON parsing |
| pthreads | Background server thread (`winpthreads` on Windows) |

### Build — Windows

```bat
compile.bat
```

Links against `-lcurl -lcjson -lws2_32 -lpthread`.

### Build — Linux / macOS

```bash
gcc functions/main.c functions/seismic.c functions/meteorological.c \
    functions/hydro.c functions/dsp.c functions/predictor.c \
    Data/data_ingestor.c Data/data_parser.c Data/globals.c \
    server.c input_handler.c \
    -Iheaders -Ifunctions -IData \
    -lcurl -lcjson -lpthread -lm \
    -o disaster_monitor
```

### Run

```bash
./disaster_monitor   # Linux/macOS
main.exe             # Windows
```

Then open `frontend/index.html` in a browser — the dashboard connects to `http://localhost:8080` automatically.

---

## ⚙️ Configuration

All sensor parameters are set in `functions/main.c`. Coordinates and station IDs are fully configurable.

```c
// Seismic station
seismic_config seismo_cfg = {
    .station_id  = 1,
    .latitude    = YOUR_LAT,
    .longitude   = YOUR_LON,
    .gain        = 1.0,
    .sample_rate = 100.0
};

// Weather station
weather_config wx_cfg = {
    .station_id = 1,
    .latitude   = YOUR_LAT,
    .longitude  = YOUR_LON
};

// Watershed (flood)
watershed_config ws_cfg = {
    .gauge_id            = 1,
    .catchment_area_km2  = 100.0,
    .max_soil_storage_mm = 50.0,
    .runoff_coefficient  = 0.35,
    .baseflow_m3s        = 2.0
};
```

Detection thresholds (`weather_thresholds`, `flood_limits`, `seismic_thresholds`) are passed as structs to each `_init()` function. Passing `NULL` uses built-in defaults.

API polling interval (default: every 10 seconds):

```c
if (now - last_api_call >= 10) { ... }
```

---

## 🌐 Data Sources

| Source | Endpoint | Data |
|---|---|---|
| [Open-Meteo](https://open-meteo.com) | `api.open-meteo.com` | Temperature, wind speed, precipitation, pressure |
| [USGS Earthquake Hazards](https://earthquake.usgs.gov) | `earthquake.usgs.gov` | All earthquakes in the last hour (GeoJSON) |

Both are **free**, require **no API key**, and are polled every 10 seconds.

---

## 📦 Dependencies

| Library | Version | Purpose |
|---|---|---|
| libcurl | any recent | HTTP GET requests to USGS and Open-Meteo |
| cJSON | any recent | Parsing JSON API responses |
| pthreads | POSIX / winpthreads | Background server thread |
| Leaflet.js | 1.9.4 (CDN) | Interactive map in the browser |
| math.h / time.h | standard C | DSP calculations and timestamps |

---

## 🖥️ Output

Console output while running:

```
HTTP Server running on port 8080
Monitoring system started successfully.
EVENT RECEIVED | type=0 prob=0.82 mag=4.30
[CRITICAL ALERT] Imminent risk detected!
```

Browser dashboard (`frontend/index.html`) shows:

- Colored circles on the map representing each event's affected radius
- Icon markers at the event epicenter/location
- Click popup: type, risk level, probability, magnitude, radius, detection time
- Status bar: active event count + last-refresh timestamp

### Risk Level Color Coding

| Level | Color | Meaning |
|---|---|---|
| `normal` | 🟢 Green | No significant hazard |
| `elevated` | 🟡 Yellow | Monitor conditions |
| `high` | 🟠 Orange | Prepare response actions |
| `critical` | 🔴 Red | Imminent threat |

---

## 💡 Motivation

This project is a proof of concept for what a unified, open-source, data-driven early warning system could look like — one that aggregates multiple data streams, applies domain-specific detection logic, and presents results in a clear visual interface accessible to both technical teams and decision-makers.

The goal is not to replace official infrastructure, but to demonstrate that a lean, real-time monitoring system can be built with open data, open-source libraries, and a small team.
