/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2016 Nic Soudée
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

#include "../ip.h"
#include "../xmalloc.h"
#include "../comment.h"
#include "../bass.h"
#include "../uchar.h"

#define BITS (16)
#define FREQ (44100)
#define CHANS (2)

struct bass_private {
	DWORD chan;
};

static int bass_init(void)
{
	static int inited = 0;

	if (inited)
		return 1;

	if (!BASS_Init(0, FREQ, 0, 0, NULL))
		return 0;

	inited = 1;
	return 1;
}

static int bass_open(struct input_plugin_data *ip_data)
{
	struct bass_private *priv;
	DWORD chan;
	DWORD flags;

	if (!bass_init())
		return -IP_ERROR_INTERNAL;

	flags = BASS_MUSIC_DECODE;
	flags |= BASS_MUSIC_RAMP;
	flags |= BASS_MUSIC_PRESCAN;
	flags |= BASS_MUSIC_STOPBACK;

	chan = BASS_MusicLoad(FALSE, ip_data->filename, 0, 0, flags, 0);

	if (!chan) {
		return -IP_ERROR_ERRNO;
	}

	priv = xnew(struct bass_private, 1);
	priv->chan = chan;
	ip_data->private = priv;
	ip_data->sf = sf_bits(BITS) | sf_rate(FREQ) | sf_channels(CHANS) | sf_signed(1);
	ip_data->sf |= sf_host_endian();
	channel_map_init_stereo(ip_data->channel_map);
	return 0;
}

static int bass_close(struct input_plugin_data *ip_data)
{
	struct bass_private *priv = ip_data->private;

	BASS_MusicFree(priv->chan);
	free(priv);
	ip_data->private = NULL;
	return 0;
}

static int bass_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	int length;
	struct bass_private *priv = ip_data->private;
	length = BASS_ChannelGetData(priv->chan, buffer, count);
	if (length < 0) {
		return 0;
	}
	return length;
}

static int bass_seek(struct input_plugin_data *ip_data, double offset)
{
	struct bass_private *priv = ip_data->private;
	QWORD pos = (QWORD)(offset * (FREQ * CHANS * (BITS / 8)) + 0.5);
	QWORD flags = BASS_POS_BYTE | BASS_POS_DECODE;

	if (!BASS_ChannelSetPosition(priv->chan, pos, flags)) {
		return -IP_ERROR_INTERNAL;
	}
	return 0;
}

static unsigned char *encode_ascii_string(const char *str)
{
	unsigned char *ret;
	int n;

	ret = malloc(strlen(str) + 1);
	n = u_to_ascii(ret, str, strlen(str));
	ret[n] = '\0';
	return ret;
}

static int bass_read_comments(struct input_plugin_data *ip_data,
				struct keyval **comments)
{
	struct bass_private *priv = ip_data->private;
	GROWING_KEYVALS(c);
	const char *val;

	val = BASS_ChannelGetTags(priv->chan, BASS_TAG_MUSIC_NAME);
	if (val && val[0])
		comments_add_const(&c, "title", encode_ascii_string(val));
	keyvals_terminate(&c);
	*comments = c.keyvals;
	return 0;
}

static int bass_duration(struct input_plugin_data *ip_data)
{
	static float length = 0;
	int pos;
	struct bass_private *priv = ip_data->private;

	pos = BASS_ChannelGetLength(priv->chan, BASS_POS_BYTE);
	if (pos && pos != -1) {
		length = BASS_ChannelBytes2Seconds(priv->chan, pos);
	}
	else {
		length = -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	}
	return length;
}

static long bass_bitrate(struct input_plugin_data *ip_data)
{
	return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

static const char *bass_type_to_string(int type)
{
	/* from <bass.h> */
	switch (type) {
		case 0x20000: return "mod";
		case 0x20001: return "mtm";
		case 0x20002: return "s3m";
		case 0x20003: return "xm";
		case 0x20004: return "it";
	}
	return NULL;
}

static char *bass_codec(struct input_plugin_data *ip_data)
{
	const char *codec;
	int type;
	BASS_CHANNELINFO info;
	struct bass_private *priv = ip_data->private;

	if (!(BASS_ChannelGetInfo(priv->chan, &info))) {
		return NULL;
	}
	type = info.ctype;
	codec = bass_type_to_string(type);
	return codec ? xstrdup(codec) : NULL;
}

static char *bass_codec_profile(struct input_plugin_data *ip_data)
{
	return NULL;
}

const struct input_plugin_ops ip_ops = {
	.open = bass_open,
	.close = bass_close,
	.read = bass_read,
	.seek = bass_seek,
	.read_comments = bass_read_comments,
	.duration = bass_duration,
	.bitrate = bass_bitrate,
	.bitrate_current = bass_bitrate,
	.codec = bass_codec,
	.codec_profile = bass_codec_profile
};

const int ip_priority = 60;
const char * const ip_extensions[] = {
	"xm", "it", "s3m", "mod", "mtm", "umx", NULL
};

const char * const ip_mime_types[] = { NULL };
const struct input_plugin_opt ip_options[] = { { NULL } };
const unsigned ip_abi_version = IP_ABI_VERSION;

