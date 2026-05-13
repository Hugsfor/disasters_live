#include "hydro.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

// default flood limits
static const flood_limits default_flood_limits = {
    .minor_flood_discharge = 30.0,
    .major_flood_discharge = 70.0,
    .cooldown_minutes = 60
};

// default tsunami limits
static const tsunami_limits default_tsunami_limits = {
    .sea_level_rise_m = 1.0,
    .rapid_rise_rate = 0.5,
    .cooldown_minutes = 120
};

// emit flood event
static void emit_flood_event(flood_monitor *monitor, double probability, double discharge_m3s, time_t timestamp) {
    event flood_event;
    memset(&flood_event, 0, sizeof(flood_event));

    flood_event.type = event_flood;
    flood_event.probability = probability;
    flood_event.magnitude = discharge_m3s;
    flood_event.latitude = monitor->config.latitude;
    flood_event.longitude = monitor->config.longitude;
    flood_event.detection_time = timestamp;
    flood_event.expiry = timestamp + 3600;

    // affected radius based on discharge
    flood_event.affected_radius = sqrt(discharge_m3s) * 2.0;
    if (flood_event.affected_radius < 5.0) flood_event.affected_radius = 5.0;
    if (flood_event.affected_radius > 100.0) flood_event.affected_radius = 100.0;

    // risk level
    if (discharge_m3s > monitor->limits.major_flood_discharge) {
        flood_event.risk = risk_critical;
    } else if (discharge_m3s > monitor->limits.minor_flood_discharge) {
        flood_event.risk = risk_high;
    } else {
        flood_event.risk = risk_elevated;
    }

    if (monitor->callback) monitor->callback(&flood_event);
}

// emit tsunami event
static void emit_tsunami_event(tsunami_monitor *monitor, double probability, double wave_height_m, time_t timestamp, time_t arrival_time) {
    event tsunami_event;
    memset(&tsunami_event, 0, sizeof(tsunami_event));

    tsunami_event.type = event_tsunami;
    tsunami_event.probability = probability;
    tsunami_event.magnitude = wave_height_m;
    tsunami_event.latitude = monitor->config.latitude;
    tsunami_event.longitude = monitor->config.longitude;
    tsunami_event.detection_time = timestamp;
    tsunami_event.estimated_arrival = arrival_time;
    tsunami_event.expiry = arrival_time + 7200;

    // affected radius based on wave height
    tsunami_event.affected_radius = wave_height_m * 50.0;
    if (tsunami_event.affected_radius < 10.0) tsunami_event.affected_radius = 10.0;
    if (tsunami_event.affected_radius > 500.0) tsunami_event.affected_radius = 500.0;

    // risk level
    if (wave_height_m > 5.0) {
        tsunami_event.risk = risk_critical;
    } else if (wave_height_m > 2.0) {
        tsunami_event.risk = risk_high;
    } else {
        tsunami_event.risk = risk_elevated;
    }

    if (monitor->callback) monitor->callback(&tsunami_event);
}

// flood monitor initialization
void flood_init(flood_monitor *monitor, const watershed_config *config, const flood_limits *limits, event_callback callback) {
    memset(monitor, 0, sizeof(*monitor));
    monitor->last_flood_alert = time(NULL);

    monitor->config = *config;
    monitor->limits = limits ? *limits : default_flood_limits;
    monitor->callback = callback;

    ewma_init(&monitor->storage_trend, 0.3);
    monitor->soil_storage_mm = 0.0;
    monitor->cumulative_rainfall = 0.0;
    monitor->hours_since_last_rain = 0;
}

// calculate runoff using storage method
void flood_add_rainfall(flood_monitor *monitor, double rainfall_mm, int duration_minutes, time_t timestamp) {
    // track cumulative rainfall for this storm
    monitor->cumulative_rainfall += rainfall_mm;
    monitor->hours_since_last_rain = 0;

    // effective rainfall
    double effective_rainfall = rainfall_mm * monitor->config.runoff_coefficient;
    double hourly_increment = effective_rainfall / (duration_minutes / 60.0);

    // simulate each hour of rainfall
    int simulation_hours = duration_minutes / 60;
    if (simulation_hours < 1) simulation_hours = 1;

    for (int hour = 0; hour < simulation_hours; hour++) {
        // add water to soil storage
        monitor->soil_storage_mm += hourly_increment;

        // drainage - how fast water leaves the watershed
        double drainage_rate = 12.0;
        double outflow = monitor->soil_storage_mm / drainage_rate;

        monitor->soil_storage_mm -= outflow;
        if (monitor->soil_storage_mm < 0.0) monitor->soil_storage_mm = 0.0;
    }

    // track storage trend
    ewma_process(&monitor->storage_trend, monitor->soil_storage_mm);

    // calculate discharge
    double discharge = flood_get_discharge(monitor);

    // check flood limits
    if (discharge > monitor->limits.minor_flood_discharge && (timestamp - monitor->last_flood_alert) > monitor->limits.cooldown_minutes * 60) {

        double probability = (discharge - monitor->limits.minor_flood_discharge) / (monitor->limits.major_flood_discharge - monitor->limits.minor_flood_discharge);
        if (probability > 1.0) probability = 1.0;
        if (probability < 0.3) probability = 0.3;

        emit_flood_event(monitor, probability, discharge, timestamp);
        monitor->last_flood_alert = timestamp;
    }
}

// return current soil storage as percentage of maximum
double flood_get_storage_pct(const flood_monitor *monitor) {
    if (monitor->config.max_soil_storage_mm <= 0.0) return 0.0;
    return monitor->soil_storage_mm / monitor->config.max_soil_storage_mm;
}

// calculate current river discharge
double flood_get_discharge(const flood_monitor *monitor) {
    double drainage_rate = 12.0;
    double outflow_mm_per_hour = monitor->soil_storage_mm / drainage_rate;

    // convert mm/hour over catchment to m3/s
    double outflow_m3_per_hour = outflow_mm_per_hour * monitor->config.catchment_area_km2 * 1000.0;
    double discharge_m3s = outflow_m3_per_hour / 3600.0;

    return discharge_m3s + monitor->config.baseflow_m3s;
}

void flood_destroy(flood_monitor *monitor) {}

// tsunami monitor initialization
void tsunami_init(tsunami_monitor *monitor, const tsunami_config *config, const tsunami_limits *limits, event_callback callback) {
    memset(monitor, 0, sizeof(*monitor));

    monitor->config = *config;
    monitor->limits = limits ? *limits : default_tsunami_limits;
    monitor->callback = callback;

    ewma_init(&monitor->sea_level_trend, 0.4);
    monitor->baseline_sea_level = 0.0;
    monitor->history_index = 0;
    monitor->history_count = 0;
}

// feed sea level reading from coastal gauge
void tsunami_feed_sea_level(tsunami_monitor *monitor, double sea_level_m, time_t timestamp) {
    // store in circular history
    monitor->sea_level_history[monitor->history_index] = sea_level_m;
    monitor->history_index = (monitor->history_index + 1) % 30;
    if (monitor->history_count < 30) monitor->history_count++;

    // establish baseline from first readings
    if (monitor->history_count < 5) {
        double sum = 0.0;
        for (int i = 0; i < monitor->history_count; i++) {
            sum += monitor->sea_level_history[i];
        }
        monitor->baseline_sea_level = sum / monitor->history_count;
        return;
    }

    // track sea level trend
    double sea_level_change = sea_level_m - monitor->baseline_sea_level;
    double trend_rate = ewma_process(&monitor->sea_level_trend, sea_level_change);

    // detect rapid rise above normal tide range
    if (sea_level_change > monitor->limits.sea_level_rise_m && trend_rate > monitor->limits.rapid_rise_rate && (timestamp - monitor->last_tsunami_alert) > monitor->limits.cooldown_minutes * 60) {

        // estimate wave height from sea level anomaly
        double estimated_wave_height = sea_level_change * 2.0;

        // tsunami waves travel at ~800 km/h
        time_t arrival_time = timestamp;

        double probability = trend_rate / (monitor->limits.rapid_rise_rate * 3.0);
        if (probability > 1.0) probability = 1.0;

        emit_tsunami_event(monitor, probability, estimated_wave_height, timestamp, arrival_time);
        monitor->last_tsunami_alert = timestamp;
    }

    // slowly update baseline during normal conditions
    if (fabs(sea_level_change) < monitor->config.normal_tide_range_m) {
        monitor->baseline_sea_level = 0.95 * monitor->baseline_sea_level + 0.05 * sea_level_m;
    }
}

void tsunami_destroy(tsunami_monitor *monitor) {}
void flood_trigger_event(flood_monitor *monitor, double probability)
{
    if (!monitor || !monitor->callback) return;

    event e;
    memset(&e, 0, sizeof(e));

    e.type = event_flood;
    e.probability = probability;
    e.magnitude = probability * 10.0;

    e.latitude = monitor->config.latitude;
    e.longitude = monitor->config.longitude;

    e.detection_time = time(NULL);
    e.estimated_arrival = 0;
    e.expiry = e.detection_time + 3600;

    e.affected_radius = probability * 50.0;

    e.risk =
        (probability > 0.75) ? risk_critical :
        (probability > 0.50) ? risk_high :
        (probability > 0.25) ? risk_elevated :
                               risk_normal;

    monitor->callback(&e);
}