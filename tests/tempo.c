/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tempo.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void encode(unsigned char *p, sample_format_t sf, double sample)
{
	int bytes = sf_get_sample_size(sf), i;
	int64_t half = INT64_C(1) << (sf_get_bits(sf) - 1);
	int64_t value = llround(sample * half);
	uint32_t u = value + (sf_get_signed(sf) ? 0 : half);

	for (i = 0; i < bytes; i++) {
		p[sf_get_bigendian(sf) ? bytes - i - 1 : i] = u & 255;
		u >>= 8;
	}
}

static double decode(const unsigned char *p, sample_format_t sf)
{
	int bytes = sf_get_sample_size(sf), i;
	int64_t half = INT64_C(1) << (sf_get_bits(sf) - 1);
	int64_t value = 0;

	for (i = 0; i < bytes; i++)
		value = value * 256 + p[sf_get_bigendian(sf) ? i : bytes - i - 1];
	if (!sf_get_signed(sf))
		value -= half;
	else if (value >= half)
		value -= 2 * half;
	return (double)value / half;
}

static void drain(struct tempo *t, unsigned char *out, int *used, int cap, int frame)
{
	char *buf;
	int size;

	while ((size = tempo_read(t, &buf))) {
		/* Simulate short output-device writes, retaining the rest. */
		int take = size < 37 * frame ? size : 37 * frame;
		int i;
		assert(*used + take <= cap);
		for (i = 0; i < take; i++)
			out[(*used)++] = (unsigned char)buf[i];
		tempo_consume(t, take);
	}
}

static double tone_power(const unsigned char *out, sample_format_t sf,
		int ch, int first, int last, double hz)
{
	double coeff = 2 * cos(2 * 3.141592653589793 * hz / sf_get_rate(sf));
	double a = 0, b = 0;
	int i, frame = sf_get_frame_size(sf), bytes = sf_get_sample_size(sf);

	for (i = first; i < last; i++) {
		double c = decode(out + i * frame + ch * bytes, sf) + coeff * a - b;
		b = a;
		a = c;
	}
	return a * a + b * b - coeff * a * b;
}

static void check_audio(int rate, int channels, int bits, int sign, int be, double speed)
{
	sample_format_t sf = sf_rate(rate) | sf_channels(channels) | sf_bits(bits) |
		sf_signed(sign) | sf_bigendian(be);
	struct tempo *t = tempo_new(sf, speed);
	int frame = sf_get_frame_size(sf), bytes = bits / 8;
	int total = rate * 2, want = llround(total / speed), used = 0;
	int cap = (want + 8192) * frame, start, ch;
	unsigned char *out = malloc(cap), *in = malloc(TEMPO_BLOCK_FRAMES * frame);

	assert(t && out && in);
	for (start = 0; start < total;) {
		int n = (start % 997) + 1, i;
		if (n > total - start)
			n = total - start;
		for (i = 0; i < n; i++) {
			for (ch = 0; ch < channels; ch++) {
				double freq = 440 + 110 * ch;
				double value = 0.5 * sin(2 * 3.141592653589793 * freq * (start + i) / rate);
				encode(in + i * frame + ch * bytes, sf, value);
			}
		}
		tempo_write(t, (const char *)in, n * frame);
		drain(t, out, &used, cap, frame);
		start += n;
	}
	tempo_finish(t);
	tempo_finish(t); /* EOF and a speed change may both request a flush. */
	drain(t, out, &used, cap, frame);
	if (abs(used / frame - want) > 1) {
		fprintf(stderr, "duration: %d/%d Hz, %d bit, speed %g: got %d, wanted %d\n",
			channels, rate, bits, speed, used / frame, want);
		abort();
	}
	for (ch = 0; ch < channels; ch++) {
		int first = rate / 10, last = used / frame - rate / 10, i;
		double best = 0, shifted = 0, energy = 0;
		double expected = 440 + 110 * ch;
		double wrong = expected * (speed == 1.0 ? 1.2 : speed);

		/* Check the spectral peak, rather than zero crossings: the
		 * time stretcher can introduce low-level ringing near zero. */
		for (i = -4; i <= 4; i++) {
			double power = tone_power(out, sf, ch, first, last, expected + i);
			double other = tone_power(out, sf, ch, first, last, wrong + i);
			if (power > best)
				best = power;
			if (other > shifted)
				shifted = other;
		}
		if (best < 16 * shifted) {
			fprintf(stderr, "pitch: %d Hz, %d ch, %d bit, speed %g: %g / %g\n",
				rate, channels, bits, speed, best, shifted);
			abort();
		}
		for (i = first; i < last; i++) {
			double value = decode(out + i * frame + ch * bytes, sf);
			energy += value * value;
		}
		assert(energy / (last - first) > 0.02);
	}
	tempo_free(t);
	free(in);
	free(out);
}

static void check_short(void)
{
	sample_format_t sf = sf_rate(48000) | sf_channels(2) | sf_bits(16) | sf_signed(1);
	int lengths[] = { 0, 1, 11, 127, 1024 };
	double speeds[] = { 0.25, 1.1, 4.0 };
	unsigned int i, j;
	unsigned char in[TEMPO_BLOCK_FRAMES * 4] = { 0 };
	unsigned char out[TEMPO_BLOCK_FRAMES * 16];

	for (i = 0; i < sizeof(lengths) / sizeof(*lengths); i++) {
		for (j = 0; j < sizeof(speeds) / sizeof(*speeds); j++) {
			struct tempo *t = tempo_new(sf, speeds[j]);
			int used = 0;
			assert(t);
			tempo_write(t, (const char *)in, lengths[i] * 4);
			tempo_finish(t);
			drain(t, out, &used, sizeof(out), 4);
			assert(used / 4 == llround(lengths[i] / speeds[j]));
			tempo_free(t);
		}
	}
}

int main(void)
{
	double speeds[] = { 0.25, 0.5, 1.0 / 1.1, 1.0, 1.1, 2.0, 4.0 };
	unsigned int i;
	int bits, be, sign;
	sample_format_t sf = sf_rate(48000) | sf_channels(2) | sf_bits(16) | sf_signed(1);

	assert(!tempo_new(sf, NAN));
	assert(!tempo_new(sf, 0));
	assert(!tempo_new(sf, 5));
	assert(!tempo_supported(0));
	tempo_free(NULL);
	if (!tempo_enabled()) {
		assert(!tempo_new(sf, 1));
		puts("tempo: disabled build passed");
		return 0;
	}
	check_short();
	for (i = 0; i < sizeof(speeds) / sizeof(*speeds); i++)
		check_audio(48000, 2, 16, 1, 0, speeds[i]);
	for (bits = 8; bits <= 32; bits += 8)
		for (be = 0; be < 2; be++)
			for (sign = 0; sign < 2; sign++)
				check_audio(44100, 2, bits, sign, be, 1.1);
	check_audio(8000, 1, 16, 1, 0, 0.5);
	check_audio(96000, 6, 24, 1, 0, 2.0);
	check_audio(192000, 2, 32, 1, 1, 2.0);
	puts("tempo: duration, pitch, PCM formats and short writes passed");
	return 0;
}
