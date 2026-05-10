#ifndef SEISMIC_H
#define SEISMIC_H

#include "types.h"
#include "dsp.h"

// station configuration
typedef struct {
    int station_id;
    double latitude;
    double longitude;
    double gain;
    double sample_rate;
} seismic_config;

// detection thresholds
typedef struct {
    double pwave_ratio;
    double swave_ratio;
    double min_magnitude;
    int cooldown_seconds;
} seismic_thresholds;

// earthquake and tsunami monitor
typedef struct {
    seismic_config config;
    seismic_thresholds thresholds;
    filter pwave_filter;
    filter swave_filter;
    stalta detector;
    ewma amplitude_smoother;
    event_callback callback;
    time_t last_pwave_alert;
    time_t last_swave_alert;
    double max_amplitude;
    double peak_ground_acceleration;
    int event_in_progress;
    double seafloor_displacement; // tsunami specific
    time_t quake_start_time;
} seismic_monitor;

void seismic_init(seismic_monitor *m, const seismic_config *config,const seismic_thresholds *thresholds, event_callback cb);
void seismic_feed(seismic_monitor *m, double x_accel, double y_accel, double z_accel, time_t timestamp);
double seismic_get_pga(const seismic_monitor *m);
int seismic_event_active(const seismic_monitor *m);
void seismic_destroy(seismic_monitor *m);

#endif