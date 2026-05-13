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
            weather_feed_rain(weather,
                              rain->valuedouble,
                              time(NULL));

            flood_add_rainfall(flood,
                               rain->valuedouble,
                               60,
                               time(NULL));
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

            seismic_feed(
                seismo,
                magnitude,
                magnitude,
                magnitude,
                time(NULL)
            );
        }
    }

    cJSON_Delete(json);
}