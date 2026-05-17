#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <pthread.h>
#define usleep(usec) Sleep((usec) / 1000)
#else
#include <unistd.h>
#include <pthread.h>
#endif

#include "../headers/global.h"
#include "types.h"
#include "seismic.h"
#include "meteorological.h"
#include "hydro.h"
#include "predictor.h"
#include "../Data/data_ingestor.h"
#include "../Data/data_parser.h"
#include "../server.h"

double global_earthquake_risk = 0.0;
double global_flood_risk      = 0.0;
static predictor global_predictor;

void on_event(const event *e) {
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
            push_global_event("tsunami",
                e->latitude, e->longitude,
                e->probability, e->magnitude, e->affected_radius);
            break;
        default: break;
    }
}

#ifdef _WIN32
DWORD WINAPI server_thread(LPVOID arg) { start_server(); return 0; }
#else
void* server_thread(void* arg) { start_server(); return NULL; }
#endif

// Fetch a URL and print the first 300 chars of the response for debugging
char* safe_http_get(const char* url) {
    printf("  GET %s\n", url);
    char* result = http_get(url);
    if (!result) {
        printf("  -> FAILED (null response)\n");
        return NULL;
    }
    printf("  -> OK %lu bytes | first 200 chars: %.200s\n",
           (unsigned long)strlen(result), result);
    return result;
}

int main(void) {
    predictor_init(&global_predictor, on_event);
    predictor_set_weights(&global_predictor, 0.50, 0.30, 0.20);

    const double DEFAULT_LAT = 47.01;
    const double DEFAULT_LON = 28.86;

    seismic_config seismo_cfg = { .station_id=1, .latitude=DEFAULT_LAT, .longitude=DEFAULT_LON, .gain=1.0, .sample_rate=100.0 };
    seismic_monitor seismo;
    seismic_init(&seismo, &seismo_cfg, NULL, on_event);

    weather_config wx_cfg = { .station_id=1, .latitude=DEFAULT_LAT, .longitude=DEFAULT_LON };
    weather_monitor weather;
    weather_init(&weather, &wx_cfg, NULL, on_event);

    watershed_config ws_cfg = { .gauge_id=1, .latitude=DEFAULT_LAT, .longitude=DEFAULT_LON,
                                 .catchment_area_km2=100.0, .max_soil_storage_mm=50.0,
                                 .runoff_coefficient=0.35, .baseflow_m3s=2.0 };
    flood_monitor flood;
    flood_init(&flood, &ws_cfg, NULL, on_event);

    tsunami_config ts_cfg = { .gauge_id=1, .latitude=DEFAULT_LAT, .longitude=DEFAULT_LON, .normal_tide_range_m=0.5 };
    tsunami_monitor tsunami;
    tsunami_init(&tsunami, &ts_cfg, NULL, on_event);

#ifdef _WIN32
    CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
#else
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, server_thread, NULL);
    pthread_detach(thread_id);
#endif

    printf("========================================\n");
    printf("Sistemul de monitorizare a pornit.\n");
    printf("DEBUG MODE: printing first 200 chars of each API response\n");
    printf("========================================\n\n");

    int cycle = 0;

    while (1) {
        time_t now = time(NULL);
        static time_t last_fast   = 0;
        static time_t last_noaa   = 0;
        static time_t last_gdacs  = 0;
        static time_t last_firms  = 0;
        static time_t last_assess = 0;

        if (now - last_fast >= 10) {
            cycle++;
            printf("\n=== Cycle #%d | events in list: %d ===\n", cycle, global_events_count);

            pthread_mutex_lock(&events_mutex);
            if (global_events_count > 150) global_events_count = 0;
            pthread_mutex_unlock(&events_mutex);

            char weather_url[512];
            snprintf(weather_url, sizeof(weather_url),
                "https://api.open-meteo.com/v1/forecast?"
                "latitude=%.4f&longitude=%.4f&"
                "current=temperature_2m,wind_speed_10m,precipitation,pressure_msl",
                weather.config.latitude, weather.config.longitude);

            char *json_data  = safe_http_get(weather_url);
            char *quake_data = safe_http_get(
                "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_hour.geojson");

            if (json_data)  { parse_and_process(json_data, &seismo, &weather, &flood); free(json_data); }
            if (quake_data) { parse_earthquake_data(quake_data, &seismo); free(quake_data); }

            
            weather_check_drought(&weather, now);
            last_fast = now;
        }

        if (now - last_noaa >= 60) {
            printf("\n--- NOAA alerts fetch ---\n");
            char *noaa = safe_http_get(
                "https://api.weather.gov/alerts/active?status=actual");
            if (noaa) { parse_noaa_alerts(noaa); free(noaa); }
            last_noaa = now;
        }

        if (now - last_gdacs >= 600) {
            printf("\n--- GDACS fetch ---\n");
            char *gdacs = safe_http_get(
                "https://www.gdacs.org/gdacsapi/api/events/geteventlist/SEARCH?"
                "eventtypes=FL,TS,TC,DR&alertlevel=Green,Orange,Red&limit=50");
            if (gdacs) { parse_gdacs_json(gdacs); free(gdacs); }
            last_gdacs = now;
        }

        if (now - last_firms >= 600) {
            const char *key = getenv("FIRMS_API_KEY");
            if (key && strlen(key) > 0) {
                printf("\n--- NASA FIRMS fetch ---\n");
                char firms_url[512];
                snprintf(firms_url, sizeof(firms_url),
                    "https://firms.modaps.eosdis.nasa.gov/api/area/csv/%s"
                    "/VIIRS_SNPP_NRT/-180,-90,180,90/1", key);
                char *firms = safe_http_get(firms_url);
                if (firms) { parse_firms_fire(firms); free(firms); }
            } else {
                printf("No FIRMS_API_KEY — wildfire monitoring disabled.\n");
            }
            last_firms = now;
        }

        if (now - last_assess >= 60) {
            predictor_assess(&global_predictor,
                             weather.config.latitude, weather.config.longitude, now);
            if (global_earthquake_risk > 0.75 || global_flood_risk > 0.75)
                printf("[ALERTA CRITICA] Risc iminent!\n");
            predictor_reset(&global_predictor);
            last_assess = now;
        }

        usleep(10000);
    }

    seismic_destroy(&seismo);
    weather_destroy(&weather);
    flood_destroy(&flood);
    tsunami_destroy(&tsunami);
    predictor_destroy(&global_predictor);
    return 0;
}
