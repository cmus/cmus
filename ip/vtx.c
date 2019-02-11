/*
 * Copyright 2014 Boris Timofeev <mashin87@gmail.com>
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

#include "../comment.h"
#include "../debug.h"
#include "../ip.h"
#include "../utils.h"
#include "../xmalloc.h"
#include <ayemu.h>
#include <string.h>

struct vtx_private {
	ayemu_ay_t ay;
	ayemu_ay_reg_frame_t regs;
	ayemu_vtx_t *vtx;
	int pos;
	int left;
};

static const int sample_rate = 44100;
static const int channels = 2;
static const int bits = 16;

static int vtx_open(struct input_plugin_data *ip_data)
{
	struct vtx_private *priv;
	priv = xnew(struct vtx_private, 1);
	ip_data->private = priv;

	priv->vtx = ayemu_vtx_load_from_file(ip_data->filename);
	if (!priv->vtx) {
		d_print("error: failed to open file %s\n", ip_data->filename);
		free(priv);
		return -IP_ERROR_INTERNAL;
	}

	ayemu_init(&priv->ay);
	ayemu_set_sound_format(&priv->ay, sample_rate, channels, bits);
	ayemu_set_chip_type(&priv->ay, priv->vtx->chiptype, NULL);
	ayemu_set_chip_freq(&priv->ay, priv->vtx->chipFreq);
	ayemu_set_stereo(&priv->ay, priv->vtx->stereo, NULL);

	ip_data->sf = sf_bits(bits) | sf_rate(sample_rate) | sf_channels(channels) | sf_signed(1);
	ip_data->sf |= sf_host_endian();
	channel_map_init_stereo(ip_data->channel_map);

	priv->pos = 0;
	priv->left = 0;

	return 0;
}

static int vtx_close(struct input_plugin_data *ip_data)
{
	struct vtx_private *priv = ip_data->private;

	ayemu_vtx_free(priv->vtx);
	free(priv);
	ip_data->private = NULL;
	return 0;
}

static int vtx_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct vtx_private *priv = ip_data->private;
	int need = count;
	int donow = 0;

	while (need > 0) {
		if (priv->left > 0) {
			donow = min_i(need, priv->left);
			buffer = ayemu_gen_sound(&priv->ay, (char *)buffer, donow);
			priv->left -= donow;
			need -= donow;
		} else {
			if (priv->pos >= priv->vtx->frames)
				return 0;
			ayemu_vtx_getframe(priv->vtx, priv->pos++, priv->regs);
			ayemu_set_regs(&priv->ay, priv->regs);
			priv->left = (sample_rate / priv->vtx->playerFreq) * (channels * bits / 8);
		}
	}

	return count;
}

static int vtx_seek(struct input_plugin_data *ip_data, double offset)
{
	struct vtx_private *priv = ip_data->private;
	int sample = sample_rate * offset;
	int samples_per_frame = sample_rate / priv->vtx->playerFreq;
	priv->pos = sample / samples_per_frame;
	if (priv->pos >= priv->vtx->frames) {
		return 0;
	}
	ayemu_vtx_getframe(priv->vtx, priv->pos, priv->regs);
	priv->left = samples_per_frame - (sample % samples_per_frame);
	return 0;
}

static int vtx_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	struct vtx_private *priv = ip_data->private;
	GROWING_KEYVALS(c);
	const char *str;

	str = priv->vtx->author;
	if (str && str[0])
		comments_add_const(&c, "artist", str);
	str = priv->vtx->from;
	if (str && str[0])
		comments_add_const(&c, "album", str);
	str = priv->vtx->title;
	if (str && str[0])
		comments_add_const(&c, "title", str);
	int year = priv->vtx->year;
	if (year > 0) {
		char buf[16] = {0};
		snprintf(buf, sizeof buf, "%d", year);
		comments_add_const(&c, "date", buf);
	}
	str = priv->vtx->comment;
	if (str && str[0])
		comments_add_const(&c, "comment", str);

	keyvals_terminate(&c);
	*comments = c.keyvals;
	return 0;
}

static int vtx_duration(struct input_plugin_data *ip_data)
{
	struct vtx_private *priv = ip_data->private;

	return (int)(priv->vtx->frames / priv->vtx->playerFreq);
}

static long vtx_bitrate(struct input_plugin_data *ip_data)
{
	return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

static long vtx_current_bitrate(struct input_plugin_data *ip_data)
{
	return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

static char *vtx_codec(struct input_plugin_data *ip_data)
{
	return xstrdup("vtx");
}

static char *vtx_codec_profile(struct input_plugin_data *ip_data)
{
	return NULL;
}

const struct input_plugin_ops ip_ops = {
	.open = vtx_open,
	.close = vtx_close,
	.read = vtx_read,
	.seek = vtx_seek,
	.read_comments = vtx_read_comments,
	.duration = vtx_duration,
	.bitrate = vtx_bitrate,
	.bitrate_current = vtx_current_bitrate,
	.codec = vtx_codec,
	.codec_profile = vtx_codec_profile
};

const int ip_priority = 50;
const char * const ip_extensions[] = {"vtx", NULL};
const char * const ip_mime_types[] = { NULL };
const struct input_plugin_opt ip_options[] = { { NULL } };
const unsigned ip_abi_version = IP_ABI_VERSION;
