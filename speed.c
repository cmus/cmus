#include "speed.h"
#include "xmalloc.h"
#include "debug.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/*
 * Minimal WSOLA implementation for time-stretching without pitch change.
 */

#define WINDOW_MS 40
#define OVERLAP_MS 10
#define SEARCH_MS 15
#define IN_BUF_MS 200

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

	d_print("new stretcher: rate=%d channels=%d win=%d overlap=%d search=%d in_buf=%d\n", 
		rate, channels, s->window_size, s->overlap_size, s->search_range, s->in_buf_size);
	return s;
}

void speed_stretcher_free(struct speed_stretcher *s)
{
	if (!s) return;
	free(s->overlap_buf);
	free(s->in_buf);
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
		
		/* Shift buffer */
		if (n < s->in_buf_fill)
			memmove(s->in_buf, s->in_buf + n * s->channels, (s->in_buf_fill - n) * s->channels * sizeof(int16_t));
		s->in_buf_fill -= n;
		return n;
	}

	int out_pos = 0;
	int in_pos = 0;
	int step = (int)((s->window_size - s->overlap_size) * speed);

	/* Check if we have enough data to process at least one window */
	/* Requirement: in_pos + step + search_range + window_size <= in_buf_fill */
	while (out_pos + s->window_size <= nr_output && 
	       in_pos + step + s->search_range + s->window_size <= s->in_buf_fill) {
		
		int best_offset = 0;
		long long min_diff = -1;

		/* Search for best match around in_pos */
		for (int offset = -s->search_range; offset < s->search_range; offset++) {
			int check_pos = in_pos + offset;
			if (check_pos < 0) continue;
			
			long long diff = calc_diff(s->channels, s->overlap_buf, 
			                           s->in_buf + check_pos * s->channels, s->overlap_size);
			if (min_diff == -1 || diff < min_diff) {
				min_diff = diff;
				best_offset = offset;
			}
		}

		in_pos += best_offset;

		/* Overlap-Add */
		for (int i = 0; i < s->overlap_size; i++) {
			int32_t w2 = (i * 1024) / s->overlap_size;
			int32_t w1 = 1024 - w2;
			for (int c = 0; c < s->channels; c++) {
				int32_t v1 = s->overlap_buf[i * s->channels + c];
				int32_t v2 = s->in_buf[(in_pos + i) * s->channels + c];
				output[(out_pos + i) * s->channels + c] = (int16_t)((v1 * w1 + v2 * w2) >> 10);
			}
		}

		/* Copy remaining part of the window */
		int remaining = s->window_size - s->overlap_size;
		memcpy(output + (out_pos + s->overlap_size) * s->channels, 
		       s->in_buf + (in_pos + s->overlap_size) * s->channels, 
		       remaining * s->channels * sizeof(int16_t));

		out_pos += s->window_size - s->overlap_size;
		in_pos += step;

		/* Save overlap for next iteration */
		memcpy(s->overlap_buf, 
		       s->in_buf + in_pos * s->channels, 
		       s->overlap_size * s->channels * sizeof(int16_t));
	}

	/* Shift remaining data in internal buffer */
	if (in_pos > 0) {
		if (in_pos < s->in_buf_fill) {
			memmove(s->in_buf, s->in_buf + in_pos * s->channels, 
			        (s->in_buf_fill - in_pos) * s->channels * sizeof(int16_t));
		}
		s->in_buf_fill -= in_pos;
	}

	return out_pos;
}
