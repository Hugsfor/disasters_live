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
        cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
        if (cJSON_IsNumber(temp))
        {
            weather_feed_temperature(weather, temp->valuedouble, time(NULL));
        }

        cJSON *wind = cJSON_GetObjectItem(current, "wind_speed_10m");
        if (cJSON_IsNumber(wind))
        {
            weather_feed_wind(weather, wind->valuedouble, 180.0, wind->valuedouble, time(NULL));
        }

        cJSON *pressure = cJSON_GetObjectItem(current, "pressure_msl");
        if (cJSON_IsNumber(pressure))
        {
            weather_feed_pressure(weather, pressure->valuedouble, -1.0, time(NULL));
        }

        cJSON *rain = cJSON_GetObjectItem(current, "precipitation");
        if (cJSON_IsNumber(rain))
        {
            weather_feed_rain(weather, rain->valuedouble, time(NULL));

            flood_add_rainfall(flood, rain->valuedouble, 60, time(NULL));
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

        // ======================================================
        // PUSH EVENT (SAFE)
        // ======================================================

        pthread_mutex_lock(&events_mutex);

        if (global_events_count < 200)
        {
            GlobalEvent *e = &global_events_list[global_events_count];

            memset(e, 0, sizeof(GlobalEvent));

            strcpy(e->type, "earthquake");
            e->latitude = temp_quake_lat;
            e->longitude = temp_quake_lon;
            e->probability = (magnitude > 5.0) ? 0.9 : 0.6;
            e->magnitude = magnitude;
            e->affected_radius = magnitude * 10.0;

            global_events_count++;
        }

        pthread_mutex_unlock(&events_mutex);
    }

    cJSON_Delete(json);
}