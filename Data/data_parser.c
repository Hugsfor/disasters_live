#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <cjson/cJSON.h>
#include "global.h"

#include "../headers/seismic.h"
#include "../headers/meteorological.h"
#include "../headers/hydro.h"

// ======================================================
// GLOBAL COORDS
// ======================================================

double temp_quake_lat = 0.0;
double temp_quake_lon = 0.0;

// externs (IMPORTANT)
extern GlobalEvent global_events_list[200];
extern int global_events_count;
extern pthread_mutex_t events_mutex;

// ======================================================
// HELPER: push event into global list
// ======================================================

static void push_global_event(const char *type,
                               double lat, double lon,
                               double probability,
                               double magnitude,
                               double affected_radius)
{
    pthread_mutex_lock(&events_mutex);
    if (global_events_count < 200)
    {
        GlobalEvent *e = &global_events_list[global_events_count];
        memset(e, 0, sizeof(GlobalEvent));
        strncpy(e->type, type, sizeof(e->type) - 1);
        e->latitude        = lat;
        e->longitude       = lon;
        e->probability     = probability;
        e->magnitude       = magnitude;
        e->affected_radius = affected_radius;
        global_events_count++;
    }
    pthread_mutex_unlock(&events_mutex);
}

// ======================================================
// WEATHER + FLOOD
// ======================================================

void parse_and_process(const char *raw_json,
                       seismic_monitor *seismo,
                       weather_monitor *weather,
                       flood_monitor *flood)
{
    if (!raw_json)
        return;

    cJSON *json = cJSON_Parse(raw_json);
    if (!json)
    {
        printf("JSON parse error (weather)\n");
        return;
    }

    cJSON *current = cJSON_GetObjectItem(json, "current");

    if (current)
    {
        double lat = 47.01;  // Chisinau - locatia statiei meteo
        double lon = 28.86;

        cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
        if (cJSON_IsNumber(temp))
        {
            weather_feed_temperature(weather, temp->valuedouble, time(NULL));
        }

        cJSON *wind = cJSON_GetObjectItem(current, "wind_speed_10m");
        double wind_speed = 0.0;
        if (cJSON_IsNumber(wind))
        {
            wind_speed = wind->valuedouble;
            weather_feed_wind(weather, wind_speed, 180.0, wind_speed, time(NULL));
        }

        cJSON *pressure = cJSON_GetObjectItem(current, "pressure_msl");
        double pressure_val = 1013.0;
        if (cJSON_IsNumber(pressure))
        {
            pressure_val = pressure->valuedouble;
            weather_feed_pressure(weather, pressure_val, -1.0, time(NULL));
        }

        cJSON *rain = cJSON_GetObjectItem(current, "precipitation");
        double rain_val = 0.0;
        if (cJSON_IsNumber(rain))
        {
            rain_val = rain->valuedouble;
            weather_feed_rain(weather, rain_val, time(NULL));
            flood_add_rainfall(flood, rain_val, 60, time(NULL));
        }

        // ======================================================
        // Tornado: vant puternic + presiune joasa
        // ======================================================
        if (wind_speed > 20.0 || pressure_val < 990.0)
        {
            double tornado_prob = 0.0;
            if (wind_speed > 40.0) tornado_prob = 0.85;
            else if (wind_speed > 28.0) tornado_prob = 0.65;
            else if (wind_speed > 20.0) tornado_prob = 0.40;
            if (pressure_val < 980.0) tornado_prob += 0.15;
            if (tornado_prob > 1.0) tornado_prob = 1.0;

            if (tornado_prob > 0.1)
            {
                printf("Tornado event | wind=%.1f km/h pressure=%.1f hPa prob=%.2f\n",
                       wind_speed, pressure_val, tornado_prob);
                push_global_event("tornado", lat, lon,
                                  tornado_prob,
                                  wind_speed / 10.0,
                                  wind_speed * 0.5);
            }
        }

        // ======================================================
        // Flood: precipitatii mari
        // ======================================================
        if (rain_val > 0.5)
        {
            double flood_prob = 0.0;
            if (rain_val > 20.0) flood_prob = 0.90;
            else if (rain_val > 10.0) flood_prob = 0.70;
            else if (rain_val > 5.0) flood_prob = 0.50;
            else if (rain_val > 2.0) flood_prob = 0.35;
            else flood_prob = 0.20;

            printf("Flood event | rain=%.1f mm prob=%.2f\n", rain_val, flood_prob);
            push_global_event("flood", lat, lon,
                              flood_prob,
                              rain_val / 5.0,
                              rain_val * 2.0);
        }

        // ======================================================
        // Drought: temperatura mare, fara precipitatii
        // ======================================================
        if (cJSON_IsNumber(temp) && rain_val < 0.1)
        {
            double t = temp->valuedouble;
            double drought_prob = 0.0;
            if (t > 38.0) drought_prob = 0.80;
            else if (t > 33.0) drought_prob = 0.55;
            else if (t > 28.0) drought_prob = 0.30;

            if (drought_prob > 0.1)
            {
                printf("Drought event | temp=%.1f C prob=%.2f\n", t, drought_prob);
                push_global_event("drought", lat, lon,
                                  drought_prob,
                                  t / 10.0,
                                  50.0 + t);
            }
        }
    }

    cJSON_Delete(json);
}

// ======================================================
// EARTHQUAKE
// ======================================================

void parse_earthquake_data(const char *raw_json,
                           seismic_monitor *seismo)
{
    if (!raw_json)
        return;

    cJSON *json = cJSON_Parse(raw_json);
    if (!json)
    {
        printf("JSON parse error (earthquake)\n");
        return;
    }

    cJSON *features = cJSON_GetObjectItem(json, "features");
    if (!cJSON_IsArray(features))
    {
        printf("No earthquake features\n");
        cJSON_Delete(json);
        return;
    }

    cJSON *quake = NULL;

    cJSON_ArrayForEach(quake, features)
    {
        cJSON *properties = cJSON_GetObjectItem(quake, "properties");
        cJSON *geometry = cJSON_GetObjectItem(quake, "geometry");

        if (!properties || !geometry)
            continue;

        cJSON *mag = cJSON_GetObjectItem(properties, "mag");
        cJSON *coordinates = cJSON_GetObjectItem(geometry, "coordinates");

        if (!cJSON_IsNumber(mag) || !cJSON_IsArray(coordinates))
            continue;

        double magnitude = mag->valuedouble;
        if (magnitude <= 0.0)
            continue;

        cJSON *lon = cJSON_GetArrayItem(coordinates, 0);
        cJSON *lat = cJSON_GetArrayItem(coordinates, 1);

        if (!lon || !lat)
            continue;

        temp_quake_lat = lat->valuedouble;
        temp_quake_lon = lon->valuedouble;

        printf("Earthquake | MAG=%.2f LAT=%.4f LON=%.4f\n",
               magnitude, temp_quake_lat, temp_quake_lon);

        seismic_feed(seismo, magnitude, magnitude, magnitude, time(NULL));

        push_global_event("earthquake",
                          temp_quake_lat, temp_quake_lon,
                          (magnitude > 5.0) ? 0.9 : 0.6,
                          magnitude,
                          magnitude * 10.0);
    }

    cJSON_Delete(json);
}