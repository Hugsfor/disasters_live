#include <pthread.h>
#include "global.h"

GlobalEvent global_events_list[200];
int global_events_count = 0;
pthread_mutex_t events_mutex = PTHREAD_MUTEX_INITIALIZER;