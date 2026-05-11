#ifndef PREDICTOR_H
#define PREDICTOR_H

#include "types.h"

// combined threat info
typedef struct {
    // current risk levels from each fisaster
    double earthquake_risk;
    double tornado_risk;
    double drought_risk;
    double flood_risk;
    double tsunami_risk;
    // signal weights
    double seismic_weight;
    double weather_weight;
    double hydro_weight;

    // tracking
    event_callback callback;
    time_t last_assessment;
    int assess_interval_seconds;
} predictor;

void predictor_init(predictor *p, event_callback callback);
void predictor_set_weights(predictor *p, double seismic_w, double weather_w, double hydro_w);
// update risk signals from each disaster
void predictor_update_earthquake(predictor *p, double risk);
void predictor_update_tornado(predictor *p, double risk);
void predictor_update_drought(predictor *p, double risk);
void predictor_update_flood(predictor *p, double risk);
void predictor_update_tsunami(predictor *p, double risk);

// run  and emit highest threat
void predictor_assess(predictor *p, double latitude, double longitude, time_t now);

// reset all signals
void predictor_reset(predictor *p);
void predictor_destroy(predictor *p);

#endif