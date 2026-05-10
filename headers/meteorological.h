#ifndef METEOROLOGICAL_H
#define METEOROLOGICAL_H

#include "types.h"
#include "dsp.h"

// weather station config
typedef struct {
    int station_id;
    double latitude;
    double longitude;
} weather_config;

// conditions for severe weather
typedef struct {
    double max_temperature_change_6h;
    double min_dewpoint_spread;
    double min_wind_shear;
    int drought_days_without_rain;
    double drought_temperature_anomaly;
} weather_thresholds;

// weather monitor
typedef struct {
    weather_config config;
    weather_thresholds thresholds;
    // temperature tracking
    double temperature_history[6];
    int temperature_index;
    int temperature_count;
    double average_temperature;
    ewma temperature_trend;
    // wind tracking
    wind_avg wind_direction;
    double max_wind_gust;
    double wind_speed_upper_atmosphere;
    // humidity tracking
    double current_dewpoint;
    double min_dewpoint_spread_today;
    // pressure tracking
    double last_pressure;
    double pressure_fall_score;
    // precipitation tracking
    time_t last_rainfall_time;
    int consecutive_dry_days;
    // alert state
    event_callback callback;
    time_t last_tornado_check;
    time_t last_drought_check;
} weather_monitor;

void weather_init(weather_monitor *monitor, const weather_config *config, const weather_thresholds *thresholds, event_callback callback);
void weather_feed_temperature(weather_monitor *monitor, double temperature_celsius, time_t timestamp);
void weather_feed_humidity(weather_monitor *monitor, double humidity_percent,double dewpoint_celsius, time_t timestamp);
void weather_feed_wind(weather_monitor *monitor, double wind_speed_ms,double wind_direction_degrees, double wind_gust_ms, time_t timestamp);
void weather_feed_pressure(weather_monitor *monitor, double pressure_hpa,double pressure_trend_3h, time_t timestamp);
void weather_feed_rain(weather_monitor *monitor, double rainfall_mm, time_t timestamp);
void weather_check_tornado(weather_monitor *monitor, time_t now);
void weather_check_drought(weather_monitor *monitor, time_t now);
void weather_destroy(weather_monitor *monitor);

#endif