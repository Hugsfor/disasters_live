#include "seismic.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

// default thresholds
static const seismic_thresholds default_thresholds = {
    .pwave_ratio = 3.0,
    .swave_ratio = 8.0,
    .min_magnitude = 3.0,
    .cooldown_seconds = 30
};

// estimate magnitude from peak ground acceleration
static double estimate_magnitude(double pga_cm_s2, double distance_km) {
    if (pga_cm_s2 <= 0.0) return 0.0;
    double geometric_spreading = log10(distance_km + 10.0);
    double mag = log10(pga_cm_s2) + geometric_spreading - 1.5;
    return mag;
}

// emit an event to the callback
static void emit_event(seismic_monitor *m, event_type type, double probability, double magnitude, time_t now, time_t arrival, int duration) {
    event e;
    memset(&e, 0, sizeof(e));

    e.type = type;
    e.probability = probability;
    e.magnitude = magnitude;
    e.latitude = m->config.latitude;
    e.longitude = m->config.longitude;
    e.detection_time = now;
    e.estimated_arrival = arrival;
    e.expiry = now + duration;

    // affected radius grows with magnitude
    e.affected_radius = pow(10.0, 0.5 * magnitude - 1.0);
    if (e.affected_radius < 5.0) e.affected_radius = 5.0;

    // risk level from magnitude and certainty
    if (magnitude >= 7.0 && probability > 0.7) {
        e.risk = risk_critical;
    } else if (magnitude >= 6.0 && probability > 0.5) {
        e.risk = risk_high;
    } else if (magnitude >= 4.0 || probability > 0.6) {
        e.risk = risk_elevated;
    } else {
        e.risk = risk_normal;
    }

    if (m->callback) m->callback(&e);
}

void seismic_init(seismic_monitor *m, const seismic_config *config, const seismic_thresholds *thresholds, event_callback cb) {
    memset(m, 0, sizeof(*m));

    m->config = *config;
    m->thresholds = thresholds ? *thresholds : default_thresholds;
    m->callback = cb;

    // p-wave filter: 1-5 Hz
    biquad_bandpass(&m->pwave_filter, 1.0, 5.0, config->sample_rate);
    // s-wave filter: 0.1-1 Hz
    biquad_bandpass(&m->swave_filter, 0.1, 1.0, config->sample_rate);

    // sta/lta: 0.5 second short window, 30 second long window
    int sta_samples = (int)(0.5 * config->sample_rate);
    int lta_samples = (int)(30.0 * config->sample_rate);
    stalta_init(&m->detector, sta_samples, lta_samples);

    ewma_init(&m->amplitude_smoother, 0.3);
}

void seismic_feed(seismic_monitor *m, double x_accel, double y_accel, double z_accel, time_t timestamp) {
    // apply sensor gain
    x_accel *= m->config.gain;
    y_accel *= m->config.gain;
    z_accel *= m->config.gain;

    // vector magnitude of acceleration
    double magnitude = sqrt(x_accel*x_accel + y_accel*y_accel + z_accel*z_accel);

    // convert to cm/s2 for magnitude estimation
    double pga_cm_s2 = magnitude * 981.0;

    // smooth the amplitude
    double smoothed_amp = ewma_process(&m->amplitude_smoother, magnitude);

    // track peak
    if (smoothed_amp > m->max_amplitude) {
        m->max_amplitude = smoothed_amp;
        m->peak_ground_acceleration = pga_cm_s2;
    }

    // filter for p-wave detection
    double pwave = biquad_process(&m->pwave_filter, magnitude);

    // run sta/lta
    double ratio = stalta_process(&m->detector, pwave);

    // p-wave detection - early warning
    if (ratio > m->thresholds.pwave_ratio && !m->event_in_progress && (timestamp - m->last_pwave_alert) > m->thresholds.cooldown_seconds) {

        // estimate distance from amplitude
        double est_distance = 50.0 / (smoothed_amp + 0.001);
        if (est_distance > 300.0) est_distance = 300.0;
        if (est_distance < 10.0) est_distance = 10.0;

        double est_magnitude = estimate_magnitude(m->peak_ground_acceleration, est_distance);

        if (est_magnitude >= m->thresholds.min_magnitude) {
            // s-wave arrives ~8 seconds per 100km
            time_t s_arrival = timestamp + (time_t)(est_distance * 8.0 / 100.0);

            emit_event(m, event_earthquake,(ratio - m->thresholds.pwave_ratio) /(m->thresholds.swave_ratio - m->thresholds.pwave_ratio), est_magnitude, timestamp, s_arrival, 120);

            m->last_pwave_alert = timestamp;
            m->event_in_progress = 1;
            m->quake_start_time = timestamp;

            // tsunami check: magnitude > 7.0 underwater triggers tsunami
            if (est_magnitude >= 7.0) {
                m->seafloor_displacement = est_magnitude - 6.0;
            }
        }
    }

    // s-wave arrival
    if (ratio > m->thresholds.swave_ratio && m->event_in_progress && (timestamp - m->last_swave_alert) > m->thresholds.cooldown_seconds) {

        double est_distance = 30.0 / (smoothed_amp + 0.001);
        if (est_distance > 200.0) est_distance = 200.0;
        if (est_distance < 5.0) est_distance = 5.0;

        double est_magnitude = estimate_magnitude(m->peak_ground_acceleration, est_distance);

        emit_event(m, event_earthquake, 1.0, est_magnitude,timestamp, timestamp, 300);

        // tsunami alert if large undersea quake
        if (m->seafloor_displacement > 1.0) {
            // tsunami travels at ~800 km/h
            double coastal_distance = 100.0;
            time_t tsunami_arrival = timestamp + (time_t)(coastal_distance * 3600.0 / 800.0);

            emit_event(m, event_tsunami, 0.85, m->seafloor_displacement * 3.0, timestamp, tsunami_arrival, 3600);
        }

        m->last_swave_alert = timestamp;
        m->event_in_progress = 0;

        // reset for next event
        stalta_reset(&m->detector);
        m->max_amplitude = 0.0;
        m->peak_ground_acceleration = 0.0;
        m->seafloor_displacement = 0.0;
    }

    // event ended naturally
    if (ratio < 1.5 && m->event_in_progress &&
        (timestamp - m->last_pwave_alert) > 60) {
        m->event_in_progress = 0;
    }
}

double seismic_get_pga(const seismic_monitor *m) {
    return m->peak_ground_acceleration;
}

int seismic_event_active(const seismic_monitor *m) {
    return m->event_in_progress;
}

void seismic_destroy(seismic_monitor *m) {
    stalta_free(&m->detector);
}