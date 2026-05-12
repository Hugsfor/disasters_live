#include "dsp.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Butterworth bandpass filter
// bilinear transform method to convert analog prototype to digital

void biquad_bandpass(filter *f, double low_cutoff, double high_cutoff, double sample_rate) {
    memset(f->x, 0, sizeof(f->x));
    memset(f->y, 0, sizeof(f->y));
    memset(f->b, 0, sizeof(f->b));
    memset(f->a, 0, sizeof(f->a));

    if (low_cutoff <= 0 || high_cutoff <= low_cutoff || sample_rate <= 0) {
        f->b[0] = 1.0;
        f->a[0] = 1.0;
        return;
    }

    // calculate normalized center frequency and bandwidth
    double bandwidth = 2.0 * M_PI * (high_cutoff - low_cutoff) / sample_rate;
    double center_freq = 2.0 * M_PI * sqrt(low_cutoff * high_cutoff) / sample_rate;

    double cos_bandwidth = cos(bandwidth);
    double sin_bandwidth = sin(bandwidth);

    // Q factor = 0.7071 gives Butterworth (maximally flat) response
    double q_factor = 1.4142;
    double alpha = sin_bandwidth / q_factor;

    // compute raw coefficients
    f->b[0] = alpha;
    f->b[1] = 0.0;
    f->b[2] = -alpha;
    f->a[0] = 1.0 + alpha;
    f->a[1] = -2.0 * cos_bandwidth;

    // normalize so feedback is 1.0
    double normalization = 1.0 / f->a[0];
    f->b[0] *= normalization;
    f->b[1] *= normalization;
    f->b[2] *= normalization;
    f->a[1] *= normalization;
}


 // process a sample through the biquad filter
 // uses Direct Form I structure: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1]

double biquad_process(filter *f, double input_sample) {
    // shift input delay
    f->x[2] = f->x[1];   // x[n-2] = old x[n-1]
    f->x[1] = f->x[0];   // x[n-1] = old x[n]
    f->x[0] = input_sample; // x[n]   = new sample

    // shift output delay
    f->y[2] = f->y[1]; // y[n-2] = old y[n-1]
    f->y[1] = f->y[0]; // y[n-1] = old y[n]

    double filtered_output = f->b[0] * f->x[0] + f->b[1] * f->x[1] + f->b[2] * f->x[2] - f->a[1] * f->y[1];

    f->y[0] = filtered_output;
    return filtered_output;
}

// initialize STA/LTA (Short-Term Average / Long-Term Average) detector used for detecting transient events like earthquake P-waves

int stalta_init(stalta *d, int sta_samples, int lta_samples)
{
    d->sta_size = sta_samples;
    d->lta_size = lta_samples;
    d->write_idx = 0;
    d->filled = 0;
    d->sta_sum = 0.0;
    d->lta_sum = 0.0;

    // allocate circular buffers
    d->sta_buffer = calloc(sta_samples, sizeof(double));
    d->lta_buffer = calloc(lta_samples, sizeof(double));

    // check allocation succeeded
    if (!d->sta_buffer || !d->lta_buffer) {
        free(d->sta_buffer);
        free(d->lta_buffer);
        d->sta_buffer = NULL;
        d->lta_buffer = NULL;
        return -1;
    }

    return 0;
}

// process one sample through STA/LTA detector
// returns STA/LTA ratio:
// ~1.0 = normal background noise
// >3.0 = potential event detected
// >8.0 = strong event in progress

double stalta_process(stalta *d, double sample) {
    double absolute_value = fabs(sample);
    int write_position = d->write_idx;

    // update short-term average window
    if (write_position < d->sta_size) {
        d->sta_sum -= d->sta_buffer[write_position];
        d->sta_buffer[write_position] = absolute_value;
        d->sta_sum += absolute_value;
    }

    // update long-term average window
    int lta_position = write_position % d->lta_size;
    d->lta_sum -= d->lta_buffer[lta_position];
    d->lta_buffer[lta_position] = absolute_value;
    d->lta_sum += absolute_value;

    // advance write position
    d->write_idx = (write_position + 1) % d->lta_size;

    //check if buffers are filled
    if (d->write_idx >= d->lta_size - 1) {
        d->filled = 1;
    }

    if (!d->filled) {
        return 1.0;
    }

    // averages
    double sta_avg = d->sta_sum / d->sta_size;
    double lta_avg = d->lta_sum / d->lta_size;

    // avoid division by near-zero
    if (lta_avg < 0.000001) {
        return 1.0;
    }

    return sta_avg / lta_avg;
}

// reset detector to initial state
void stalta_reset(stalta *d) {
    memset(d->sta_buffer, 0, d->sta_size * sizeof(double));
    memset(d->lta_buffer, 0, d->lta_size * sizeof(double));
    d->write_idx = 0;
    d->filled = 0;
    d->sta_sum = 0.0;
    d->lta_sum = 0.0;
}

// f ree memory
void stalta_free(stalta *d) {
    free(d->sta_buffer);
    free(d->lta_buffer);
    d->sta_buffer = NULL;
    d->lta_buffer = NULL;
}

// initialize EWMA filter
void ewma_init(ewma *e, double alpha) {
    e->alpha = alpha;
    e->current = 0.0;
    e->initialized = 0;
}

// process sample through EWMA filter
double ewma_process(ewma *e, double sample) {
    if (!e->initialized) {
        e->current = sample;
        e->initialized = 1;
    } else {
        e->current = e->alpha * sample + (1.0 - e->alpha) * e->current;
    }
    return e->current;
}

// initialize wind direction avg
void wind_avg_init(wind_avg *w) {
    w->sin_sum = 0.0;
    w->cos_sum = 0.0;
    w->count = 0;
}

// add wind direction reading
void wind_avg_add(wind_avg *w, double direction_degrees) {
    double rad = direction_degrees * M_PI / 180.0;
    w->sin_sum += sin(rad);
    w->cos_sum += cos(rad);
    w->count++;
}

// get averaged wind direction
double wind_avg_get(const wind_avg *w) {
    if (w->count == 0) {
        return 0.0;
    }

    double avg_sin = w->sin_sum / w->count;
    double avg_cos = w->cos_sum / w->count;
    double avg_rad = atan2(avg_sin, avg_cos);
    double avg_deg = avg_rad * 180.0 / M_PI;

    if (avg_deg < 0.0) {
        avg_deg += 360.0;
    }

    return avg_deg;
}