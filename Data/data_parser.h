#ifndef DATA_PARSER_H
#define DATA_PARSER_H

#include "seismic.h"
#include "meteorological.h"
#include "hydro.h"
    
// parsează JSON-ul și actualizează sistemele
void parse_and_process(const char *raw_json,
                       seismic_monitor *seismo,
                       weather_monitor *weather,
                       flood_monitor *flood);

#endif