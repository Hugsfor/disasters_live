# Disaster Prevention System

A real-time natural disaster monitoring and early-warning platform built in C, with a live web dashboard. The system continuously collects data from public APIs, processes it through domain-specific detection modules (seismic, meteorological, hydrological), and serves the results to a browser-based interactive map.

---

## What Is This?

This is a local backend + web frontend system designed to monitor natural hazard risks for Moldova and surrounding regions. A C backend polls external data sources every 10 seconds, analyzes the readings through specialized detection algorithms, and exposes a JSON API on `localhost:8080`. The frontend — a single HTML page — fetches that data and renders events on an interactive map with color-coded risk levels.

The system is designed around one principle: **detect problems early, present them clearly.**

---

## Features

- **Multi-hazard monitoring** — tracks earthquakes, floods, tornadoes, droughts, and tsunamis from a single unified pipeline
- **Live seismic detection** — STA/LTA P-wave detector with configurable gain and sample rate; feeds real USGS earthquake data
- **Meteorological analysis** — tornado risk scoring from wind, pressure drop, temperature gradients, and dewpoint spread; drought detection from rainfall history and temperature anomalies
- **Hydrological modeling** — runoff and discharge estimation using a soil-storage model; flood alerting with configurable minor/major thresholds
- **Risk classification** — every event is automatically rated: `normal`, `elevated`, `high`, or `critical`
- **Interactive map** — Leaflet.js map with colored circles (affected radius) and icon markers; click any event for details
- **Filter by hazard type** — toggle buttons to isolate earthquakes, floods, tornadoes, drought, or tsunami events
- **Auto-refresh** — frontend polls the backend every 5 seconds; no page reload needed
- **Threaded HTTP server** — the C backend runs a lightweight HTTP server on a background thread so monitoring is never blocked
- **CORS-enabled API** — the JSON endpoint can be queried from any local web client

---

## Project Structure

```
moldova weather system/
│
├── functions/
│   ├── main.c              # Entry point: initializes all monitors, starts server thread, runs polling loop
│   ├── seismic.c           # STA/LTA P-wave detector, magnitude estimation, seismic event emission
│   ├── meteorological.c    # Tornado and drought detection from weather readings
│   ├── hydro.c             # Flood (runoff/discharge model) and tsunami detection
│   ├── dsp.c               # Signal processing utilities (EWMA, biquad filters, STA/LTA, wind averager)
│   └── predictor.c         # Multi-hazard risk aggregator and alert escalation
│
├── Data/
│   ├── data_ingestor.c     # HTTP GET helper using libcurl (fetches API responses)
│   ├── data_parser.c       # JSON parsers for Open-Meteo weather and USGS earthquake feeds
│   └── globals.c           # Shared global event list (GlobalEvent[200]) and mutex
│
├── headers/
│   ├── global.h            # GlobalEvent struct and extern declarations for shared state
│   ├── seismic.h
│   ├── meteorological.h
│   ├── hydro.h
│   ├── predictor.h
│   ├── dsp.h
│   └── types.h             # event, event_type, risk_level, and callback typedefs
│
├── frontend/
│   └── index.html          # Single-file web dashboard (Leaflet map + filter buttons + auto-refresh)
│
├── server.c                # Minimal HTTP server; reads global_events_list and serializes to JSON
├── server.h
├── input_handler.c / .h    # (Input handling utilities)
├── geologie.json           # Local geological reference data for Moldova
├── compile.bat             # Windows build script (gcc + libcurl + cjson + pthreads)
└── main.exe                # Pre-compiled Windows binary
```

---

## Getting Started

### Prerequisites

| Dependency | Purpose |
|---|---|
| GCC (MinGW on Windows) | Compiler |
| libcurl | HTTP requests to external APIs |
| cJSON | JSON parsing |
| pthreads | Background server thread (use `winpthreads` on Windows) |

### Build (Windows)

```bat
compile.bat
```

The script compiles all `.c` files and links against `-lcurl -lcjson -lws2_32 -lpthread`.

### Build (Linux/macOS)

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
./disaster_monitor        # Linux/macOS
main.exe                  # Windows
```

Then open `frontend/index.html` in a browser. The dashboard will connect to `http://localhost:8080` automatically.

---

## Output

When running, the backend prints to the console:

```
HTTP Server running on port 8080
Sistemul de monitorizare a pornit cu succes.
EVENT RECEIVED | type=0 prob=0.82 mag=4.30
[ALERTA CRITICA] Risc iminent detectat!
```

The frontend at `localhost:8080` (via the browser) shows:
- Colored circles on the map representing the affected radius of each event
- Icon markers at the event epicenter/location
- A popup on click with type, risk level, probability, magnitude, radius, and detection time
- A status bar with active event count and last-refresh timestamp

---

## Configuration

All sensor parameters are set in `functions/main.c` during initialization:

```c
// Seismic station — position and gain
seismic_config seismo_cfg = {
    .station_id  = 1,
    .latitude    = 47.01,   // Moldova
    .longitude   = 28.86,
    .gain        = 1.0,
    .sample_rate = 100.0
};

// Weather station
weather_config wx_cfg = {
    .station_id = 1,
    .latitude   = 47.01,
    .longitude  = 28.86
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

Detection thresholds (e.g. `weather_thresholds`, `flood_limits`, `seismic_thresholds`) can be passed as custom structs to each `_init()` function; passing `NULL` uses the built-in defaults.

The API polling interval is set in `main.c`:

```c
if (now - last_api_call >= 10) { ... }   // every 10 seconds
```

---

## Dependencies

| Library | Version | Why |
|---|---|---|
| **libcurl** | any recent | HTTP GET requests to USGS and Open-Meteo |
| **cJSON** | any recent | Parsing JSON API responses |
| **pthreads** | POSIX / winpthreads | Server runs on a background thread |
| **Leaflet.js** | 1.9.4 (CDN) | Interactive map in the browser |
| **math.h / time.h** | standard C | DSP calculations and timestamps |

---

## Data Sources

| Source | URL | Data |
|---|---|---|
| Open-Meteo | `api.open-meteo.com` | Temperature, wind speed, precipitation, pressure — Moldova coordinates |
| USGS Earthquake Hazards | `earthquake.usgs.gov` | All earthquakes in the last hour (GeoJSON) |

Both are free, require no API key, and are polled every 10 seconds.

---


## Motivation


This project was built as a proof of concept for what a unified, open-source, data-driven early warning system could look like — one that aggregates multiple data streams, applies domain-specific detection logic, and presents results in a clear visual interface accessible to both technical teams and decision-makers.

The goal is not to replace official infrastructure, but to demonstrate that a lean, real-time monitoring system can be built with open data, open-source libraries, and a small team.
