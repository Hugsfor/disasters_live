#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "types.h"
#include "seismic.h"
#include "meteorological.h"
#include "hydro.h"
#include "predictor.h"

// global predictor for all subsystems to share
static predictor global_predictor;

// called by every subsystem when they detect something
void on_event(const event *e) {
    // push to database
    
    // push to message queue
    
    // feed into predictor for combined assessment
    switch (e->type) {
        case event_earthquake:
            predictor_update_earthquake(&global_predictor, e->probability);
            break;
        case event_tornado:
            predictor_update_tornado(&global_predictor, e->probability);
            break;
        case event_drought:
            predictor_update_drought(&global_predictor, e->probability);
            break;
        case event_flood:
            predictor_update_flood(&global_predictor, e->probability);
            break;
        case event_tsunami:
            predictor_update_tsunami(&global_predictor, e->probability);
            break;
        default:
            break;
    }
}

int main(void) {
    // init predictor
    predictor_init(&global_predictor, on_event);
    predictor_set_weights(&global_predictor, 0.50, 0.30, 0.20);

    // init seismic monitor
    seismic_config seismo_cfg = {
        .station_id = 1,
        .latitude = 34.05,
        .longitude = -118.25,
        .gain = 1.0,
        .sample_rate = 100.0
    };
    seismic_monitor seismo;
    seismic_init(&seismo, &seismo_cfg, NULL, on_event);

    // init weather monitor
    weather_config wx_cfg = {
        .station_id = 1,
        .latitude = 35.20,
        .longitude = -97.44
    };
    weather_monitor weather;
    weather_init(&weather, &wx_cfg, NULL, on_event);

    // init flood monitor
    watershed_config ws_cfg = {
        .gauge_id = 1,
        .latitude = 34.05,
        .longitude = -118.25,
        .catchment_area_km2 = 100.0,
        .max_soil_storage_mm = 50.0,
        .runoff_coefficient = 0.35,
        .baseflow_m3s = 2.0
    };
    flood_monitor flood;
    flood_init(&flood, &ws_cfg, NULL, on_event);

    // init tsunami monitor
    tsunami_config ts_cfg = {
        .gauge_id = 1,
        .latitude = 33.85,
        .longitude = -118.40,
        .normal_tide_range_m = 2.0
    };
    tsunami_monitor tsunami;
    tsunami_init(&tsunami, &ts_cfg, NULL, on_event);


    while (1) {
        time_t now = time(NULL);

   // make the connections






        static time_t last_assessment = 0;
        if (now - last_assessment >= 60) {
            predictor_assess(&global_predictor, 34.05, -118.25, now);
            predictor_reset(&global_predictor);
            last_assessment = now;
        }

        // small sleep to prevent busy loop
        usleep(10000);
    }

    // cleanup
    seismic_destroy(&seismo);
    weather_destroy(&weather);
    flood_destroy(&flood);
    tsunami_destroy(&tsunami);
    predictor_destroy(&global_predictor);

    return 0;
}