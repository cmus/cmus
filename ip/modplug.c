/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2005 Timo Hirvonen
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
#include "../file.h"
#include "../xmalloc.h"
#include "../comment.h"
#ifdef HAVE_CONFIG
#include "../config/modplug.h"
#endif

#include <libmodplug/modplug.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

struct mod_private {
	ModPlugFile *file;
};

static int mod_open(struct input_plugin_data *ip_data)
{
	struct mod_private *priv;
	char *contents;
	off_t size;
	ssize_t rc;
	ModPlugFile *file;
	ModPlug_Settings settings;

	size = lseek(ip_data->fd, 0, SEEK_END);
	if (size == -1)
		return -IP_ERROR_ERRNO;
	if (lseek(ip_data->fd, 0, SEEK_SET) == -1)
		return -IP_ERROR_ERRNO;

	contents = xnew(char, size);
	rc = read_all(ip_data->fd, contents, size);
	if (rc == -1) {
		int save = errno;

		free(contents);
		errno = save;
		return -IP_ERROR_ERRNO;
	}
	if (rc != size) {
		free(contents);
		return -IP_ERROR_FILE_FORMAT;
	}
	errno = 0;
	file = ModPlug_Load(contents, size);
	if (file == NULL) {
		int save = errno;

		free(contents);
		errno = save;
		if (errno == 0) {
			/* libmodplug never sets errno? */
			return -IP_ERROR_FILE_FORMAT;
		}
		return -IP_ERROR_ERRNO;
	}
	free(contents);

	priv = xnew(struct mod_private, 1);
	priv->file = file;

	ModPlug_GetSettings(&settings);
	settings.mFlags = MODPLUG_ENABLE_OVERSAMPLING | MODPLUG_ENABLE_NOISE_REDUCTION;
/* 	settings.mFlags |= MODPLUG_ENABLE_REVERB; */
/* 	settings.mFlags |= MODPLUG_ENABLE_MEGABASS; */
/* 	settings.mFlags |= MODPLUG_ENABLE_SURROUND; */
	settings.mChannels = 2;
	settings.mBits = 16;
	settings.mFrequency = 44100;
	settings.mResamplingMode = MODPLUG_RESAMPLE_FIR;
	ModPlug_SetSettings(&settings);

	ip_data->private = priv;
	ip_data->sf = sf_bits(16) | sf_rate(44100) | sf_channels(2) | sf_signed(1);
	ip_data->sf |= sf_host_endian();
	channel_map_init_stereo(ip_data->channel_map);
	return 0;
}

static int mod_close(struct input_plugin_data *ip_data)
{
	struct mod_private *priv = ip_data->private;

	ModPlug_Unload(priv->file);
	free(priv);
	ip_data->private = NULL;
	return 0;
}

static int mod_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct mod_private *priv = ip_data->private;
	int rc;

	errno = 0;
	rc = ModPlug_Read(priv->file, buffer, count);
	if (rc < 0) {
		if (errno == 0)
			return -IP_ERROR_INTERNAL;
		return -IP_ERROR_ERRNO;
	}
	return rc;
}

static int mod_seek(struct input_plugin_data *ip_data, double offset)
{
	struct mod_private *priv = ip_data->private;
	int ms = (int)(offset * 1000.0 + 0.5);

	/* void */
	ModPlug_Seek(priv->file, ms);
	return 0;
}

static int mod_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	struct mod_private *priv = ip_data->private;
	GROWING_KEYVALS(c);
	const char *val;

	val = ModPlug_GetName(priv->file);
	if (val && val[0])
		comments_add_const(&c, "title", val);

#if MODPLUG_API_8
	val = ModPlug_GetMessage(priv->file);
	if (val && val[0])
		comments_add_const(&c, "comment", val);
#endif

	keyvals_terminate(&c);
	*comments = c.keyvals;
	return 0;
}

static int mod_duration(struct input_plugin_data *ip_data)
{
	struct mod_private *priv = ip_data->private;

	return (ModPlug_GetLength(priv->file) + 500) / 1000;
}

static long mod_bitrate(struct input_plugin_data *ip_data)
{
	return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

#if MODPLUG_API_8
static const char *mod_type_to_string(int type)
{
	/* from <libmodplug/sndfile.h>, which is C++ */
	switch (type) {
	case 0x01:	return "mod";
	case 0x02:	return "s3m";
	case 0x04:	return "xm";
	case 0x08:	return "med";
	case 0x10:	return "mtm";
	case 0x20:	return "it";
	case 0x40:	return "699";
	case 0x80:	return "ult";
	case 0x100:	return "stm";
	case 0x200:	return "far";
	case 0x800:	return "amf";
	case 0x1000:	return "ams";
	case 0x2000:	return "dsm";
	case 0x4000:	return "mdl";
	case 0x8000:	return "okt";
	case 0x10000:	return "midi";
	case 0x20000:	return "dmf";
	case 0x40000:	return "ptm";
	case 0x80000:	return "dbm";
	case 0x100000:	return "mt2";
	case 0x200000:	return "amf0";
	case 0x400000:	return "psm";
	case 0x80000000:return "umx";
	}
	return NULL;
}
#endif

static char *mod_codec(struct input_plugin_data *ip_data)
{
#if MODPLUG_API_8
	struct mod_private *priv = ip_data->private;
	const char *codec;
	int type;

	type = ModPlug_GetModuleType(priv->file);
	codec = mod_type_to_string(type);

	return codec ? xstrdup(codec) : NULL;
#else
	return NULL;
#endif
}

static char *mod_codec_profile(struct input_plugin_data *ip_data)
{
	return NULL;
}

const struct input_plugin_ops ip_ops = {
	.open = mod_open,
	.close = mod_close,
	.read = mod_read,
	.seek = mod_seek,
	.read_comments = mod_read_comments,
	.duration = mod_duration,
	.bitrate = mod_bitrate,
	.bitrate_current = mod_bitrate,
	.codec = mod_codec,
	.codec_profile = mod_codec_profile
};

const int ip_priority = 50;
const char * const ip_extensions[] = {
	"mod", "s3m", "xm", "it", "669", "amf", "ams", "dbm", "dmf", "dsm",
	"far", "mdl", "med", "mtm", "okt", "ptm", "stm", "ult", "umx", "mt2",
	"psm", NULL
};
const char * const ip_mime_types[] = { NULL };
const struct input_plugin_opt ip_options[] = { { NULL } };
const unsigned ip_abi_version = IP_ABI_VERSION;
