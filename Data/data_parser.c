#include <stdio.h>
#include <cjson/cJSON.h>
#include <time.h>
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
        return;

    // WEATHER DATA (Open-Meteo format)
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

void parse_earthquake_data(const char *raw_json,
                           seismic_monitor *seismo)
{
    if (!raw_json) return;

    cJSON *json = cJSON_Parse(raw_json);
    if (!json) return;

    cJSON *features =
        cJSON_GetObjectItem(json, "features");

    if (!cJSON_IsArray(features)) {
        cJSON_Delete(json);
        return;
    }

    cJSON *quake = NULL;

    cJSON_ArrayForEach(quake, features) {

        cJSON *properties =
            cJSON_GetObjectItem(quake, "properties");

        if (!properties) continue;

        cJSON *mag =
            cJSON_GetObjectItem(properties, "mag");

        if (cJSON_IsNumber(mag)) {

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
