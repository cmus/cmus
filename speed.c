#include "speed.h"
#include "xmalloc.h"
#include "debug.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/*
 * WSOLA implementation for pitch-invariant time-stretching.
 * Optimized for quality with Hann windowing and corrected template matching.
 */

#define WINDOW_MS 60
#define OVERLAP_MS 20
#define SEARCH_MS 20
#define IN_BUF_MS 300

struct speed_stretcher {
	int channels;
	int rate;
	int window_size;
	int overlap_size;
	int search_range;
	int16_t *overlap_buf;

	int16_t *in_buf;
	int in_buf_size;
	int in_buf_fill;

	/* Hann window ramp for overlap-add */
	int32_t *window_table;

	/* Fractional input position for high-precision speed control */
	double in_pos_frac;
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

	s->in_buf_size = (IN_BUF_MS * rate) / 1000;
	s->in_buf = xnew(int16_t, s->in_buf_size * channels);
	s->in_buf_fill = 0;
	s->in_pos_frac = 0.0;

	s->window_table = xnew(int32_t, s->overlap_size);
	for (int i = 0; i < s->overlap_size; i++) {
		/* Raised Cosine (Hann) ramp: 0.5 * (1 - cos(pi * i / n)) */
		double x = (double)i / s->overlap_size;
		s->window_table[i] = (int32_t)(0.5 * (1.0 - cos(M_PI * x)) * 16384.0);
	}

	d_print("new stretcher: rate=%d channels=%d win=%d overlap=%d search=%d\n", 
		rate, channels, s->window_size, s->overlap_size, s->search_range);
	return s;
}

void speed_stretcher_free(struct speed_stretcher *s)
{
	if (!s) return;
	free(s->overlap_buf);
	free(s->in_buf);
	free(s->window_table);
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
	/* Copy new input to internal buffer */
	int to_copy = nr_input;
	if (s->in_buf_fill + to_copy > s->in_buf_size)
		to_copy = s->in_buf_size - s->in_buf_fill;
	
	if (to_copy > 0) {
		memcpy(s->in_buf + s->in_buf_fill * s->channels, input, to_copy * s->channels * sizeof(int16_t));
		s->in_buf_fill += to_copy;
		*consumed = to_copy;
	} else {
		*consumed = 0;
	}

	if (speed > 0.99 && speed < 1.01) {
		int n = s->in_buf_fill < nr_output ? s->in_buf_fill : nr_output;
		memcpy(output, s->in_buf, n * s->channels * sizeof(int16_t));
		if (n < s->in_buf_fill)
			memmove(s->in_buf, s->in_buf + n * s->channels, (s->in_buf_fill - n) * s->channels * sizeof(int16_t));
		s->in_buf_fill -= n;
		s->in_pos_frac = 0.0;
		return n;
	}

	int out_pos = 0;
	double step = (double)(s->window_size - s->overlap_size) * speed;

	/* We need enough data for search and one full window */
	while (out_pos + s->window_size <= nr_output && 
	       (int)s->in_pos_frac + (int)step + s->search_range + s->window_size <= s->in_buf_fill) {
		
		int best_offset = 0;
		long long min_diff = -1;

		/* 1. Find best match around the TARGET position (in_pos + step) */
		int target_pos = (int)s->in_pos_frac + (int)step;
		for (int offset = -s->search_range; offset < s->search_range; offset++) {
			int check_pos = target_pos + offset;
			if (check_pos < 0) continue;
			
			long long diff = calc_diff(s->channels, s->overlap_buf, 
			                           s->in_buf + check_pos * s->channels, s->overlap_size);
			if (min_diff == -1 || diff < min_diff) {
				min_diff = diff;
				best_offset = offset;
			}
		}

		/* Update real in_pos to the best matching segment */
		int actual_in_pos = target_pos + best_offset;

		/* 2. Overlap-Add using Hann window */
		for (int i = 0; i < s->overlap_size; i++) {
			int32_t w2 = s->window_table[i];
			int32_t w1 = 16384 - w2;
			for (int c = 0; c < s->channels; c++) {
				int32_t v1 = s->overlap_buf[i * s->channels + c];
				int32_t v2 = s->in_buf[(actual_in_pos + i) * s->channels + c];
				output[(out_pos + i) * s->channels + c] = (int16_t)((v1 * w1 + v2 * w2) >> 14);
			}
		}

		/* 3. Copy the rest of the window (non-overlapping part) */
		int remaining = s->window_size - s->overlap_size;
		memcpy(output + (out_pos + s->overlap_size) * s->channels, 
		       s->in_buf + (actual_in_pos + s->overlap_size) * s->channels, 
		       remaining * s->channels * sizeof(int16_t));

		/* 4. Prepare for next iteration */
		out_pos += remaining;
		
		/* Save the continuation as the next overlap template */
		memcpy(s->overlap_buf, 
		       s->in_buf + (actual_in_pos + remaining) * s->channels, 
		       s->overlap_size * s->channels * sizeof(int16_t));
		
		/* Advance fractional position by the exact speed-scaled step */
		s->in_pos_frac += step;
		
		/* Shift buffer as we go to avoid large memmoves at the end */
		int consumed_frames = (int)s->in_pos_frac;
		if (consumed_frames > 0) {
			memmove(s->in_buf, s->in_buf + consumed_frames * s->channels, 
			        (s->in_buf_fill - consumed_frames) * s->channels * sizeof(int16_t));
			s->in_buf_fill -= consumed_frames;
			s->in_pos_frac -= consumed_frames;
		}
	}

	return out_pos;
}
