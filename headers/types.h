#ifndef TYPES_H
#define TYPES_H

#include <time.h>

// event categories
typedef enum {
    event_none = 0,
    event_earthquake,
    event_tornado,
    event_drought,
    event_flood,
    event_tsunami,
    event_count
} event_type;

// risk levles
typedef enum {
    risk_normal = 0,
    risk_elevated,
    risk_high,
    risk_critical
} risk_level;

// core sensor reading
typedef struct {
    time_t timestamp;
    double value;
    double secondary;
    double latitude;
    double longitude;
    int sensor_id;
    int sensor_type;
} sensor_reading;

// Processed event
typedef struct {
    event_type type;
    risk_level risk;
    double probability;
    double magnitude;
    double latitude;
    double longitude;
    double affected_radius;
    time_t detection_time;
    time_t estimated_arrival;
    time_t expiry;
} event;

// Callback for event notifications
typedef void (*event_callback)(const event *event);

#endif