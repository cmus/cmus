#ifndef CMUS_SPEED_H
#define CMUS_SPEED_H

#include <stdint.h>

struct speed_stretcher;

struct speed_stretcher *speed_stretcher_new(int channels, int rate);
void speed_stretcher_free(struct speed_stretcher *s);

/* 
 * Process input samples and produce output samples.
 * input and output are int16_t arrays.
 * nr_input is number of frames in input.
 * nr_output is max number of frames in output.
 * returns number of frames produced in output.
 */
int speed_stretcher_process(struct speed_stretcher *s, double speed, 
                             const int16_t *input, int nr_input,
                             int16_t *output, int nr_output,
                             int *consumed);

#endif
