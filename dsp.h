#ifndef DSP_H
#define DSP_H

// Digital Signal Processing module - filtering and detection algorithms for sensor data

// bandpass filter for frequency isolation
typedef struct {
    double x[3]; // input sample history
    double y[3]; // output sample history
    double b[3]; // feedforward
    double a[2]; // feedback
} filter;

// STA/LTA trigger for signal detection
typedef struct {
    double *sta_buffer;  // short-term average window
    double *lta_buffer;  // long-term average window
    int sta_size; // nr of samples in STA window
    int lta_size; // nr of samples in LTA window
    int write_idx; //  write position in circular buffers
    int filled; // whether buffers are fully populated
    double sta_sum; // running sum for STA
    double lta_sum; // running sum for LTA
} stalta;

// exponentially weighted moving average for smoothing
typedef struct {
    double alpha; // smoothing factor
    double current; // current smoothed value
    int initialized;
} ewma;

// wind direction averaging
typedef struct {
    double sin_sum;
    double cos_sum;
    int count;
} wind_avg;

void biquad_bandpass(filter *f, double low_cutoff, double high_cutoff, double sample_rate);
double biquad_process(filter *f, double sample);

int stalta_init(stalta *d, int sta_samples, int lta_samples);
double stalta_process(stalta *d, double sample);
void stalta_reset(stalta *d);
void stalta_free(stalta*d);

void ewma_init(ewma *e, double alpha);
double ewma_process(ewma *e, double sample);

void wind_avg_init(wind_avg *w);
void wind_avg_add(wind_avg *w, double direction_degrees);
double wind_avg_get(const wind_avg *w);

#endif