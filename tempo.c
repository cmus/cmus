/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tempo.h"
#include "config/rubberband.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_RUBBERBAND
#include <rubberband/rubberband-c.h>

struct tempo {
	RubberBandState state;
	sample_format_t sf;
	double speed;
	float **planes;
	float *samples;
	char *buffer;
	int size, offset, final;
	unsigned int skip;
	uint64_t input_frames, output_frames;
};

int tempo_enabled(void)
{
	return 1;
}

int tempo_supported(sample_format_t sf)
{
	unsigned int bits = sf_get_bits(sf);

	return sf_get_channels(sf) > 0 && sf_get_channels(sf) <= 32 &&
		sf_get_rate(sf) >= 8000 && sf_get_rate(sf) <= 192000 &&
		(bits == 8 || bits == 16 || bits == 24 || bits == 32);
}

struct tempo *tempo_new(sample_format_t sf, double speed)
{
	struct tempo *t;
	unsigned int channels = sf_get_channels(sf), ch, pad;

	if (!tempo_supported(sf) || !isfinite(speed) ||
		speed < PLAYER_SPEED_MIN || speed > PLAYER_SPEED_MAX)
		return NULL;
	t = calloc(1, sizeof(*t));
	if (!t)
		return NULL;
	t->sf = sf;
	t->speed = speed;
	t->planes = calloc(channels, sizeof(*t->planes));
	t->samples = calloc(channels * TEMPO_BLOCK_FRAMES, sizeof(*t->samples));
	t->buffer = malloc(TEMPO_BLOCK_FRAMES * sf_get_frame_size(sf));
	t->state = rubberband_new(sf_get_rate(sf), channels,
		RubberBandOptionProcessRealTime | RubberBandOptionThreadingNever,
		1.0 / speed, 1.0);
	if (!t->planes || !t->samples || !t->buffer || !t->state) {
		tempo_free(t);
		return NULL;
	}
	for (ch = 0; ch < channels; ch++)
		t->planes[ch] = t->samples + ch * TEMPO_BLOCK_FRAMES;
	rubberband_set_max_process_size(t->state, TEMPO_BLOCK_FRAMES);
	/* Real-time mode needs explicit input padding and output delay removal. */
	t->skip = rubberband_get_start_delay(t->state);
	pad = rubberband_get_preferred_start_pad(t->state);
	while (pad) {
		unsigned int n = pad > TEMPO_BLOCK_FRAMES ? TEMPO_BLOCK_FRAMES : pad;
		rubberband_process(t->state, (const float * const *)t->planes, n, 0);
		pad -= n;
	}
	return t;
}

void tempo_free(struct tempo *t)
{
	if (!t)
		return;
	if (t->state)
		rubberband_delete(t->state);
	free(t->planes);
	free(t->samples);
	free(t->buffer);
	free(t);
}

/* Byte-wise conversion also handles packed 24-bit and unaligned samples. */
static double read_sample(const unsigned char *p, sample_format_t sf)
{
	unsigned int bytes = sf_get_sample_size(sf), i;
	uint32_t value = 0;
	int64_t midpoint = INT64_C(1) << (sf_get_bits(sf) - 1);
	int64_t sample;

	for (i = 0; i < bytes; i++) {
		unsigned int index = sf_get_bigendian(sf) ? i : bytes - i - 1;
		value = (value << 8) | p[index];
	}
	sample = value;
	if (!sf_get_signed(sf))
		sample -= midpoint;
	else if (sample >= midpoint)
		sample -= 2 * midpoint;
	return (double)sample / midpoint;
}

static void write_sample(unsigned char *p, sample_format_t sf, double sample)
{
	unsigned int bytes = sf_get_sample_size(sf), i;
	int64_t midpoint = INT64_C(1) << (sf_get_bits(sf) - 1);
	int64_t value;
	uint32_t encoded;

	if (!isfinite(sample))
		sample = 0;
	if (sample >= 1.0)
		value = midpoint - 1;
	else if (sample <= -1.0)
		value = -midpoint;
	else
		value = llrint(sample * midpoint);
	if (value >= midpoint)
		value = midpoint - 1;
	if (!sf_get_signed(sf))
		value += midpoint;
	encoded = (uint32_t)value;
	for (i = 0; i < bytes; i++) {
		unsigned int index = sf_get_bigendian(sf) ? bytes - i - 1 : i;
		p[index] = encoded & 0xff;
		encoded >>= 8;
	}
}

void tempo_write(struct tempo *t, const char *buf, int count)
{
	unsigned int channels = sf_get_channels(t->sf);
	unsigned int bytes = sf_get_sample_size(t->sf);
	unsigned int frames = count / sf_get_frame_size(t->sf), i, ch;
	const unsigned char *p = (const unsigned char *)buf;

	for (i = 0; i < frames; i++) {
		for (ch = 0; ch < channels; ch++, p += bytes)
			t->planes[ch][i] = read_sample(p, t->sf);
	}
	rubberband_process(t->state, (const float * const *)t->planes, frames, 0);
	t->input_frames += frames;
}

void tempo_finish(struct tempo *t)
{
	if (!t->final) {
		unsigned int pad = rubberband_get_preferred_start_pad(t->state) +
			ceil(t->speed * rubberband_get_start_delay(t->state));
		/* Supply end padding as well: real-time rounding can otherwise
		 * leave the last fractional processing block short. read() trims
		 * the result to the duration of the real input. */
		memset(t->samples, 0, sf_get_channels(t->sf) * TEMPO_BLOCK_FRAMES * sizeof(float));
		while (pad) {
			unsigned int n = pad > TEMPO_BLOCK_FRAMES ? TEMPO_BLOCK_FRAMES : pad;
			rubberband_process(t->state, (const float * const *)t->planes, n, 0);
			pad -= n;
		}
		rubberband_process(t->state, (const float * const *)t->planes, 0, 1);
		t->final = 1;
	}
}

int tempo_finished(const struct tempo *t)
{
	return t->final;
}

double tempo_speed(const struct tempo *t)
{
	return t->speed;
}

int tempo_read(struct tempo *t, char **buf)
{
	unsigned int channels = sf_get_channels(t->sf);
	unsigned int bytes = sf_get_sample_size(t->sf);
	int available;

	*buf = t->buffer + t->offset;
	if (t->offset < t->size)
		return t->size - t->offset;
	t->offset = t->size = 0;
	*buf = t->buffer;
	while ((available = rubberband_available(t->state)) > 0) {
		unsigned int n = available, start = 0, i, ch;
		unsigned char *p = (unsigned char *)t->buffer;
		uint64_t target = llround(t->input_frames / t->speed);

		if (n > TEMPO_BLOCK_FRAMES)
			n = TEMPO_BLOCK_FRAMES;
		n = rubberband_retrieve(t->state, t->planes, n);
		if (t->skip) {
			start = t->skip < n ? t->skip : n;
			t->skip -= start;
		}
		/* The final process call can round up to a whole internal block. */
		if (t->final && t->output_frames + n - start > target)
			n = start + (target > t->output_frames ? target - t->output_frames : 0);
		for (i = start; i < n; i++) {
			for (ch = 0; ch < channels; ch++, p += bytes)
				write_sample(p, t->sf, t->planes[ch][i]);
		}
		t->output_frames += n - start;
		t->size = (n - start) * sf_get_frame_size(t->sf);
		if (t->size)
			return t->size;
	}
	return 0;
}

void tempo_consume(struct tempo *t, int count)
{
	t->offset += count;
}

#else

int tempo_enabled(void) { return 0; }
int tempo_supported(sample_format_t sf) { return 0; }
struct tempo *tempo_new(sample_format_t sf, double speed) { return NULL; }
void tempo_free(struct tempo *t) { }
void tempo_write(struct tempo *t, const char *buf, int count) { }
void tempo_finish(struct tempo *t) { }
int tempo_finished(const struct tempo *t) { return 1; }
double tempo_speed(const struct tempo *t) { return 1.0; }
int tempo_read(struct tempo *t, char **buf) { return 0; }
void tempo_consume(struct tempo *t, int count) { }

#endif
