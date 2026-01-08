#include "speed.h"
#include "xmalloc.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/*
 * Minimal WSOLA implementation for time-stretching without pitch change.
 */

#define WINDOW_MS 40
#define OVERLAP_MS 10
#define SEARCH_MS 15

struct speed_stretcher {
	int channels;
	int rate;
	int window_size;
	int overlap_size;
	int search_range;
	int16_t *overlap_buf;
};

struct speed_stretcher *speed_stretcher_new(int channels, int rate)
{
	struct speed_stretcher *s = xnew(struct speed_stretcher, 1);
	s->channels = channels;
	s->rate = rate;
	s->window_size = (WINDOW_MS * rate) / 1000;
	s->overlap_size = (OVERLAP_MS * rate) / 1000;
	s->search_range = (SEARCH_MS * rate) / 1000;
	s->overlap_buf = xnew0(int16_t, s->overlap_size * channels);
	return s;
}

void speed_stretcher_free(struct speed_stretcher *s)
{
	if (!s) return;
	free(s->overlap_buf);
	free(s);
}

static long long calc_diff(int channels, const int16_t *a, const int16_t *b, int n)
{
	long long diff = 0;
	int i;
	int limit = n * channels;
	for (i = 0; i < limit; i++) {
		int d = a[i] - b[i];
		diff += (d < 0) ? -d : d;
	}
	return diff;
}

int speed_stretcher_process(struct speed_stretcher *s, double speed, 
                             const int16_t *input, int nr_input,
                             int16_t *output, int nr_output,
                             int *consumed)
{
	if (speed > 0.99 && speed < 1.01) {
		int n = nr_input < nr_output ? nr_input : nr_output;
		memcpy(output, input, n * s->channels * sizeof(int16_t));
		*consumed = n;
		return n;
	}

	int out_pos = 0;
	int in_pos = 0;
	/* Nominal step in input for one window in output */
	int step = (int)((s->window_size - s->overlap_size) * speed);

	while (out_pos + s->window_size <= nr_output && 
	       in_pos + step + s->search_range + s->window_size <= nr_input) {
		
		int best_offset = 0;
		long long min_diff = -1;

		/* Search for best match around in_pos */
		for (int offset = -s->search_range; offset < s->search_range; offset++) {
			int check_pos = in_pos + offset;
			if (check_pos < 0) continue;
			
			long long diff = calc_diff(s->channels, s->overlap_buf, 
			                           input + check_pos * s->channels, s->overlap_size);
			if (min_diff == -1 || diff < min_diff) {
				min_diff = diff;
				best_offset = offset;
			}
		}

		in_pos += best_offset;

		/* Overlap-Add previous overlap with best match from input */
		for (int i = 0; i < s->overlap_size; i++) {
			/* Linear ramp for simplicity */
			int32_t w2 = (i * 1024) / s->overlap_size;
			int32_t w1 = 1024 - w2;
			for (int c = 0; c < s->channels; c++) {
				int32_t v1 = s->overlap_buf[i * s->channels + c];
				int32_t v2 = input[(in_pos + i) * s->channels + c];
				output[(out_pos + i) * s->channels + c] = (int16_t)((v1 * w1 + v2 * w2) >> 10);
			}
		}

		/* Copy remaining part of the window */
		int remaining = s->window_size - s->overlap_size;
		memcpy(output + (out_pos + s->overlap_size) * s->channels, 
		       input + (in_pos + s->overlap_size) * s->channels, 
		       remaining * s->channels * sizeof(int16_t));

		out_pos += s->window_size - s->overlap_size;
		in_pos += step;

		/* Save overlap for next iteration (taken from input after the window we just used) */
		memcpy(s->overlap_buf, 
		       input + in_pos * s->channels, 
		       s->overlap_size * s->channels * sizeof(int16_t));
	}

	*consumed = in_pos;
	return out_pos;
}
