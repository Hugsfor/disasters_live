#include "meteorological.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

// default thresholds
static const weather_thresholds default_wx_thresholds = {
    .max_temperature_change_6h = 15.0,
    .min_dewpoint_spread = 3.0,
    .min_wind_shear = 20.0,
    .drought_days_without_rain = 15,
    .drought_temperature_anomaly = 5.0
};

// emit weather event to callback
static void emit_weather_event(weather_monitor *monitor, event_type type, double probability, double magnitude, time_t timestamp, int duration_seconds) {
    event weather_event;
    memset(&weather_event, 0, sizeof(weather_event));

    weather_event.type = type;
    weather_event.probability = probability;
    weather_event.magnitude = magnitude;
    weather_event.latitude = monitor->config.latitude;
    weather_event.longitude = monitor->config.longitude;
    weather_event.detection_time = timestamp;
    weather_event.expiry = timestamp + duration_seconds;

    // affected radius by event type
    if (type == event_tornado) {
        weather_event.affected_radius = 10.0;
    } else if (type == event_drought) {
        weather_event.affected_radius = 200.0;
    }

    // risk level
    if (probability > 0.8) weather_event.risk = risk_critical;
    else if (probability > 0.6) weather_event.risk = risk_high;
    else if (probability > 0.4) weather_event.risk = risk_elevated;
    else weather_event.risk = risk_normal;

    if (monitor->callback) monitor->callback(&weather_event);
}

void weather_init(weather_monitor *monitor, const weather_config *config, const weather_thresholds *thresholds, event_callback callback) {
    memset(monitor, 0, sizeof(*monitor));

    monitor->config = *config;
    monitor->thresholds = thresholds ? *thresholds : default_wx_thresholds;
    monitor->callback = callback;

    ewma_init(&monitor->temperature_trend, 0.2);
    wind_avg_init(&monitor->wind_direction);
    monitor->min_dewpoint_spread_today = 999.0;
    monitor->last_rainfall_time = 0;
    monitor->last_pressure = 1013.25;     // standard atmosphere
    monitor->pressure_fall_score = 0.0;
}

// feed current temperature reading
void weather_feed_temperature(weather_monitor *monitor, double temperature_celsius, time_t timestamp) {
    // store in circular history buffer
    monitor->temperature_history[monitor->temperature_index] = temperature_celsius;
    monitor->temperature_index = (monitor->temperature_index + 1) % 6;
    if (monitor->temperature_count < 6) monitor->temperature_count++;

    // calculate running average
    double previous_average = monitor->average_temperature;
    if (monitor->temperature_count >= 6) {
        double sum = 0.0;
        for (int i = 0; i < 6; i++) sum += monitor->temperature_history[i];
        monitor->average_temperature = sum / 6.0;
    } else {
        monitor->average_temperature = temperature_celsius;
    }

    // track warming/cooling trend
    ewma_process(&monitor->temperature_trend, temperature_celsius - previous_average);
}

// feed humidity and dewpoint
void weather_feed_humidity(weather_monitor *monitor, double humidity_percent, double dewpoint_celsius, time_t timestamp) {
    monitor->current_dewpoint = dewpoint_celsius;

    // calculate temperature-dewpoint spread
    double last_temperature = monitor->temperature_history[(monitor->temperature_index - 1 + 6) % 6];
    double dewpoint_spread = last_temperature - dewpoint_celsius;

    // track minimum spread (important for severe storm formation)
    if (dewpoint_spread < monitor->min_dewpoint_spread_today) {
        monitor->min_dewpoint_spread_today = dewpoint_spread;
    }
}

// feed wind observations
void weather_feed_wind(weather_monitor *monitor, double wind_speed_ms, double wind_direction_degrees, double wind_gust_ms, time_t timestamp) {
    wind_avg_add(&monitor->wind_direction, wind_direction_degrees);

    if (wind_gust_ms > monitor->max_wind_gust) {
        monitor->max_wind_gust = wind_gust_ms;
    }
}

// feed barometric pressure - falling pressure indicates approaching storms
void weather_feed_pressure(weather_monitor *monitor, double pressure_hpa, double pressure_trend_3h, time_t timestamp) {
    // significant pressure drop increases tornado probability
    if (pressure_trend_3h < -4.0) {
        // rapid pressure fall - strong indicator of severe weather
        monitor->pressure_fall_score = 0.3;
    } else if (pressure_trend_3h < -2.0) {
        // moderate pressure fall
        monitor->pressure_fall_score = 0.15;
    } else {
        monitor->pressure_fall_score = 0.0;
    }

    // store for trend analysis
    monitor->last_pressure = pressure_hpa;
}

// feed rainfall measurement
void weather_feed_rain(weather_monitor *monitor, double rainfall_mm, time_t timestamp) {
    if (rainfall_mm > 0.5) {
        monitor->last_rainfall_time = timestamp;
        monitor->consecutive_dry_days = 0;
    } else {
        monitor->consecutive_dry_days++;
    }
}

// tornado detection:
// 1. strong temperature gradient (cold front meets warm air)
// 2. low dewpoint spread (high surface humidity = storm fuel)
// 3. strong wind gusts (rotating updraft indicator)
// 4. falling barometric pressure (approaching storm system)
void weather_check_tornado(weather_monitor *monitor, time_t now) {
    // check every 5 minutes
    if (now - monitor->last_tornado_check < 300) return;

    double risk_score = 0.0;
    int indicators_present = 0;

    // check temperature change in last 6 hours
    if (monitor->temperature_count >= 6) {
        double oldest_temp = monitor->temperature_history[(monitor->temperature_index) % 6];
        double newest_temp = monitor->temperature_history[(monitor->temperature_index - 1 + 6) % 6];
        double temperature_change = fabs(newest_temp - oldest_temp);

        if (temperature_change > monitor->thresholds.max_temperature_change_6h * 0.5) {
            risk_score += 0.25;
            indicators_present++;
        }
        if (temperature_change > monitor->thresholds.max_temperature_change_6h) {
            risk_score += 0.25;
            indicators_present++;
        }
    }
    // check dewpoint spread (small spread = high humidity)
    if (monitor->min_dewpoint_spread_today < monitor->thresholds.min_dewpoint_spread * 2) {
        risk_score += 0.15;
        indicators_present++;
    }
    if (monitor->min_dewpoint_spread_today < monitor->thresholds.min_dewpoint_spread) {
        risk_score += 0.20;
        indicators_present++;
    }

    // check wind gusts
    if (monitor->max_wind_gust > 20.0) {
        risk_score += 0.15;
        indicators_present++;
    }

    // check pressure fall
    if (monitor->pressure_fall_score > 0.0) {
        risk_score += monitor->pressure_fall_score;
        indicators_present++;
    }

    // emit warning if enough indicators present
    if (indicators_present >= 3 && risk_score > 0.5) {
        emit_weather_event(monitor, event_tornado, risk_score,
                          risk_score * 5.0, now, 1800);
    }

    // reset daily tracking variables
    monitor->min_dewpoint_spread_today = 999.0;
    monitor->max_wind_gust = 0.0;
    monitor->pressure_fall_score = 0.0;
    monitor->last_tornado_check = now;
}

// drought detection:
// 1. extended period without rainfall
// 2. above average temperatures
void weather_check_drought(weather_monitor *monitor, time_t now) {
    // check hourly
    if (now - monitor->last_drought_check < 3600) return;

    double risk_score = 0.0;
    double drought_severity = 0.0;

    // days without rain
    if (monitor->consecutive_dry_days > monitor->thresholds.drought_days_without_rain * 0.5) {
        risk_score += 0.3;
        drought_severity = monitor->consecutive_dry_days / (double)monitor->thresholds.drought_days_without_rain;
    }
    if (monitor->consecutive_dry_days > monitor->thresholds.drought_days_without_rain) {
        risk_score += 0.4;
    }

    // temperature anomaly
    if (monitor->temperature_trend.current > monitor->thresholds.drought_temperature_anomaly * 0.5) {
        risk_score += 0.15;
    }

    if (risk_score > 0.5) {
        emit_weather_event(monitor, event_drought, risk_score, drought_severity, now, 86400);
    }

    monitor->last_drought_check = now;
}

void weather_destroy(weather_monitor *monitor) {}