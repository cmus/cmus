/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004-2005 Timo Hirvonen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "../op.h"
#include "../xmalloc.h"
#include "../debug.h"

#include <artsc.h>

static arts_stream_t arts_stream;
static sample_format_t arts_sf;
static int arts_buffer_size;

static int op_arts_init(void)
{
	int rc;

	rc = arts_init();
	if (rc < 0) {
		return -1;
	}
	return 0;
}

static int op_arts_exit(void)
{
	arts_free();
	return 0;
}

static int op_arts_open(sample_format_t sf, const channel_position_t *channel_map)
{
	int buffer_time, server_latency, total_latency;
	int blocking;

	arts_sf = sf;
	arts_stream = arts_play_stream(sf_get_rate(arts_sf), sf_get_bits(arts_sf),
			sf_get_channels(arts_sf), "cmus");
	blocking = arts_stream_set(arts_stream, ARTS_P_BLOCKING, 0);
	if (blocking) {
	}
	arts_buffer_size = arts_stream_get(arts_stream, ARTS_P_BUFFER_SIZE);
	if (arts_buffer_size < 0) {
	}
	buffer_time = arts_stream_get(arts_stream, ARTS_P_BUFFER_TIME);
	server_latency = arts_stream_get(arts_stream, ARTS_P_SERVER_LATENCY);
	total_latency = arts_stream_get(arts_stream, ARTS_P_TOTAL_LATENCY);
	d_print("buffer_time: %d\n", buffer_time);
	d_print("server_latency: %d\n", server_latency);
	d_print("total_latency: %d\n", total_latency);
	return 0;
}

static int op_arts_close(void)
{
	arts_close_stream(arts_stream);
	return 0;
}

static int op_arts_write(const char *buffer, int count)
{
	int rc;

	rc = arts_write(arts_stream, buffer, count);
	if (rc < 0) {
		d_print("rc = %d, count = %d\n", rc, count);
		return -1;
	}
	return rc;
}

static int op_arts_pause(void)
{
	return 0;
}

static int op_arts_unpause(void)
{
	return 0;
}

static int op_arts_buffer_space(void)
{
	int space;

	space = arts_stream_get(arts_stream, ARTS_P_BUFFER_SPACE);
	if (space < 0)
		return -1;
	return space;
}

const struct output_plugin_ops op_pcm_ops = {
	.init = op_arts_init,
	.exit = op_arts_exit,
	.open = op_arts_open,
	.close = op_arts_close,
	.write = op_arts_write,
	.pause = op_arts_pause,
	.unpause = op_arts_unpause,
	.buffer_space = op_arts_buffer_space,
};

const struct output_plugin_opt op_pcm_options[] = {
	{ NULL },
};

const int op_priority = 4;
const unsigned op_abi_version = OP_ABI_VERSION;
