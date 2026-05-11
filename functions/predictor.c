#include "predictor.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

void predictor_init(predictor *p, event_callback callback) {
    memset(p, 0, sizeof(*p));

    p->callback = callback;
    p->seismic_weight = 0.50;
    p->weather_weight = 0.30;
    p->hydro_weight = 0.20;
    p->assess_interval_seconds = 60;
    p->last_assessment = 0;
}

void predictor_set_weights(predictor *p, double seismic_w, double weather_w, double hydro_w) {
    double total = seismic_w + weather_w + hydro_w;
    if (total > 0.0) {
        p->seismic_weight = seismic_w / total;
        p->weather_weight = weather_w / total;
        p->hydro_weight = hydro_w / total;
    }
}

void predictor_update_earthquake(predictor *p, double risk) {
    if (risk > p->earthquake_risk) p->earthquake_risk = risk;
}

void predictor_update_tornado(predictor *p, double risk) {
    if (risk > p->tornado_risk) p->tornado_risk = risk;
}

void predictor_update_drought(predictor *p, double risk) {
    if (risk > p->drought_risk) p->drought_risk = risk;
}

void predictor_update_flood(predictor *p, double risk) {
    if (risk > p->flood_risk) p->flood_risk = risk;
}

void predictor_update_tsunami(predictor *p, double risk) {
    if (risk > p->tsunami_risk) p->tsunami_risk = risk;
}

// determine which hazard is most likely from combined signals
void predictor_assess(predictor *p, double latitude, double longitude, time_t now) {
    // rate limit assessments
    if (now - p->last_assessment < p->assess_interval_seconds) return;

    // calculate weighted scores for each category
    double seismic_score = p->earthquake_risk * p->seismic_weight + p->tsunami_risk * p->seismic_weight * 0.5;
    double weather_score = p->tornado_risk * p->weather_weight + p->drought_risk * p->weather_weight * 0.3;
    double hydro_score = p->flood_risk * p->hydro_weight + p->tsunami_risk * p->hydro_weight * 0.3;

    // find highest combined threat
    double highest_score = 0.0;
    event_type most_likely = event_none;

    // earthquake dominates seismic category
    if (p->earthquake_risk > 0.3 && p->earthquake_risk >= highest_score) {
        highest_score = p->earthquake_risk;
        most_likely = event_earthquake;
    }

    // tornado from weather
    if (p->tornado_risk > 0.3 && p->tornado_risk > highest_score) {
        highest_score = p->tornado_risk;
        most_likely = event_tornado;
    }

    // drought from weather
    if (p->drought_risk > 0.3 && p->drought_risk > highest_score) {
        highest_score = p->drought_risk;
        most_likely = event_drought;
    }

    // flood from hydro
    if (p->flood_risk > 0.3 && p->flood_risk > highest_score) {
        highest_score = p->flood_risk;
        most_likely = event_flood;
    }

    // tsunami can come from seismic or coastal gauge
    if (p->tsunami_risk > 0.3 && p->tsunami_risk > highest_score) {
        highest_score = p->tsunami_risk;
        most_likely = event_tsunami;
    }

    // emit if significant threat found
    if (highest_score > 0.3 && most_likely != event_none && p->callback) {
        event combined_event;
        memset(&combined_event, 0, sizeof(combined_event));

        combined_event.type = most_likely;
        combined_event.probability = highest_score;
        combined_event.latitude = latitude;
        combined_event.longitude = longitude;
        combined_event.detection_time = now;
        combined_event.expiry = now + 600;

        // magnitude from the highest score
        combined_event.magnitude = highest_score * 10.0;

        // affected radius scales with probability
        combined_event.affected_radius = highest_score * 100.0;
        if (combined_event.affected_radius < 5.0) combined_event.affected_radius = 5.0;

        // risk level
        if (highest_score > 0.8) combined_event.risk = risk_critical;
        else if (highest_score > 0.6) combined_event.risk = risk_high;
        else if (highest_score > 0.4) combined_event.risk = risk_elevated;
        else combined_event.risk = risk_normal;

        p->callback(&combined_event);
    }

    p->last_assessment = now;
}

// reset all signals
void predictor_reset(predictor *p) {
    p->earthquake_risk *= 0.8;
    p->tornado_risk *= 0.7;
    p->drought_risk *= 0.7;
    p->flood_risk *= 0.7;
    p->tsunami_risk *= 0.8;

    // clear very low signals entirely
    if (p->earthquake_risk < 0.05) p->earthquake_risk = 0.0;
    if (p->tornado_risk < 0.05) p->tornado_risk = 0.0;
    if (p->drought_risk < 0.05) p->drought_risk = 0.0;
    if (p->flood_risk < 0.05) p->flood_risk = 0.0;
    if (p->tsunami_risk < 0.05) p->tsunami_risk = 0.0;
}

void predictor_destroy(predictor *p) {}