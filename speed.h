#ifndef SPEED_H
#define SPEED_H

#include <stdint.h>

struct speed_stretcher;

struct speed_stretcher *speed_stretcher_new(int channels, int rate);
void speed_stretcher_free(struct speed_stretcher *s);

/*
 * Process audio data. 
 * Returns number of frames written to output.
 * consumed is the number of frames consumed from input.
 */
int speed_stretcher_process(struct speed_stretcher *s, double speed, 
                             const int16_t *input, int nr_input,
                             int16_t *output, int nr_output,
                             int *consumed);

#endif
