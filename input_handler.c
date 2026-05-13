#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_handler.h"

int get_geo_data(const char* target_location, GeoInput* out_data) {
    FILE *file = fopen("geologie.json", "r");
    if (file == NULL) {
        printf("Eroare: Nu s-a putut deschide geologie.json\n");
        return 0;
    }

    char line[256];
    int found = 0;

    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, target_location) != NULL) {
            found = 1;

            while (fgets(line, sizeof(line), file)) {
                if (strstr(line, "quake_mag")) sscanf(line, " \"quake_mag\": %f,", &out_data->quake_mag);
                if (strstr(line, "depth")) sscanf(line, " \"depth\": %f,", &out_data->depth);
                if (strstr(line, "plate_speed")) sscanf(line, " \"plate_speed\": %f,", &out_data->plate_speed);
                if (strstr(line, "is_oceanic")) sscanf(line, " \"is_oceanic\": %d", &out_data->is_oceanic);
                
                if (strstr(line, "}")) break;
            }
            break; 
        }
    }

    fclose(file);
    return found; 
}
