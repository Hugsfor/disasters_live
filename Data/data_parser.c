#include <stdio.h>
#include <cjson/cJSON.h>

#include "../headers/seismic.h"
#include "../headers/meteorological.h"
#include "../headers/hydro.h"

void parse_and_process(const char *raw_json,
                       seismic_monitor *seismo,
                       weather_monitor *weather,
                       flood_monitor *flood)
{
    if (!raw_json) return;

    cJSON *json = cJSON_Parse(raw_json);
    if (!json) return;

    // 🔥 WEATHER DATA (Open-Meteo format)
    cJSON *current = cJSON_GetObjectItem(json, "current");
    if (current) {

        cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
        if (cJSON_IsNumber(temp)) {
            weather_feed_temperature(weather, temp->valuedouble, time(NULL));
        }

        cJSON *wind = cJSON_GetObjectItem(current, "wind_speed_10m");
        if (cJSON_IsNumber(wind)) {
            weather_feed_wind(weather, wind->valuedouble, 180.0, wind->valuedouble, time(NULL));
        }

        cJSON *pressure = cJSON_GetObjectItem(current, "pressure_msl");
        if (cJSON_IsNumber(pressure)) {
            weather_feed_pressure(weather, pressure->valuedouble, -1.0, time(NULL));
        }

        cJSON *rain = cJSON_GetObjectItem(current, "precipitation");
        if (cJSON_IsNumber(rain)) {
            weather_feed_rain(weather, rain->valuedouble, time(NULL));
        }
    }

    cJSON_Delete(json);
}