/*
 * Copyright (C) 2024 Patrick Gaskin <patrick@pgaskin.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// for development, can cross-compile with $ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang -target aarch64-linux-android26 -shared -o aaudio.so -fPIC -D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ -Werror=unguarded-availability -Wall -std=gnu11 op/aaudio.c -laaudio
// also see https://github.com/google/oboe/blob/main/docs/AndroidAudioHistory.md
// also see https://android.googlesource.com/platform/frameworks/av/+/master/media/libaaudio/examples/utils/AAudioSimplePlayer.h

#ifndef __ANDROID__
// make ide autocomplete work without using a full ndk toolchain
#define __INTRODUCED_IN(api_level)
#endif

// https://developer.android.com/ndk/guides/using-newer-apis
#define REQUIRES_API(x) __attribute__((__availability__(android,introduced=x)))
#define API_AT_LEAST(x) __builtin_available(android x, *)
#define AAUDIO_MINIMUM_API 26

#include <aaudio/AAudio.h>

#include "../op.h"
#include "../mixer.h"
#include "../sf.h"
#include "../utils.h"
#include "../xmalloc.h"

// mapping from AAUDIO_CHANNEL_* enum values to cmus channel_position_t values
//
// cat "$(find ${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk} -wholename '*/AAudio.h' | sort -n | tail -n1)" |
// grep AAUDIO_CHANNEL | tr -d ' \n' | tr '|,' ' \n' | grep -F '<<' | cut -d '_' -f3- |
// cut -d '=' -f1 | xargs printf '#define A2C__%s\tCHANNEL_POSITION_INVALID\n' |
// sed -E $(printf " -e s/(A2C__%s\\\t)CHANNEL_POSITION_INVALID/\\\1CHANNEL_POSITION_%s/" \
//   FRONT_LEFT             FRONT_LEFT \
//   FRONT_RIGHT            FRONT_RIGHT \
//   FRONT_CENTER           FRONT_CENTER \
//   LOW_FREQUENCY          LFE \
//   BACK_LEFT              REAR_LEFT \
//   BACK_RIGHT             REAR_RIGHT \
//   FRONT_LEFT_OF_CENTER   FRONT_LEFT_OF_CENTER \
//   FRONT_RIGHT_OF_CENTER  FRONT_RIGHT_OF_CENTER \
//   BACK_CENTER            REAR_CENTER \
//   SIDE_LEFT              SIDE_LEFT \
//   SIDE_RIGHT             SIDE_RIGHT \
//   TOP_CENTER             TOP_CENTER \
//   TOP_FRONT_LEFT         TOP_FRONT_LEFT \
//   TOP_FRONT_CENTER       TOP_FRONT_CENTER \
//   TOP_FRONT_RIGHT        TOP_FRONT_RIGHT \
//   TOP_BACK_LEFT          TOP_REAR_LEFT \
//   TOP_BACK_CENTER        TOP_REAR_CENTER \
//   TOP_BACK_RIGHT         TOP_REAR_RIGHT \
// ) |
// column -s $'\t' -t | tee /dev/stderr | cut -d ' ' -f2 | cut -d '_' -f3- |
// xargs printf ' X(%s)' | xargs -0 printf '#define A2C_CHANNELS%s\n'
#define A2C__FRONT_LEFT             CHANNEL_POSITION_FRONT_LEFT
#define A2C__FRONT_RIGHT            CHANNEL_POSITION_FRONT_RIGHT
#define A2C__FRONT_CENTER           CHANNEL_POSITION_FRONT_CENTER
#define A2C__LOW_FREQUENCY          CHANNEL_POSITION_LFE
#define A2C__BACK_LEFT              CHANNEL_POSITION_REAR_LEFT
#define A2C__BACK_RIGHT             CHANNEL_POSITION_REAR_RIGHT
#define A2C__FRONT_LEFT_OF_CENTER   CHANNEL_POSITION_FRONT_LEFT_OF_CENTER
#define A2C__FRONT_RIGHT_OF_CENTER  CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER
#define A2C__BACK_CENTER            CHANNEL_POSITION_REAR_CENTER
#define A2C__SIDE_LEFT              CHANNEL_POSITION_SIDE_LEFT
#define A2C__SIDE_RIGHT             CHANNEL_POSITION_SIDE_RIGHT
#define A2C__TOP_CENTER             CHANNEL_POSITION_TOP_CENTER
#define A2C__TOP_FRONT_LEFT         CHANNEL_POSITION_TOP_FRONT_LEFT
#define A2C__TOP_FRONT_CENTER       CHANNEL_POSITION_TOP_FRONT_CENTER
#define A2C__TOP_FRONT_RIGHT        CHANNEL_POSITION_TOP_FRONT_RIGHT
#define A2C__TOP_BACK_LEFT          CHANNEL_POSITION_TOP_REAR_LEFT
#define A2C__TOP_BACK_CENTER        CHANNEL_POSITION_TOP_REAR_CENTER
#define A2C__TOP_BACK_RIGHT         CHANNEL_POSITION_TOP_REAR_RIGHT
#define A2C__TOP_SIDE_LEFT          CHANNEL_POSITION_INVALID
#define A2C__TOP_SIDE_RIGHT         CHANNEL_POSITION_INVALID
#define A2C__BOTTOM_FRONT_LEFT      CHANNEL_POSITION_INVALID
#define A2C__BOTTOM_FRONT_CENTER    CHANNEL_POSITION_INVALID
#define A2C__BOTTOM_FRONT_RIGHT     CHANNEL_POSITION_INVALID
#define A2C__LOW_FREQUENCY_2        CHANNEL_POSITION_INVALID
#define A2C__FRONT_WIDE_LEFT        CHANNEL_POSITION_INVALID
#define A2C__FRONT_WIDE_RIGHT       CHANNEL_POSITION_INVALID
#define A2C_CHANNELS X(FRONT_LEFT) X(FRONT_RIGHT) X(FRONT_CENTER) X(LOW_FREQUENCY) X(BACK_LEFT) X(BACK_RIGHT) X(FRONT_LEFT_OF_CENTER) X(FRONT_RIGHT_OF_CENTER) X(BACK_CENTER) X(SIDE_LEFT) X(SIDE_RIGHT) X(TOP_CENTER) X(TOP_FRONT_LEFT) X(TOP_FRONT_CENTER) X(TOP_FRONT_RIGHT) X(TOP_BACK_LEFT) X(TOP_BACK_CENTER) X(TOP_BACK_RIGHT) X(TOP_SIDE_LEFT) X(TOP_SIDE_RIGHT) X(BOTTOM_FRONT_LEFT) X(BOTTOM_FRONT_CENTER) X(BOTTOM_FRONT_RIGHT) X(LOW_FREQUENCY_2) X(FRONT_WIDE_LEFT) X(FRONT_WIDE_RIGHT)

// mapping from AAUDIO_CHANNEL_* masks to cmus channel_position_t lists
//
// cat "$(find ${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk} -wholename '*/aaudio/AAudio.h' | sort -n | tail -n1)" |
// grep AAUDIO_CHANNEL | tr -d ' \n' | tr '|,' ',\n' | grep -Fve '<<' -e '-1' | cut -d '_' -f3- |
// xargs printf '#define A2C__%s\n' | tr '=' '\t' | sed -E 's/AAUDIO_CHANNEL_([A-Z0-9_]+)/A2C__\1/g' |
// column -s $'\t' -t | tee /dev/stderr | cut -d ' ' -f2 | cut -d '_' -f3- |
// xargs printf ' X(%s)' | xargs -0 printf '#define A2C_LAYOUTS%s\n'
#define A2C__MONO           A2C__FRONT_LEFT
#define A2C__STEREO         A2C__FRONT_LEFT,A2C__FRONT_RIGHT
#define A2C__2POINT1        A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__LOW_FREQUENCY
#define A2C__TRI            A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__FRONT_CENTER
#define A2C__TRI_BACK       A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__BACK_CENTER
#define A2C__3POINT1        A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__FRONT_CENTER,A2C__LOW_FREQUENCY
#define A2C__2POINT0POINT2  A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__TOP_SIDE_LEFT,A2C__TOP_SIDE_RIGHT
#define A2C__2POINT1POINT2  A2C__2POINT0POINT2,A2C__LOW_FREQUENCY
#define A2C__3POINT0POINT2  A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__FRONT_CENTER,A2C__TOP_SIDE_LEFT,A2C__TOP_SIDE_RIGHT
#define A2C__3POINT1POINT2  A2C__3POINT0POINT2,A2C__LOW_FREQUENCY
#define A2C__QUAD           A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__BACK_LEFT,A2C__BACK_RIGHT
#define A2C__QUAD_SIDE      A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__SIDE_LEFT,A2C__SIDE_RIGHT
#define A2C__SURROUND       A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__FRONT_CENTER,A2C__BACK_CENTER
#define A2C__PENTA          A2C__QUAD,A2C__FRONT_CENTER
#define A2C__5POINT1        A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__FRONT_CENTER,A2C__LOW_FREQUENCY,A2C__BACK_LEFT,A2C__BACK_RIGHT
#define A2C__5POINT1_SIDE   A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__FRONT_CENTER,A2C__LOW_FREQUENCY,A2C__SIDE_LEFT,A2C__SIDE_RIGHT
#define A2C__6POINT1        A2C__FRONT_LEFT,A2C__FRONT_RIGHT,A2C__FRONT_CENTER,A2C__LOW_FREQUENCY,A2C__BACK_LEFT,A2C__BACK_RIGHT,A2C__BACK_CENTER
#define A2C__7POINT1        A2C__5POINT1,A2C__SIDE_LEFT,A2C__SIDE_RIGHT
#define A2C__5POINT1POINT2  A2C__5POINT1,A2C__TOP_SIDE_LEFT,A2C__TOP_SIDE_RIGHT
#define A2C__5POINT1POINT4  A2C__5POINT1,A2C__TOP_FRONT_LEFT,A2C__TOP_FRONT_RIGHT,A2C__TOP_BACK_LEFT,A2C__TOP_BACK_RIGHT
#define A2C__7POINT1POINT2  A2C__7POINT1,A2C__TOP_SIDE_LEFT,A2C__TOP_SIDE_RIGHT
#define A2C__7POINT1POINT4  A2C__7POINT1,A2C__TOP_FRONT_LEFT,A2C__TOP_FRONT_RIGHT,A2C__TOP_BACK_LEFT,A2C__TOP_BACK_RIGHT
#define A2C__9POINT1POINT4  A2C__7POINT1POINT4,A2C__FRONT_WIDE_LEFT,A2C__FRONT_WIDE_RIGHT
#define A2C__9POINT1POINT6  A2C__9POINT1POINT4,A2C__TOP_SIDE_LEFT,A2C__TOP_SIDE_RIGHT
#define A2C__FRONT_BACK     A2C__FRONT_CENTER,A2C__BACK_CENTER
#define A2C_LAYOUTS X(MONO) X(STEREO) X(2POINT1) X(TRI) X(TRI_BACK) X(3POINT1) X(2POINT0POINT2) X(2POINT1POINT2) X(3POINT0POINT2) X(3POINT1POINT2) X(QUAD) X(QUAD_SIDE) X(SURROUND) X(PENTA) X(5POINT1) X(5POINT1_SIDE) X(6POINT1) X(7POINT1) X(5POINT1POINT2) X(5POINT1POINT4) X(7POINT1POINT2) X(7POINT1POINT4) X(9POINT1POINT4) X(9POINT1POINT6) X(FRONT_BACK)

// convert a cmus channel map to an equivalent aaudio channel mask (the returned
// value will either be invalid or have the same number of bits set as the
// number of channels)
static aaudio_channel_mask_t cmus_channel_map_to_aaudio_mask(int channels, const channel_position_t *channel_map)
{
	aaudio_channel_mask_t mask = 0;

	// we can only convert a valid channel map
	if (channels >= CHANNELS_MAX || !channel_map || !channel_map_valid(channel_map)) {
		return AAUDIO_CHANNEL_INVALID;
	}

	// special case for mono since cmus defines a separate channel position
	// for it
	if (channels == 1 && channel_map[0] == CHANNEL_POSITION_MONO) {
		return AAUDIO_CHANNEL_FRONT_LEFT;
	}

	// fill the mask, returning invalid if it has duplicates or no mapping
	for (int i = 0; i < channels; i++) {
		#define X(aaudio) \
		if (A2C__##aaudio != CHANNEL_POSITION_INVALID && channel_map[i] == A2C__##aaudio) { \
			if (mask & AAUDIO_CHANNEL_##aaudio) \
				return AAUDIO_CHANNEL_INVALID; \
			mask |= AAUDIO_CHANNEL_##aaudio; \
		}
		A2C_CHANNELS
		#undef X
	}

	return mask;
}

// get the expected cmus channel order for the specified aaudio channel mask
static bool channel_map_init_aaudio(aaudio_channel_mask_t mask, channel_position_t *map)
{
	switch (mask) {
	#define X(aaudio) \
	case AAUDIO_CHANNEL_##aaudio: channel_map_copy(map, (channel_position_t[CHANNELS_MAX]){ A2C__##aaudio }); return true;
	A2C_LAYOUTS
	#undef X
	}
	return false;
}

// get the name of a known aaudio channel mask
static const char *aaudio_channel_to_string(aaudio_channel_mask_t mask)
{
	switch (mask) {
		#define X(aaudio) \
		case AAUDIO_CHANNEL_##aaudio: return #aaudio;
		A2C_CHANNELS
		#undef X
	}
	switch (mask) {
		#define X(aaudio) \
		case AAUDIO_CHANNEL_##aaudio: return #aaudio;
		A2C_LAYOUTS
		#undef X
	}
	return NULL;
}

// allocate a map of output frame byte indexes to input frame byte indexes (or
// -1 to zero) to remap channels (map must be sf_get_frame_size elements)
static ssize_t *make_channel_remap(const channel_position_t *channel_map_out, const channel_position_t *channel_map_in, sample_format_t sf)
{
	int byte, channel_out, channel_in;
	ssize_t *map;

	map = malloc(sizeof(ssize_t) * (size_t) sf_get_frame_size(sf));
	if (!map) {
		return NULL;
	}

	if (!channel_map_out || !channel_map_valid(channel_map_out) || !channel_map_in || !channel_map_valid(channel_map_in)) {
		for (byte = 0; byte < sf_get_frame_size(sf); byte++) {
			map[byte] = byte;
		}
	} else {
		for (byte = 0; byte < sf_get_frame_size(sf); byte++) {
			map[byte] = -1;
		}
		for (channel_out = 0; channel_out < sf_get_channels(sf); channel_out++) {
			if (channel_map_out[channel_out] != CHANNEL_POSITION_INVALID) {
				for (channel_in = 0; channel_in < sf_get_channels(sf); channel_in++) {
					if (channel_map_in[channel_in] == channel_map_out[channel_out]) {
						for (byte = 0; byte < sf_get_sample_size(sf); byte++) {
							map[sf_get_sample_size(sf) * channel_out + byte] = (ssize_t) sf_get_sample_size(sf) * channel_in + byte;
						}
						break;
					}
				}
			}
		}
	}

	d_print("remap bytes");
	for (byte = 0; byte < sf_get_frame_size(sf); byte++) {
		d_print(" %03zd", map[byte]);
	}
	d_print("\n");

	return map;
}

// if remap is non-null, applies it and returns dst, otherwise returns src
static const uint8_t *apply_channel_remap(uint8_t *dst, const uint8_t *src, size_t n, sample_format_t sf, const ssize_t *remap)
{
	size_t off_frame, off;
	BUG_ON(!src || dst == src);

	if (remap) {
		BUG_ON(!dst);

		for (off_frame = 0; off_frame < n; off_frame += (size_t) sf_get_frame_size(sf)) {
			for (off = 0; off < (size_t) sf_get_frame_size(sf); off++) {
				if (remap[off] != -1) {
					dst[off_frame+off] = src[off_frame+remap[off]];
				} else {
					dst[off_frame+off] = 0;
				}
			}
		}
		return dst;
	}
	return src;
}

// configure builder to use the specified sample format (and on a best-effort
// basis, channel_map, if provided)
//
// successful if the sample format is valid and was configured
//
// on success, out_remap will be set to NULL, or if a channel map was configured
// and requires remapping, an allocated map of target frame byte indexes from
// the source index
REQUIRES_API(AAUDIO_MINIMUM_API)
static aaudio_result_t configure_aaudio_sf(AAudioStreamBuilder *builder, sample_format_t sf, const channel_position_t *channel_map, ssize_t **out_remap)
{
	aaudio_format_t format;
	aaudio_channel_mask_t mask;
	channel_position_t mask_expected_channels[CHANNELS_MAX];

	// apply the sample format
	if (!sf_get_signed(sf)) {
		d_print("aaudio does not support unsigned samples\n");
		return AAUDIO_ERROR_INVALID_FORMAT;
	}
	if (sf_get_bigendian(sf)) {
		d_print("aaudio does not support big-endian samples\n");
		return AAUDIO_ERROR_INVALID_FORMAT;
	}
	switch (sf_get_bits(sf)) {
	case 16: format = AAUDIO_FORMAT_PCM_I16; break;
	case 24: format = AAUDIO_FORMAT_PCM_I24_PACKED; break;
	case 32: format = AAUDIO_FORMAT_PCM_I32; break;
	default:
		d_print("unsupported sample format bits\n");
		return AAUDIO_ERROR_INVALID_FORMAT;
	}
	AAudioStreamBuilder_setFormat(builder, format);

	// apply the sample rate
	AAudioStreamBuilder_setSampleRate(builder, sf_get_rate(sf));

	// set the channel count
	//
	// note: if no channel mask is set, aaudio will treat the first two
	// channels as left/right (duplicating mono to stereo if required), and
	// leave the rest up to the device, dropping them if the device doesn't
	// have that many channels
	AAudioStreamBuilder_setChannelCount(builder, sf_get_channels(sf));

	// if we have a channel map, apply it on a best-effort basis
	*out_remap = NULL;
	if (channel_map && channel_map_valid(channel_map)) {
		if (API_AT_LEAST(32)) {
			mask = cmus_channel_map_to_aaudio_mask(sf_get_channels(sf), channel_map);
			d_print("channel map aaudio mask %d (%s)\n", mask, aaudio_channel_to_string(mask) ? aaudio_channel_to_string(mask) : "(null)");
			if (mask == AAUDIO_CHANNEL_INVALID) {
				d_print("not applying channel map since it contains duplicates or not all channels have an aaudio equivalent\n");
			} else {
				if (!channel_map_init_aaudio(mask, mask_expected_channels)) {
					d_print("not applying channel map since there isn't a valid cmus channel mapping for the aaudio mask\n");
				} else {
					if (!channel_map_equal(channel_map, mask_expected_channels, sf_get_channels(sf))) {
						d_print("will remap channels since the input channel_map order doesn't match the order expected by aaudio\n");
						*out_remap = make_channel_remap(mask_expected_channels, channel_map, sf);
						if (!*out_remap) return AAUDIO_ERROR_NO_MEMORY;
					}
					d_print("applying channel mask\n");
					AAudioStreamBuilder_setChannelMask(builder, mask);
				}
			}
		}
	}

	return AAUDIO_OK;
}

// maps an res to a suitable error code
static int OP_ERROR_AAUDIO(aaudio_result_t res) {
	// see https://android.googlesource.com/platform/bionic/+/refs/heads/main/libc/private/bionic_errdefs.h
	switch (res) {
	case AAUDIO_OK:                                           return 0;
	case AAUDIO_ERROR_INTERNAL:                               return OP_ERROR_INTERNAL;
	case AAUDIO_ERROR_NO_SERVICE:                             return OP_ERROR_NOT_SUPPORTED;
	case AAUDIO_ERROR_INVALID_FORMAT:                         return OP_ERROR_SAMPLE_FORMAT;
	case AAUDIO_ERROR_INVALID_RATE:                           return OP_ERROR_SAMPLE_FORMAT;
	case AAUDIO_ERROR_UNAVAILABLE:      errno = ECONNREFUSED; return OP_ERROR_ERRNO; // Connection refused
	case AAUDIO_ERROR_DISCONNECTED:     errno = ECONNRESET;   return OP_ERROR_ERRNO; // Connection reset by peer
	case AAUDIO_ERROR_TIMEOUT:          errno = ETIMEDOUT;    return OP_ERROR_ERRNO; // Connection timed out
	case AAUDIO_ERROR_WOULD_BLOCK:      errno = ENOBUFS;      return OP_ERROR_ERRNO; // No buffer space available
	case AAUDIO_ERROR_UNIMPLEMENTED:    errno = ENOSYS;       return OP_ERROR_ERRNO; // Function not implemented
	case AAUDIO_ERROR_NO_FREE_HANDLES:  errno = EMFILE;       return OP_ERROR_ERRNO; // Too many open files
	case AAUDIO_ERROR_NO_MEMORY:        errno = ENOMEM;       return OP_ERROR_ERRNO; // Out of memory
	case AAUDIO_ERROR_NULL:             errno = EFAULT;       return OP_ERROR_ERRNO; // Bad address
	case AAUDIO_ERROR_OUT_OF_RANGE:     errno = EINVAL;       return OP_ERROR_ERRNO; // Invalid argument
	case AAUDIO_ERROR_INVALID_HANDLE:   errno = EBADF;        return OP_ERROR_ERRNO; // Bad file descriptor
	case AAUDIO_ERROR_INVALID_STATE:    errno = EBADFD;       return OP_ERROR_ERRNO; // File descriptor in bad state
	case AAUDIO_ERROR_ILLEGAL_ARGUMENT: errno = EINVAL;       return OP_ERROR_ERRNO; // Invalid argument
	default:                                                  return OP_ERROR_INTERNAL;
	}
}
// note: all options require restarting the output stream to apply
static aaudio_performance_mode_t op_aaudio_opt_performance_mode = AAUDIO_PERFORMANCE_MODE_POWER_SAVING;
static aaudio_allowed_capture_policy_t op_aaudio_opt_allowed_capture = AAUDIO_ALLOW_CAPTURE_BY_ALL;
static aaudio_sharing_mode_t op_aaudio_opt_sharing_mode = AAUDIO_SHARING_MODE_SHARED;
static bool op_aaudio_opt_disable_spatialization = false;
static int op_aaudio_opt_min_buffer_capacity_ms = 0;

// if we ever decide to support AAUDIO_PERFORMANCE_MODE_LOW_LATENCY streams,
// note that disconnection is broken for shared low-latency streams on RQ1A
// (this doesn't affect us right now since we don't use low-latency shared mmap
// streams)
//
// https://issuetracker.google.com/issues/173928197

static int op_aaudio_set_performance_mode(const char *val)
{
	if (!strcmp(val, "none")) {
		op_aaudio_opt_performance_mode = AAUDIO_PERFORMANCE_MODE_NONE;
		return OP_ERROR_SUCCESS;
	}
	if (!strcmp(val, "power_saving")) {
		op_aaudio_opt_performance_mode = AAUDIO_PERFORMANCE_MODE_POWER_SAVING;
		return OP_ERROR_SUCCESS;
	}
	errno = EINVAL;
	return -OP_ERROR_ERRNO;
}

static int op_aaudio_get_performance_mode(char **val)
{
	switch (op_aaudio_opt_performance_mode) {
	default:
		__attribute__((fallthrough));
	case AAUDIO_PERFORMANCE_MODE_NONE:
		*val = xstrdup("none");
		break;
	case AAUDIO_PERFORMANCE_MODE_POWER_SAVING:
		*val = xstrdup("power_saving");
		break;
	}
	return OP_ERROR_SUCCESS;
}

static int op_aaudio_set_allowed_capture(const char *val)
{
	if (!strcmp(val, "all")) {
		op_aaudio_opt_allowed_capture = AAUDIO_ALLOW_CAPTURE_BY_ALL;
		return OP_ERROR_SUCCESS;
	}
	if (!strcmp(val, "none")) {
		op_aaudio_opt_allowed_capture = AAUDIO_ALLOW_CAPTURE_BY_NONE;
		return OP_ERROR_SUCCESS;
	}
	if (!strcmp(val, "system")) {
		op_aaudio_opt_allowed_capture = AAUDIO_ALLOW_CAPTURE_BY_SYSTEM;
		return OP_ERROR_SUCCESS;
	}
	errno = EINVAL;
	return -OP_ERROR_ERRNO;
}

static int op_aaudio_get_allowed_capture(char **val)
{
	switch (op_aaudio_opt_allowed_capture) {
	default:
		__attribute__((fallthrough));
	case AAUDIO_ALLOW_CAPTURE_BY_ALL:
		*val = xstrdup("all");
		break;
	case AAUDIO_ALLOW_CAPTURE_BY_NONE:
		*val = xstrdup("none");
		break;
	case AAUDIO_ALLOW_CAPTURE_BY_SYSTEM:
		*val = xstrdup("system");
		break;
	}
	return OP_ERROR_SUCCESS;
}

static int op_aaudio_set_sharing_mode(const char *val)
{
	if (!strcmp(val, "shared")) {
		op_aaudio_opt_sharing_mode = AAUDIO_SHARING_MODE_SHARED;
		return OP_ERROR_SUCCESS;
	}
	if (!strcmp(val, "exclusive")) {
		op_aaudio_opt_sharing_mode = AAUDIO_SHARING_MODE_EXCLUSIVE;
		return OP_ERROR_SUCCESS;
	}
	errno = EINVAL;
	return -OP_ERROR_ERRNO;
}

static int op_aaudio_get_sharing_mode(char **val)
{
	switch (op_aaudio_opt_performance_mode) {
	default:
		__attribute__((fallthrough));
	case AAUDIO_SHARING_MODE_SHARED:
		*val = xstrdup("shared");
		break;
	case AAUDIO_SHARING_MODE_EXCLUSIVE:
		*val = xstrdup("exclusive");
		break;
	}
	return OP_ERROR_SUCCESS;
}

static int op_aaudio_set_disable_spatialization(const char *val)
{
	op_aaudio_opt_disable_spatialization = strcmp(val, "true") ? false : true;
	return OP_ERROR_SUCCESS;
}

static int op_aaudio_get_disable_spatialization(char **val)
{
	*val = xstrdup(op_aaudio_opt_disable_spatialization ? "true" : "false");
	return OP_ERROR_SUCCESS;
}

static int op_aaudio_set_min_buffer_capacity_ms(const char *val)
{
	long tmp;
	if (str_to_int(val, &tmp) == -1 || tmp < 0 || tmp > 1000) {
		errno = EINVAL;
		return -OP_ERROR_ERRNO;
	}
	op_aaudio_opt_min_buffer_capacity_ms = (int) tmp;
	return OP_ERROR_SUCCESS;
}

static int op_aaudio_get_min_buffer_capacity_ms(char **val)
{
	char tmp[5];
	snprintf(tmp, sizeof(tmp), "%d", op_aaudio_opt_min_buffer_capacity_ms);
	*val = xstrdup(tmp);
	return OP_ERROR_SUCCESS;
}

static bool aaudio_supported() {
	if (API_AT_LEAST(27)) {} else {
		// don't use AAudio on API 26 due to bug causing crash on some
		// devices when closing stream
		//
		// https://github.com/google/oboe/issues/40
		return -OP_ERROR_NOT_SUPPORTED;
	}
	if (API_AT_LEAST(AAUDIO_MINIMUM_API)) {
		return !!&AAudio_createStreamBuilder;
	}
	return false;
}

static struct {
	AAudioStream *stream;
	int32_t device;
	aaudio_result_t error;
	sample_format_t sf;
	ssize_t *remap;
	char *remap_buf;
} op;

int mixer_notify_output_in, mixer_notify_output_out;

static int op_aaudio_init(void)
{
	if (!aaudio_supported()) {
		// skip the output plugin (see op_select_any)
		return -OP_ERROR_NOT_SUPPORTED;
	}

	init_pipes(&mixer_notify_output_out, &mixer_notify_output_in);

	return OP_ERROR_SUCCESS;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_exit(void)
{
	close(mixer_notify_output_out);
	close(mixer_notify_output_in);

	return OP_ERROR_SUCCESS;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static void handle_error(AAudioStream *stream, void *userData, aaudio_result_t error) {
	if (error == AAUDIO_ERROR_DISCONNECTED) {
		notify_via_pipe(mixer_notify_output_in);
	}
	d_print("stream errored (%d - %s)\n", error, AAudio_convertResultToText(error));
	op.error = error;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_open(sample_format_t sf, const channel_position_t *channel_map)
{
	aaudio_result_t rc;
	AAudioStreamBuilder *bld;

	// create the stream builder
	rc = AAudio_createStreamBuilder(&bld);
	if (rc) {
		d_print("create stream builder failed (%d - %s)\n", rc, AAudio_convertResultToText(rc));
		return -OP_ERROR_AAUDIO(rc);
	}

	// set the error callback
	AAudioStreamBuilder_setErrorCallback(bld, handle_error, NULL);

	// apply the options
	AAudioStreamBuilder_setSharingMode(bld, op_aaudio_opt_sharing_mode);
	AAudioStreamBuilder_setPerformanceMode(bld, op_aaudio_opt_performance_mode);
	if (API_AT_LEAST(28)) AAudioStreamBuilder_setContentType(bld, AAUDIO_CONTENT_TYPE_MUSIC);
	if (API_AT_LEAST(28)) AAudioStreamBuilder_setUsage(bld, AAUDIO_USAGE_MEDIA);
	if (API_AT_LEAST(29)) AAudioStreamBuilder_setAllowedCapturePolicy(bld, op_aaudio_opt_allowed_capture);
	if (API_AT_LEAST(31)) AAudioStreamBuilder_setAttributionTag(bld, "cmus");
	if (API_AT_LEAST(32)) AAudioStreamBuilder_setSpatializationBehavior(bld, op_aaudio_opt_disable_spatialization ? AAUDIO_SPATIALIZATION_BEHAVIOR_NEVER : AAUDIO_SPATIALIZATION_BEHAVIOR_AUTO);

	// ensure the buffer holds at least the requested amount of audio (default 80ms)
	AAudioStreamBuilder_setBufferCapacityInFrames(bld, sf_get_rate(sf) / (1000 / (op_aaudio_opt_min_buffer_capacity_ms ? op_aaudio_opt_min_buffer_capacity_ms : 80)));

	// configure the sample format and channel map
	rc = configure_aaudio_sf(bld, sf, channel_map, &op.remap);
	if (rc) {
		d_print("configure format failed (%d - %s)\n", rc, AAudio_convertResultToText(rc));
		return -OP_ERROR_AAUDIO(rc);
	}
	if (op.remap) {
		d_print("allocating %zu bytes for remap buffer\n", (size_t) AAudioStream_getBufferCapacityInFrames(op.stream) * (size_t) sf_get_frame_size(sf));
		op.remap_buf = xmalloc((size_t) AAudioStream_getBufferCapacityInFrames(op.stream) * (size_t) sf_get_frame_size(sf));
	}
	op.sf = sf;

	// open the stream
	op.error = 0;
	rc = AAudioStreamBuilder_openStream(bld, &op.stream);
	if (rc) {
		d_print("open stream failed (%d - %s)\n", rc, AAudio_convertResultToText(rc));
		if (op.remap_buf) {
			free(op.remap_buf);
			op.remap_buf = NULL;
		}
		if (op.remap) {
			free(op.remap);
			op.remap = NULL;
		}
		AAudioStreamBuilder_delete(bld);
		return -OP_ERROR_AAUDIO(rc);
	}
	op.device = AAudioStream_getDeviceId(op.stream);

	d_print("optimal buffer frames = %d\n", AAudioStream_getFramesPerBurst(op.stream));
	d_print("buffer capacity frames = %d\n", AAudioStream_getBufferCapacityInFrames(op.stream));


	// cleanup the stream builder
	rc = AAudioStreamBuilder_delete(bld);
	if (rc) {
		d_print("delete stream builder failed (%d - %s)\n", rc, AAudio_convertResultToText(rc));
		if (op.remap_buf) {
			free(op.remap_buf);
			op.remap_buf = NULL;
		}
		if (op.remap) {
			free(op.remap);
			op.remap = NULL;
		}
		AAudioStream_close(op.stream);
		return -OP_ERROR_AAUDIO(rc);
	}

	// done (we don't actually start the stream until the first write)
	return OP_ERROR_SUCCESS;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_close(void)
{
	if (op.remap_buf) {
		free(op.remap_buf);
		op.remap_buf = NULL;
	}
	if (op.remap) {
		free(op.remap);
		op.remap = NULL;
	}
	if (op.stream) {
		AAudioStream_close(op.stream);
		op.stream = NULL;
	}
	return OP_ERROR_SUCCESS;
}



REQUIRES_API(AAUDIO_MINIMUM_API)
static aaudio_result_t do_state_change(aaudio_result_t (*request)(AAudioStream *strm), aaudio_stream_state_t state, aaudio_stream_state_t state2)
{
	aaudio_result_t rc;

	if (op.error) {
		rc = op.error;
		return rc;
	}

	if (request) {
		d_print("request state change\n");
		rc = request(op.stream);
		if (rc) {
			return rc;
		}
	}

	d_print("wait state change (%d:%s || %d:%s)\n", state, AAudio_convertStreamStateToText(state), state2, AAudio_convertStreamStateToText(state2));
	aaudio_stream_state_t currentState = AAUDIO_STREAM_STATE_UNKNOWN;
	aaudio_stream_state_t inputState = currentState;
	rc = AAUDIO_OK;
	while (rc == AAUDIO_OK && currentState != state && (state2 == 0 || currentState != state2)) {
		// this is required to prevent hanging during pause_on_output_change
		if (op.error) {
			rc = op.error;
			break;
		}
		if (currentState == AAUDIO_STREAM_STATE_CLOSING || currentState == AAUDIO_STREAM_STATE_CLOSED || currentState == AAUDIO_STREAM_STATE_DISCONNECTED) {
			rc = AAUDIO_ERROR_DISCONNECTED;
			break;
		}
		d_print("current state change %d\r\n", currentState);
		rc = AAudioStream_waitForStateChange(op.stream, inputState, &currentState, INT64_MAX);
		inputState = currentState;
	}
	if (rc) {
		d_print("failed state change (%d - %s) [current=%d:%s]\n", rc, AAudio_convertResultToText(rc), currentState, AAudio_convertStreamStateToText(currentState));
	} else {
		d_print("done state change [current=%d:%s]\n", currentState, AAudio_convertStreamStateToText(currentState));
	}
	return rc;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_drop(void)
{
	aaudio_result_t rc;
	aaudio_stream_state_t orig_state = AAudioStream_getState(op.stream);

	// we can't flush if it's closing
	if (orig_state == AAUDIO_STREAM_STATE_CLOSING || orig_state == AAUDIO_STREAM_STATE_CLOSED) {
		return -OP_ERROR_NOT_OPEN;
	}

	// only flush if it isn't already flushed or closed
	if (orig_state != AAUDIO_STREAM_STATE_FLUSHED) {

		// the stream must be paused to be flushed
		if (orig_state == AAUDIO_STREAM_STATE_STARTED || orig_state == AAUDIO_STREAM_STATE_STARTING) {
			rc = do_state_change(AAudioStream_requestPause, AAUDIO_STREAM_STATE_PAUSED, 0);
			if (rc) {
				return -OP_ERROR_AAUDIO(rc);
			}
			// the stream will be started again on the first write
		}

		// flush the stream
		rc = do_state_change(AAudioStream_requestFlush, AAUDIO_STREAM_STATE_FLUSHED, 0);
		if (rc) {
			return -OP_ERROR_AAUDIO(rc);
		}
	}

	return OP_ERROR_SUCCESS;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_write(const char *buf, int count)
{
	int32_t device;
	aaudio_result_t rc;
	aaudio_stream_state_t state;

	// if the stream errored, return an error so cmus restarts the output
	// plugin
	//
	// note that this isn't strictly required since AAudioStream_write will
	// return an error on stream disconnection, which will cause cmus to
	// reopen the output plugin
	//
	// https://github.com/google/oboe/wiki/TechNote_Disconnect
	if (op.error) {
		return -OP_ERROR_AAUDIO(op.error);
	}

	// note: this is cheap; it's just a field getter internally
	device = AAudioStream_getDeviceId(op.stream);
	if (op.device != device) {
		if (op.device != -1) {
			notify_via_pipe(mixer_notify_output_in);
		}
		op.device = device;
	}

	// start the stream on the first write (rather than after opening or
	// flushing since cmus may not always use the stream and starting a
	// stream is somewhat expensive)
	//
	// note: this is cheap; it's just a atomic field getter internally
	state = AAudioStream_getState(op.stream);
	if (state == AAUDIO_STREAM_STATE_CLOSING || state == AAUDIO_STREAM_STATE_CLOSED) {
		return -OP_ERROR_NOT_OPEN;
	}
	if (state != AAUDIO_STREAM_STATE_STARTING && state != AAUDIO_STREAM_STATE_STARTED) {
		rc = do_state_change(AAudioStream_requestStart, AAUDIO_STREAM_STATE_STARTED, AAUDIO_STREAM_STATE_STARTING);
		if (rc) {
			return -OP_ERROR_AAUDIO(rc);
		}
	}

	// this should never happen since op_aaudio_buffer_space should always
	// be less than AAudioStream_getBufferCapacityInFrames, and cmus
	// determines how much to write using it
	BUG_ON(count >= AAudioStream_getBufferCapacityInFrames(op.stream) * sf_get_frame_size(op.sf));

	// remap if necessary
	buf = (char *) apply_channel_remap((uint8_t *) op.remap_buf, (uint8_t *) buf, count, op.sf, op.remap);

	// synchronously write the samples to the buffer
	rc = AAudioStream_write(op.stream, buf, count / (int) sf_get_frame_size(op.sf), INT64_MAX);
	if (rc < 0) {
		d_print("write %d = error %d - %s [device=%d] [state=%d]\n", count / (int) sf_get_frame_size(op.sf), rc, AAudio_convertResultToText(rc), device, state);
		return -OP_ERROR_AAUDIO(rc);
	}
	d_print("write %d = %d (* %d bytes) [device=%d] [state=%d]\n", count / (int) sf_get_frame_size(op.sf), rc, (int) sf_get_frame_size(op.sf), device, state);

	// return the number of bytes we write
	return (int) sf_get_frame_size(op.sf) * rc;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_pause(void)
{
	// request stream pause, wait until it completes
	return -OP_ERROR_AAUDIO(do_state_change(AAudioStream_requestPause, AAUDIO_STREAM_STATE_PAUSED, 0));
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_unpause(void)
{
	// request stream start, wait until it starts to start (i.e., will start
	// consuming frames written to it)
	return -OP_ERROR_AAUDIO(do_state_change(AAudioStream_requestStart, AAUDIO_STREAM_STATE_STARTED, AAUDIO_STREAM_STATE_STARTING));
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_buffer_space(void)
{
	int32_t optimal, nonblock;

	// optimal buffer amount (anecdotally, this generally seems to be less
	// than half the buffer capacity)
	optimal = AAudioStream_getFramesPerBurst(op.stream) * (int32_t) sf_get_frame_size(op.sf);

	// max buffer amount (without blocking)
	nonblock = AAudioStream_getBufferSizeInFrames(op.stream) * (int32_t) sf_get_frame_size(op.sf);

	// want to write the optimal amount (up to the nonblock amount)
	return optimal < nonblock ? optimal : nonblock;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_mixer_init(void)
{
	if (!aaudio_supported()) {
		// skip the output plugin (see op_select_any)
		return -OP_ERROR_NOT_SUPPORTED;
	}
	return OP_ERROR_SUCCESS;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_mixer_exit(void)
{
	return OP_ERROR_SUCCESS;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_mixer_open(int *volume_max)
{
	*volume_max = UINT16_MAX;

	return OP_ERROR_SUCCESS;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_mixer_close(void)
{
	return OP_ERROR_SUCCESS;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_mixer_get_fds(int what, int *fds)
{
	switch (what) {
	case MIXER_FDS_OUTPUT:
		fds[0] = mixer_notify_output_out;
		return 1;
	default:
		return 0;
	}
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_mixer_set_volume(int l, int r)
{
	return -OP_ERROR_NOT_SUPPORTED;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
static int op_aaudio_mixer_get_volume(int *l, int *r)
{
	// aaudio doesn't support volume control, so say the volume is 100%
	*l = *r = UINT16_MAX;

	return OP_ERROR_SUCCESS;
}

REQUIRES_API(AAUDIO_MINIMUM_API)
const struct output_plugin_ops op_pcm_ops = {
	.init = op_aaudio_init,
	.exit = op_aaudio_exit,
	.open = op_aaudio_open,
	.close = op_aaudio_close,
	.drop = op_aaudio_drop,
	.write = op_aaudio_write,
	.pause = op_aaudio_pause,
	.unpause = op_aaudio_unpause,
	.buffer_space = op_aaudio_buffer_space,
};

REQUIRES_API(AAUDIO_MINIMUM_API)
const struct mixer_plugin_ops op_mixer_ops = {
	.init = op_aaudio_mixer_init,
	.exit = op_aaudio_mixer_exit,
	.open = op_aaudio_mixer_open,
	.close = op_aaudio_mixer_close,
	.get_fds.abi_2 = op_aaudio_mixer_get_fds,
	.set_volume = op_aaudio_mixer_set_volume,
	.get_volume = op_aaudio_mixer_get_volume,
};

const struct output_plugin_opt op_pcm_options[] = {
	OPT(op_aaudio, performance_mode),
	OPT(op_aaudio, allowed_capture),
	OPT(op_aaudio, sharing_mode),
	OPT(op_aaudio, disable_spatialization),
	OPT(op_aaudio, min_buffer_capacity_ms),
	{ NULL },
};

const struct mixer_plugin_opt op_mixer_options[] = {
	{ NULL },
};

const int op_priority = -3; // higher priority than pulse (-2)
const unsigned op_abi_version = OP_ABI_VERSION;
