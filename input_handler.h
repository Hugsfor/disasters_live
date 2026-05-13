#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

// (din JSON)
typedef struct {
    float quake_mag;
    float depth;
    float plate_speed;
    int is_oceanic;
} GeoInput;

// (Meteo)
typedef struct {
    float temp;
    float precip;
    float humidity;
    float pressure;
} WeatherInput;

typedef struct {
    char location_name[50];
    GeoInput geo;
    WeatherInput weather;
} FinalDataPackage;

#endif
