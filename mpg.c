/*
 * Copyright 2014-2014 Various Authors
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

#include "ip.h"
#include "xmalloc.h"
#include "read_wrapper.h"
#include "debug.h"
#include "comment.h"

#include <mpg123.h>

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

#define SAMPLING_RATE 44100

struct mpg_private {
	mpg123_handle *mh;
};

static __attribute__((constructor)) void init(void) 
{
	mpg123_init();
}

static int mpg_open(struct input_plugin_data *ip_data)
{
	struct mpg_private *priv;
	
	priv = xnew(struct mpg_private, 1);
	priv->mh = mpg123_new(NULL, NULL);

	if ( priv->mh == NULL
	  || mpg123_open_fd(priv->mh, ip_data->fd) != MPG123_OK
	  || mpg123_format_none(priv->mh) != MPG123_OK
	  || mpg123_format(priv->mh, SAMPLING_RATE, 2, MPG123_ENC_SIGNED_16) != MPG123_OK) {
		free(priv);
		return -IP_ERROR_INTERNAL;
	}
	mpg123_param(priv->mh, MPG123_ADD_FLAGS, MPG123_QUIET, 0);
	ip_data->private = priv;

	ip_data->sf = sf_rate(SAMPLING_RATE)
		| sf_channels(2)
		| sf_bits(16)
		| sf_signed(1);
	return 0;
}

static int mpg_close(struct input_plugin_data *ip_data)
{
	struct mpg_private *priv;
	
	priv = ip_data->private;
	mpg123_close(priv->mh);
	close(ip_data->fd);
	ip_data->fd = -1;
	free(priv);
	ip_data->private = NULL;
	return 0;
}

static int mpg_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct mpg_private *priv;
	size_t done;
	int rc;

	priv = ip_data->private;
again:
	rc = mpg123_read(priv->mh, (void *)buffer, count, &done);
	switch (rc) {
	case MPG123_OK:
	case MPG123_DONE:
		return done;
	case MPG123_NEW_FORMAT:
		goto again;
	default:
		return -IP_ERROR_FILE_FORMAT;
	}
}

static int mpg_seek(struct input_plugin_data *ip_data, double offset)
{
	struct mpg_private *priv;
	int rc;

	priv = ip_data->private;
	rc = mpg123_seek(priv->mh, offset * SAMPLING_RATE, SEEK_SET);
	switch (rc) {
	case MPG123_NO_SEEK:
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	default:
		return 0;
	}
}

#if defined __clang__ && defined FAST_ID3
  #define INIT() uint32_t buf; memcpy(&buf, v->text[i].id, 4);
  #define EQ(x) (buf == *((uint32_t *)x))
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wcast-align"
#else
  #define INIT()
  #define EQ(x) (memcmp(&v->text[i].id, x, 4) == 0)
#endif
#define ADD(x) { comments_add_const(&c, x, v->text[i].text.p); continue; }

static int mpg_read_comments(struct input_plugin_data *ip_data, struct keyval **comments)
{
	struct mpg_private *priv;
	int i;

	priv = ip_data->private;
	mpg123_decode_frame(priv->mh, NULL, NULL, NULL);
	mpg123_id3v2 *v = NULL;
	mpg123_id3(priv->mh, NULL, &v);

	GROWING_KEYVALS(c);
	if (v) {
		for (i = 0; i < v->texts; i++) {
			INIT();
			if (EQ("TYER") || EQ("TDRD") || EQ("TDRL")) ADD("date");
			if (EQ("TDOR") || EQ("TORY")) ADD("originaldate");
			if (EQ("TSOP")) ADD("artistsort");
			if (EQ("TSOA")) ADD("albumsort");
			if (EQ("TPE1")) ADD("artist");
			if (EQ("TALB")) ADD("album");
			if (EQ("TIT2")) ADD("title");
			if (EQ("TCON")) ADD("genre");
			if (EQ("TPOS")) ADD("discnumber");
			if (EQ("TRCK")) ADD("tracknumber");
			if (EQ("TPE2")) ADD("albumartist");
			if (EQ("TPE2")) ADD("albumartist");
			if (EQ("TSO2")) ADD("albumartistsort");
			if (EQ("TCMP")) ADD("compilation");
			if (EQ("TCOM")) ADD("composer");
			if (EQ("TPE3")) ADD("conductor");
			if (EQ("TEXT")) ADD("lyricist");
			if (EQ("TPE4")) ADD("remixer");
			if (EQ("TPUB")) ADD("publisher");
			if (EQ("TID3")) ADD("subtitle");
			if (EQ("TMED")) ADD("media");
		}
	}
	keyvals_terminate(&c);

	*comments = c.keyvals;
	return 0;
}

#if defined __clang__ && defined FAST_ID3
  #pragma clang diagnostic pop
#endif

static int mpg_duration(struct input_plugin_data *ip_data)
{
	struct mpg_private *priv;

	priv = ip_data->private;
	return mpg123_length(priv->mh)/SAMPLING_RATE;
}

static long mpg_current_bitrate(struct input_plugin_data *ip_data)
{
	struct mpg_private *priv;
	struct mpg123_frameinfo info;

	priv = ip_data->private;
	mpg123_info(priv->mh, &info);
	return info.bitrate;
}

static long mpg_bitrate(struct input_plugin_data *ip_data)
{
	return mpg_current_bitrate(ip_data);
}

static char *mpg_codec(struct input_plugin_data *ip_data)
{
	return xstrdup("mp3");
}

static char *mpg_codec_profile(struct input_plugin_data *ip_data)
{
	return NULL;
}

const struct input_plugin_ops ip_ops = {
	.open            = mpg_open,
	.close           = mpg_close,
	.read            = mpg_read,
	.seek            = mpg_seek,
	.read_comments   = mpg_read_comments,
	.duration        = mpg_duration,
	.bitrate         = mpg_bitrate,
	.bitrate_current = mpg_current_bitrate,
	.codec           = mpg_codec,
	.codec_profile   = mpg_codec_profile
};

const int ip_priority = 60;
const char * const ip_extensions[] = { "mp3", NULL };
const char * const ip_mime_types[] = { NULL };
const char * const ip_options[] = { NULL };
