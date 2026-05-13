#ifndef GLOBAL_H
#define GLOBAL_H

#include <pthread.h>

// =====================
// EVENT STRUCT
// =====================
typedef struct
{
    char type[20];
    double latitude;
    double longitude;
    double probability;
    double magnitude;
    double affected_radius;
} GlobalEvent;

// =====================
// GLOBAL DATA
// =====================
extern GlobalEvent global_events_list[200];
extern int global_events_count;
extern pthread_mutex_t events_mutex;

#endif