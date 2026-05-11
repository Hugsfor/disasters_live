#ifndef HYDRO_H
#define HYDRO_H

#include "types.h"
#include "dsp.h"

// waters config for flood prediction
typedef struct {
    int gauge_id;
    double latitude;
    double longitude;
    double catchment_area_km2;
    double max_soil_storage_mm;
    double runoff_coefficient;
    double baseflow_m3s;
} watershed_config;

// flood limits
typedef struct {
    double minor_flood_discharge;
    double major_flood_discharge;
    int cooldown_minutes;
} flood_limits;

// flood monitor using storage function method
typedef struct {
    watershed_config config;
    flood_limits limits;
    double soil_storage_mm;
    double cumulative_rainfall;
    int hours_since_last_rain;
    ewma storage_trend;
    event_callback callback;
    time_t last_flood_alert;
} flood_monitor;

// coastal gauge for tsunami detection
typedef struct {
    int gauge_id;
    double latitude;
    double longitude;
    double normal_tide_range_m;
} tsunami_config;

// tsunami limits
typedef struct {
    double sea_level_rise_m;
    double rapid_rise_rate;
    int cooldown_minutes;
} tsunami_limits;

// tsunami monitor from coastal sea level gauges
typedef struct {
    tsunami_config config;
    tsunami_limits limits;
    double sea_level_history[30];
    int history_index;
    int history_count;
    double baseline_sea_level;
    ewma sea_level_trend;
    event_callback callback;
    time_t last_tsunami_alert;
} tsunami_monitor;

void flood_init(flood_monitor *monitor, const watershed_config *config, const flood_limits *limits, event_callback callback);
void flood_add_rainfall(flood_monitor *monitor, double rainfall_mm, int duration_minutes, time_t timestamp);
double flood_get_storage_pct(const flood_monitor *monitor);
double flood_get_discharge(const flood_monitor *monitor);
void flood_destroy(flood_monitor *monitor);
void tsunami_init(tsunami_monitor *monitor, const tsunami_config *config, const tsunami_limits *limits, event_callback callback);
void tsunami_feed_sea_level(tsunami_monitor *monitor, double sea_level_m, time_t timestamp);
void tsunami_destroy(tsunami_monitor *monitor);

#endif