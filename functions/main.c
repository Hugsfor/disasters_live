#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define usleep(usec) Sleep((usec) / 1000)
#else
#include <unistd.h>
#include <pthread.h>
#endif

#include "types.h"
#include "seismic.h"
#include "meteorological.h"
#include "hydro.h"
#include "predictor.h"
//+++
#include "../Data/data_ingestor.h"
#include "../Data/data_parser.h"
#include "../server.h"

double global_earthquake_risk = 0.0;
double global_flood_risk = 0.0;
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
            global_earthquake_risk = e->probability; 
            break;
        case event_tornado:
            predictor_update_tornado(&global_predictor, e->probability);
            break;
        case event_drought:
            predictor_update_drought(&global_predictor, e->probability);
            break;
        case event_flood:
            predictor_update_flood(&global_predictor, e->probability);
            global_flood_risk = e->probability;
            break;
        case event_tsunami:
            predictor_update_tsunami(&global_predictor, e->probability);
            break;
        default:
            break;
    }
}

#ifdef _WIN32
DWORD WINAPI server_thread(LPVOID arg) {
    start_server();
    return 0;
}
#else
void* server_thread(void* arg) {
    start_server();
    return NULL;
}
#endif

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

        // 3. Pornire Thread în mod securizat în funcție de OS
    #ifdef _WIN32
        CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
    #else
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, server_thread, NULL);
        pthread_detach(thread_id); // Lasă thread-ul să ruleze independent
    #endif

    printf("Sistemul de monitorizare a pornit cu succes.\n");

    while (1) {
    time_t now = time(NULL);

    // 1. API CALL
    static time_t last_api_call = 0;

    if (now - last_api_call >= 10) {

        char *json_data =
            http_get("https://api.open-meteo.com/v1/forecast?latitude=35&longitude=-97&current=temperature_2m,precipitation,pressure_msl,wind_speed_10m");

        char *quake_data =
        http_get("https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_hour.geojson");

        if (json_data) {
            parse_and_process( json_data, &seismo, &weather, &flood);
            free(json_data);
        }

        if (quake_data) {
            parse_earthquake_data( quake_data, &seismo);
            free(quake_data);
        }

    last_api_call = now;
    }

    // 2. predictor
    static time_t last_assessment = 0;
    if (now - last_assessment >= 60) {
        predictor_assess(&global_predictor, 34.05, -118.25, now);
        if (global_earthquake_risk > 0.75 || global_flood_risk > 0.75) {
            printf("[ALERTA CRITICA] Risc iminent detectat la coordonatele specificate!\n");
        }
        predictor_reset(&global_predictor);
        last_assessment = now;
    }

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
