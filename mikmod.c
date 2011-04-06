/*
 * Adapted from AlsaPlayer 0.99.76
 *
 * mikmod_engine.c
 * Copyright (C) 1999 Paul N. Fisher <rao@gnu.org>
 * Copyright (C) 2002 Andy Lo A Foe <andy@alsaplayer.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "ip.h"
#include "xmalloc.h"
#include <mikmod.h>
#include "debug.h"

struct mik_private {
	MODULE *file;
};

static int mikmod_init(void)
{
	static int inited = 0;

	if (inited)
		return 1;

	MikMod_RegisterAllDrivers();
	MikMod_RegisterAllLoaders();

	md_reverb = 0;
	/* we should let the user decide which one is better... */
	md_mode = DMODE_SOFT_MUSIC | DMODE_SOFT_SNDFX | DMODE_STEREO |
		DMODE_16BITS | DMODE_INTERP;

	if (MikMod_Init(NULL)) {
		d_print("Could not initialize mikmod, reason: %s\n",
				MikMod_strerror(MikMod_errno));
		return 0;
	}

	inited = 1;
	return 1;
}

static int mik_open(struct input_plugin_data *ip_data)
{
	MODULE *mf = NULL;
	struct mik_private *priv;
	int mi = mikmod_init();

	if (!mi)
		return -IP_ERROR_INTERNAL;

	mf = Player_Load(ip_data->filename, 255, 0);

	if (!mf)
		return -IP_ERROR_ERRNO;

	priv = xnew(struct mik_private, 1);
	priv->file = mf;

	ip_data->private = priv;
	ip_data->sf = sf_bits(16) | sf_rate(44100) | sf_channels(2) | sf_signed(1);
#ifdef WORDS_BIGENDIAN
	ip_data->sf |= sf_bigendian(1);
#endif
	return 0;
}

static int mik_close(struct input_plugin_data *ip_data)
{
	struct mik_private *priv = ip_data->private;

	Player_Stop();
	Player_Free(priv->file);
	free(ip_data->private);
	ip_data->private = NULL;
	return 0;
}

static int mik_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	int length;
	struct mik_private *priv = ip_data->private;

	if (!Player_Active())
		Player_Start(priv->file);

	if (!Player_Active())
		return 0;

	length = VC_WriteBytes(buffer, count);

	return length;
}

static int mik_seek(struct input_plugin_data *ip_data, double offset)
{
	/* cannot seek in modules properly */
	return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

static int mik_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	struct mik_private *priv = ip_data->private;
	GROWING_KEYVALS(c);
	const char *val;

	val = priv->file->songname;
	if (val && val[0])
		comments_add_const(&c, "title", val);

	val = priv->file->comment;
	if (val && val[0])
		comments_add_const(&c, "comment", val);

	keyvals_terminate(&c);
	*comments = c.keyvals;
	return 0;
}

static int mik_duration(struct input_plugin_data *ip_data)
{
	return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
}

const struct input_plugin_ops ip_ops = {
	.open = mik_open,
	.close = mik_close,
	.read = mik_read,
	.seek = mik_seek,
	.read_comments = mik_read_comments,
	.duration = mik_duration
};

const char * const ip_extensions[] = {
	"mod", "s3m", "xm", "it", "669", "amf", "dsm",
	"far", "med", "mtm", "stm", "ult",
	NULL
};

const char * const ip_mime_types[] = { NULL };
