#include "pcm.h"

#include <inttypes.h>
#include <stdlib.h>

/*
 * Functions to convert PCM to 16-bit signed little-endian stereo
 *
 * Conversion for 8-bit PCM):
 *   1. phase
 *      unsigned -> signed
 *      mono -> stereo
 *      8 -> 16
 *
 * Conversion for 16-bit PCM:
 *   1. phase
 *      be -> le
 *      unsigned -> signed
 *
 *   2. phase
 *      mono -> stereo
 *
 * There's no reason to split 8-bit conversion to 2 phases because we need to
 * use separate buffer for 8->16 conversion anyway.
 *
 * Conversions for 16-bit stereo can be done in place. 16-bit mono needs to be
 * converted to stereo so it's worthwhile to split the conversion to 2 phases.
 */

static void convert_u8_1ch_to_s16_2ch(char *dst, const char *src, int count)
{
	int16_t *d = (int16_t *)dst;
	const uint8_t *s = (const uint8_t *)src;
	int i, j = 0;

	for (i = 0; i < count; i++) {
		int16_t sample = s[i] << 8;
		sample -= 32768;
		d[j++] = sample;
		d[j++] = sample;
	}
}

static void convert_s8_1ch_to_s16_2ch(char *dst, const char *src, int count)
{
	int16_t *d = (int16_t *)dst;
	const int8_t *s = (const int8_t *)src;
	int i, j = 0;

	for (i = 0; i < count; i++) {
		int16_t sample = s[i] << 8;
		d[j++] = sample;
		d[j++] = sample;
	}
}

static void convert_u8_2ch_to_s16_2ch(char *dst, const char *src, int count)
{
	int16_t *d = (int16_t *)dst;
	const int8_t *s = (const int8_t *)src;
	int i;

	for (i = 0; i < count; i++) {
		int16_t sample = s[i] << 8;
		sample -= 32768;
		d[i] = sample;
	}
}

static void convert_s8_2ch_to_s16_2ch(char *dst, const char *src, int count)
{
	int16_t *d = (int16_t *)dst;
	const int8_t *s = (const int8_t *)src;
	int i;

	for (i = 0; i < count; i++) {
		int16_t sample = s[i] << 8;
		d[i] = sample;
	}
}

static void convert_u16_le_to_s16_le(char *buf, int count)
{
	int16_t *b = (int16_t *)buf;
	int i;

	for (i = 0; i < count; i++) {
		int sample = (uint16_t)b[i];
		sample -= 32768;
		b[i] = sample;
	}
}

static void convert_u16_be_to_s16_le(char *buf, int count)
{
	int16_t *b = (int16_t *)buf;
	int i;

	for (i = 0; i < count; i++) {
		uint16_t u = b[i];
		int sample;

		u = (u << 8) | (u >> 8);
		sample = (int)u - 32768;
		b[i] = sample;
	}
}

static void convert_s16_be_to_s16_le(char *buf, int count)
{
	int16_t *b = (int16_t *)buf;
	int i;

	for (i = 0; i < count; i++) {
		uint16_t sample = b[i];
		b[i] = (sample << 8) | (sample >> 8);
	}
}

static void convert_16_1ch_to_16_2ch(char *dst, const char *src, int count)
{
	int16_t *d = (int16_t *)dst;
	const int16_t *s = (const int16_t *)src;
	int i, j = 0;

	for (i = 0; i < count; i++) {
		d[j++] = s[i];
		d[j++] = s[i];
	}
}

/* index is ((bits >> 2) & 1) | (is_signed << 1) | (channels - 1) */
pcm_conv_func pcm_conv[8] = {
	convert_u8_1ch_to_s16_2ch,
	convert_u8_2ch_to_s16_2ch,
	convert_s8_1ch_to_s16_2ch,
	convert_s8_2ch_to_s16_2ch,

	convert_16_1ch_to_16_2ch,
	NULL,
	convert_16_1ch_to_16_2ch,
	NULL
};

/* index is ((bits >> 2) & 1) | (is_signed << 1) | bigendian */
pcm_conv_in_place_func pcm_conv_in_place[8] = {
	NULL,
	NULL,
	NULL,
	NULL,

	convert_u16_le_to_s16_le,
	convert_u16_be_to_s16_le,
	NULL,
	convert_s16_be_to_s16_le
};
