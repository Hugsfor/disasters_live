#ifndef DATA_PARSER_H
#define DATA_PARSER_H

#include "../headers/seismic.h"
#include "../headers/meteorological.h"
#include "../headers/hydro.h"

void parse_and_process(const char *raw_json,
                       seismic_monitor *seismo,
                       weather_monitor *weather,
                       flood_monitor *flood);

void parse_earthquake_data(const char *raw_json,
                           seismic_monitor *seismo);

void parse_gdacs_json(const char *json);    // GDACS JSON API — floods, tsunamis, cyclones
void parse_gdacs_feed(const char *xml);     // GDACS XML RSS (fallback)
void parse_noaa_alerts(const char *json);   // NOAA NWS alerts — tornadoes, floods
void parse_firms_fire(const char *csv);     // NASA FIRMS — wildfires

#endif
