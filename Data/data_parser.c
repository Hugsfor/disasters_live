#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <cjson/cJSON.h>
#include "global.h"

#include "../headers/seismic.h"
#include "../headers/meteorological.h"
#include "../headers/hydro.h"

// Write an event directly into the shared list the server reads.
void push_global_event(const char *type,
                               double lat, double lon,
                               double probability,
                               double magnitude,
                               double affected_radius_km)
{
    pthread_mutex_lock(&events_mutex);
    if (global_events_count < 200) {
        GlobalEvent *ge = &global_events_list[global_events_count];
        strncpy(ge->type, type, sizeof(ge->type) - 1);
        ge->type[sizeof(ge->type) - 1] = '\0';
        ge->latitude        = lat;
        ge->longitude       = lon;
        ge->probability     = probability;
        ge->magnitude       = magnitude;
        ge->affected_radius = affected_radius_km;
        global_events_count++;
        printf("PUSH EVENT | %s lat=%.4f lon=%.4f mag=%.1f prob=%.2f r=%.0fkm\n",
               type, lat, lon, magnitude, probability, affected_radius_km);
    }
    pthread_mutex_unlock(&events_mutex);
}

// ======================================================
// WEATHER / FLOOD — open-meteo
// ======================================================

void parse_and_process(const char *raw_json,
                       seismic_monitor *seismo,
                       weather_monitor *weather,
                       flood_monitor *flood)
{
    if (!raw_json) return;

    cJSON *json = cJSON_Parse(raw_json);
    if (!json) { printf("JSON parse error (weather)\n"); return; }

    cJSON *lat_node = cJSON_GetObjectItem(json, "latitude");
    cJSON *lon_node = cJSON_GetObjectItem(json, "longitude");
    double lat = 47.01, lon = 28.86;

    if (cJSON_IsNumber(lat_node) && cJSON_IsNumber(lon_node)) {
        lat = lat_node->valuedouble;
        lon = lon_node->valuedouble;
        weather->config.latitude  = lat;
        weather->config.longitude = lon;
        flood->config.latitude    = lat;
        flood->config.longitude   = lon;
    }

    cJSON *current = cJSON_GetObjectItem(json, "current");
    if (current) {
        double temp = 0, wind = 0, pressure = 1013, rain = 0;
        int has_temp = 0, has_rain = 0;
        cJSON *j;

        j = cJSON_GetObjectItem(current, "temperature_2m");
        if (cJSON_IsNumber(j)) { temp = j->valuedouble; has_temp = 1; weather_feed_temperature(weather, temp, time(NULL)); }

        j = cJSON_GetObjectItem(current, "wind_speed_10m");
        if (cJSON_IsNumber(j)) { wind = j->valuedouble; weather_feed_wind(weather, wind, 180.0, wind, time(NULL)); }

        j = cJSON_GetObjectItem(current, "pressure_msl");
        if (cJSON_IsNumber(j)) { pressure = j->valuedouble; weather_feed_pressure(weather, pressure, -1.0, time(NULL)); }

        j = cJSON_GetObjectItem(current, "precipitation");
        if (cJSON_IsNumber(j)) {
            rain = j->valuedouble; has_rain = 1;
            weather_feed_rain(weather, rain, time(NULL));
            flood_add_rainfall(flood, rain, 60, time(NULL));
        }

        if (has_rain && rain > 5.0) {
            double prob = fmin(rain / 50.0, 1.0);
            push_global_event("flood", lat, lon, prob, rain, sqrt(rain) * 10.0);
        }
        if (wind > 20.0 && pressure < 990.0) {
            double prob = fmin((wind - 20.0) / 60.0 + (990.0 - pressure) / 50.0, 1.0);
            push_global_event("tornado", lat, lon, prob, wind / 10.0, 50.0);
        }
        if (has_temp && temp > 35.0 && weather->consecutive_dry_days > 7) {
            double prob = fmin((temp - 35.0) / 15.0 + weather->consecutive_dry_days / 30.0, 1.0);
            push_global_event("drought", lat, lon, prob, temp / 10.0, 200.0);
        }
    }

    cJSON_Delete(json);
}

// ======================================================
// EARTHQUAKE — USGS GeoJSON feed
// ======================================================

void parse_earthquake_data(const char *raw_json, seismic_monitor *seismo)
{
    if (!raw_json) return;

    cJSON *json = cJSON_Parse(raw_json);
    if (!json) { printf("JSON parse error (earthquake)\n"); return; }

    cJSON *features = cJSON_GetObjectItem(json, "features");
    if (!cJSON_IsArray(features)) { cJSON_Delete(json); return; }

    int pushed = 0;
    cJSON *quake = NULL;
    cJSON_ArrayForEach(quake, features) {
        cJSON *props    = cJSON_GetObjectItem(quake, "properties");
        cJSON *geometry = cJSON_GetObjectItem(quake, "geometry");
        if (!props || !geometry) continue;

        cJSON *mag   = cJSON_GetObjectItem(props, "mag");
        cJSON *coords = cJSON_GetObjectItem(geometry, "coordinates");
        if (!cJSON_IsNumber(mag) || !cJSON_IsArray(coords)) continue;

        cJSON *lon_item = cJSON_GetArrayItem(coords, 0);
        cJSON *lat_item = cJSON_GetArrayItem(coords, 1);
        if (!cJSON_IsNumber(lon_item) || !cJSON_IsNumber(lat_item)) continue;

        double magnitude = mag->valuedouble;
        if (magnitude < 1.0) continue;

        double probability     = fmin(magnitude / 10.0 + 0.2, 0.95);
        double affected_radius = pow(10.0, 0.5 * magnitude - 0.5);
        if (affected_radius < 10.0)   affected_radius = 10.0;
        if (affected_radius > 1000.0) affected_radius = 1000.0;

        push_global_event("earthquake",
                          lat_item->valuedouble, lon_item->valuedouble,
                          probability, magnitude, affected_radius);
        pushed++;
    }

    printf("Parsed %d earthquakes from USGS\n", pushed);
    cJSON_Delete(json);
}

// ======================================================
// GDACS FEED — floods, tsunamis, cyclones, droughts
// https://www.gdacs.org/xml/rss.xml  (no key needed)
// ======================================================

void parse_gdacs_feed(const char *xml)
{
    if (!xml) return;
    const char *ptr = xml;

    while ((ptr = strstr(ptr, "<item>")) != NULL) {
        ptr += 6;
        const char *item_end = strstr(ptr, "</item>");
        if (!item_end) break;

        char item_buf[4096] = {0};
        int item_len = (int)(item_end - ptr);
        if (item_len >= (int)sizeof(item_buf)) item_len = sizeof(item_buf) - 1;
        strncpy(item_buf, ptr, item_len);

        // Flexible event type detection
        char event_code[8] = {0};
        const char *et = strstr(item_buf, "eventtype>");
        if (et) {
            et = strchr(et, '>') + 1;
            int k = 0;
            while (*et && *et != '<' && k < 7) event_code[k++] = *et++;
        }

        const char *type = NULL;
        if      (strcmp(event_code, "FL") == 0) type = "flood";
        else if (strcmp(event_code, "TS") == 0) type = "tsunami";
        else if (strcmp(event_code, "TC") == 0) type = "tornado";
        else if (strcmp(event_code, "DR") == 0) type = "drought";
        else { ptr = item_end; continue; }

        // Flexible coordinate detection
        double lat = 0.0, lon = 0.0;
        const char *lat_tag = strstr(item_buf, "lat>");
        const char *lon_tag = strstr(item_buf, "long>");

        if (lat_tag && lon_tag) {
            lat = atof(strchr(lat_tag, '>') + 1);
            lon = atof(strchr(lon_tag, '>') + 1);
        } else {
            const char *geopoint = strstr(item_buf, "point>");
            if (geopoint) {
                geopoint = strchr(geopoint, '>') + 1;
                lat = atof(geopoint);
                const char *space = strchr(geopoint, ' ');
                if (space) lon = atof(space + 1);
            } else { ptr = item_end; continue; }
        }

        // Alert level
        double prob = 0.4;
        if (strstr(item_buf, "Red"))         prob = 0.90;
        else if (strstr(item_buf, "Orange")) prob = 0.65;
        else if (strstr(item_buf, "Green"))  prob = 0.35;

        push_global_event(type, lat, lon, prob, 5.0, 150.0);
        ptr = item_end;
    }
}

// ======================================================
// NOAA NWS ACTIVE ALERTS — tornadoes, floods, hurricanes
// https://api.weather.gov/alerts/active  (no key needed)
// ======================================================

void parse_noaa_alerts(const char *json)
{
    if (!json) return;
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    cJSON *features = cJSON_GetObjectItem(root, "features");
    if (!cJSON_IsArray(features)) { cJSON_Delete(root); return; }

    cJSON *feature;
    cJSON_ArrayForEach(feature, features) {
        cJSON *props = cJSON_GetObjectItem(feature, "properties");
        cJSON *geom  = cJSON_GetObjectItem(feature, "geometry");
        if (!props || !geom) continue;

        cJSON *event_node = cJSON_GetObjectItem(props, "event");
        cJSON *type_node  = cJSON_GetObjectItem(geom,  "type");
        cJSON *coords     = cJSON_GetObjectItem(geom,  "coordinates");
        if (!cJSON_IsString(event_node) || !cJSON_IsString(type_node)) continue;

        const char *event_str = event_node->valuestring;
        const char *type = NULL;
        if      (strstr(event_str, "Tornado"))   type = "tornado";
        else if (strstr(event_str, "Flood"))     type = "flood";
        else if (strstr(event_str, "Hurricane")) type = "tornado";
        else continue;

        double lat = 0, lon = 0;
        if (strcmp(type_node->valuestring, "Point") == 0) {
            cJSON *c0 = cJSON_GetArrayItem(coords, 0);
            cJSON *c1 = cJSON_GetArrayItem(coords, 1);
            if (!cJSON_IsNumber(c0) || !cJSON_IsNumber(c1)) continue;
            lon = c0->valuedouble;
            lat = c1->valuedouble;
        } else if (strcmp(type_node->valuestring, "Polygon") == 0) {
            cJSON *ring = cJSON_GetArrayItem(coords, 0);
            if (!cJSON_IsArray(ring)) continue;
            int n = cJSON_GetArraySize(ring);
            if (n == 0) continue;
            double s_lat = 0, s_lon = 0;
            cJSON *pt;
            cJSON_ArrayForEach(pt, ring) {
                cJSON *px = cJSON_GetArrayItem(pt, 0);
                cJSON *py = cJSON_GetArrayItem(pt, 1);
                if (cJSON_IsNumber(px) && cJSON_IsNumber(py)) {
                    s_lon += px->valuedouble;
                    s_lat += py->valuedouble;
                }
            }
            lat = s_lat / n;
            lon = s_lon / n;
        } else continue;

        double prob = 0.5;
        cJSON *sev = cJSON_GetObjectItem(props, "severity");
        if (cJSON_IsString(sev)) {
            if      (strcmp(sev->valuestring, "Extreme")  == 0) prob = 0.95;
            else if (strcmp(sev->valuestring, "Severe")   == 0) prob = 0.75;
            else if (strcmp(sev->valuestring, "Moderate") == 0) prob = 0.55;
            else if (strcmp(sev->valuestring, "Minor")    == 0) prob = 0.35;
        }

        push_global_event(type, lat, lon, prob, prob * 5.0,
                          strcmp(type, "tornado") == 0 ? 30.0 : 80.0);
    }

    cJSON_Delete(root);
}

// ======================================================
// NASA FIRMS — active wildfires (drought proxy)
// Requires free MAP_KEY from https://firms.modaps.eosdis.nasa.gov/api/
// ======================================================

void parse_firms_fire(const char *csv)
{
    if (!csv) return;
    const char *line = strstr(csv, "\n");
    if (!line) return;
    line++;

    int count = 0;
    while (*line && count < 50) {
        char row[512];
        int i = 0;
        while (*line && *line != '\n' && i < 511) row[i++] = *line++;
        if (*line == '\n') line++;
        row[i] = '\0';
        if (i == 0) continue;

        // Manual CSV split — avoids strtok side effects
        char *fields[15] = {0};
        int f_idx = 0;
        char *tmp = row;
        fields[f_idx++] = tmp;
        while ((tmp = strchr(tmp, ',')) && f_idx < 15) {
            *tmp = '\0';
            fields[f_idx++] = ++tmp;
        }
        if (f_idx < 10) continue;

        double lat = atof(fields[0]);
        double lon = atof(fields[1]);
        double frp = (f_idx >= 13) ? atof(fields[12]) : 10.0;
        const char *conf = fields[9];

        // Skip low-confidence detections
        if (strncasecmp(conf, "l", 1) == 0) continue;

        push_global_event("drought", lat, lon, 0.7, frp / 100.0, 20.0);
        count++;
    }

    printf("Parsed %d fire events from NASA FIRMS\n", count);
}

// ======================================================
// GDACS JSON API — floods, tsunamis, cyclones, droughts
// https://www.gdacs.org/gdacsapi/api/events/geteventlist/SEARCH
// This is the official GDACS REST API, much more reliable than
// the XML RSS feed because it returns clean JSON and doesn't
// require a browser User-Agent to serve correctly.
//
// Response structure:
// { "features": [ { "properties": { "eventtype": "FL",
//     "alertlevel": "Orange", "country": "...",
//     "fromdate": "...", "todate": "..." },
//   "geometry": { "coordinates": [lon, lat] } } ] }
// ======================================================

void parse_gdacs_json(const char *json)
{
    if (!json) return;

    cJSON *root = cJSON_Parse(json);
    if (!root) { printf("JSON parse error (GDACS)\n"); return; }

    cJSON *features = cJSON_GetObjectItem(root, "features");
    if (!cJSON_IsArray(features)) {
        // Try alternate structure: { "results": [...] }
        features = cJSON_GetObjectItem(root, "results");
    }
    if (!cJSON_IsArray(features)) {
        printf("GDACS: no features array found\n");
        cJSON_Delete(root);
        return;
    }

    int pushed = 0;
    cJSON *feature = NULL;

    cJSON_ArrayForEach(feature, features)
    {
        cJSON *props    = cJSON_GetObjectItem(feature, "properties");
        cJSON *geometry = cJSON_GetObjectItem(feature, "geometry");
        if (!props) props = feature; // flat structure fallback

        // Event type
        cJSON *etype = cJSON_GetObjectItem(props, "eventtype");
        if (!cJSON_IsString(etype)) continue;

        const char *type = NULL;
        if      (strcmp(etype->valuestring, "FL") == 0) type = "flood";
        else if (strcmp(etype->valuestring, "TS") == 0) type = "tsunami";
        else if (strcmp(etype->valuestring, "TC") == 0) type = "tornado";
        else if (strcmp(etype->valuestring, "DR") == 0) type = "drought";
        else if (strcmp(etype->valuestring, "WF") == 0) type = "drought"; // wildfire
        else continue;

        // Coordinates
        double lat = 0.0, lon = 0.0;
        int got_coords = 0;

        if (geometry) {
            cJSON *coords = cJSON_GetObjectItem(geometry, "coordinates");
            if (cJSON_IsArray(coords)) {
                cJSON *c0 = cJSON_GetArrayItem(coords, 0);
                cJSON *c1 = cJSON_GetArrayItem(coords, 1);
                if (cJSON_IsNumber(c0) && cJSON_IsNumber(c1)) {
                    lon = c0->valuedouble;
                    lat = c1->valuedouble;
                    got_coords = 1;
                }
            }
        }

        // Fallback: lat/lon directly in properties
        if (!got_coords) {
            cJSON *plat = cJSON_GetObjectItem(props, "latitude");
            cJSON *plon = cJSON_GetObjectItem(props, "longitude");
            if (cJSON_IsNumber(plat) && cJSON_IsNumber(plon)) {
                lat = plat->valuedouble;
                lon = plon->valuedouble;
                got_coords = 1;
            }
        }

        if (!got_coords) continue;

        // Alert level → probability
        double prob = 0.4;
        cJSON *alert = cJSON_GetObjectItem(props, "alertlevel");
        if (cJSON_IsString(alert)) {
            if      (strcasecmp(alert->valuestring, "Red")    == 0) prob = 0.90;
            else if (strcasecmp(alert->valuestring, "Orange") == 0) prob = 0.65;
            else if (strcasecmp(alert->valuestring, "Green")  == 0) prob = 0.35;
        }

        // Severity value if available
        double magnitude = 5.0;
        cJSON *sev = cJSON_GetObjectItem(props, "severitydata");
        if (sev) {
            cJSON *sval = cJSON_GetObjectItem(sev, "severity");
            if (cJSON_IsNumber(sval)) magnitude = sval->valuedouble;
        }

        double radius = 150.0;
        if (strcmp(type, "tsunami") == 0) radius = 300.0;
        if (strcmp(type, "tornado") == 0) radius = 50.0;

        push_global_event(type, lat, lon, prob, magnitude, radius);
        pushed++;
    }

    printf("Parsed %d events from GDACS JSON API\n", pushed);
    cJSON_Delete(root);
}