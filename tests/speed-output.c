/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only output: record PCM, exercise short writes and pace playback. */
#include "op.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static FILE *capture;
static sample_format_t format;
static double deadline;
static int writes;

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int output_init(void) { return 0; }
static int output_exit(void) { return 0; }
static int output_open(sample_format_t sf, const channel_position_t *map)
{
	format = sf;
	deadline = now();
	capture = fopen(getenv("CMUS_TEST_CAPTURE"), "ab");
	return capture ? 0 : -1;
}
static int output_close(void)
{
	if (capture)
		fclose(capture);
	capture = NULL;
	return 0;
}
static int output_drop(void) { deadline = now(); return 0; }
static int output_pause(void) { return 0; }
static int output_unpause(void) { deadline = now(); return 0; }
static int output_space(void)
{
	if (getenv("CMUS_TEST_FAST"))
		return 16384 * sf_get_frame_size(format);
	if (deadline <= now() + 0.02)
		return 1024 * sf_get_frame_size(format);
	return 0;
}
static int output_write(const char *buf, int count)
{
	int frame = sf_get_frame_size(format);
	if (++writes % 101 == 0)
		return 0;
	if (count > 113 * frame)
		count = 113 * frame;
	count = fwrite(buf, 1, count, capture);
	fflush(capture);
	deadline += (double)count / sf_get_second_size(format);
	return count;
}

const struct output_plugin_ops op_pcm_ops = {
	.init = output_init, .exit = output_exit,
	.open = output_open, .close = output_close, .drop = output_drop,
	.write = output_write, .buffer_space = output_space,
	.pause = output_pause, .unpause = output_unpause,
};
const struct output_plugin_opt op_pcm_options[] = { { NULL } };
const int op_priority = 100;
const unsigned op_abi_version = OP_ABI_VERSION;
