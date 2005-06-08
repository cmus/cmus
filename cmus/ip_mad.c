/* 
 * Copyright 2004 Timo Hirvonen
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <ip_mad.h>
#include <nomad.h>
#include <xmalloc.h>
#include <read_wrapper.h>

#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <id3tag.h>

/* ------------------------------------------------------------------------- */

static ssize_t read_func(void *datasource, void *buffer, size_t count)
{
	struct input_plugin_data *ip_data = datasource;

	return read_wrapper(ip_data, buffer, count);
}

static off_t lseek_func(void *datasource, off_t offset, int whence)
{
	struct input_plugin_data *ip_data = datasource;

	return lseek(ip_data->fd, offset, whence);
}

static int close_func(void *datasource)
{
	struct input_plugin_data *ip_data = datasource;

	return close(ip_data->fd);
}

static struct nomad_callbacks callbacks = {
	.read = read_func,
	.lseek = lseek_func,
	.close = close_func
};

/* ------------------------------------------------------------------------- */

static int mad_open(struct input_plugin_data *ip_data)
{
	struct nomad *nomad;
	struct nomad_info info;
	int rc, fast;

	fast = 1;
	rc = nomad_open_callbacks(&nomad, ip_data, fast, &callbacks);
	switch (rc) {
	case -NOMAD_ERROR_ERRNO:
		return -1;
	case -NOMAD_ERROR_FILE_FORMAT:
		return -IP_ERROR_FILE_FORMAT;
	}
	ip_data->private = nomad;

	nomad_info(nomad, &info);

	/* always 16-bit signed little-endian */
	ip_data->sf = sf_rate(info.sample_rate) | sf_channels(info.channels) | sf_bits(16) | sf_signed(1);
	return 0;
}

static int mad_close(struct input_plugin_data *ip_data)
{
	struct nomad *nomad;

	nomad = ip_data->private;
	nomad_close(nomad);
	ip_data->fd = -1;
	ip_data->private = NULL;
	return 0;
}

static int mad_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct nomad *nomad;
	
	nomad = ip_data->private;
	return nomad_read(nomad, buffer, count);
}

static int mad_seek(struct input_plugin_data *ip_data, double offset)
{
	struct nomad *nomad;
	
	nomad = ip_data->private;
	return nomad_time_seek(nomad, offset);
}

static char *get_tag(struct id3_tag *tag, const char *name)
{
	struct id3_frame *frame;

	frame = id3_tag_findframe(tag, name, 0);
	if (frame) {
		const id3_ucs4_t *ucs4;

		ucs4 = id3_field_getstrings(&frame->fields[1], 0);
		if (ucs4)
			return (char *)id3_ucs4_utf8duplicate(ucs4);
	}
	return NULL;
}

static void add_tag(struct id3_tag *tag, const char *name, struct comment **cp, const char *key)
{
	char *val;

	val = get_tag(tag, name);
	if (val) {
		struct comment *c = *cp;

		c->key = xstrdup(key);
		c->val = val;
		*cp = c + 1;
	}
}

static void add_int_tag(struct id3_tag *tag, const char *name, struct comment **cp, const char *key)
{
	char *val;

	val = get_tag(tag, name);
	if (val) {
		struct comment *c = *cp;
		int i;

		/* val is "4" or "4/10" */
		for (i = 0; val[i]; i++) {
			if (val[i] == '/') {
				val[i] = 0;
				break;
			}
		}
		c->key = xstrdup(key);
		c->val = val;
		*cp = c + 1;
	}
}

static void add_genre(struct id3_tag *tag, struct comment **cp)
{
	struct comment *c = *cp;
	struct id3_frame *frame;
	const id3_ucs4_t *ucs4;
	char *genre;

	frame = id3_tag_findframe(tag, ID3_FRAME_GENRE, 0);
	if (frame == NULL)
		return;

	ucs4 = id3_field_getstrings(&frame->fields[1], 0);
	if (ucs4 == NULL)
		return;

	ucs4 = id3_genre_name(ucs4);
	if (ucs4 == NULL)
		return;

	genre = (char *)id3_ucs4_utf8duplicate(ucs4);
	if (genre == NULL)
		return;

	c->key = xstrdup("genre");
	c->val = genre;
	*cp = c + 1;
}

/* FIXME: get duration (milli seconds) from TLEN frame
 */
static int mad_read_comments(struct input_plugin_data *ip_data,
		struct comment **comments)
{
	struct id3_file *file;

	file = id3_file_open(ip_data->filename, ID3_FILE_MODE_READONLY);
	if (file) {
		struct id3_tag *tag;

		tag = id3_file_tag(file);
		if (tag) {
			struct comment *c;

			*comments = xnew0(struct comment, 8);
			c = *comments;
			add_tag(tag, ID3_FRAME_ARTIST, &c, "artist");
			add_tag(tag, ID3_FRAME_ALBUM, &c, "album");
			add_tag(tag, ID3_FRAME_TITLE, &c, "title");
			add_tag(tag, ID3_FRAME_YEAR, &c, "date");
			add_int_tag(tag, ID3_FRAME_TRACK, &c, "tracknumber");
			add_int_tag(tag, "TPOS", &c, "discnumber");
			add_genre(tag, &c);
		} else {
			*comments = xnew0(struct comment, 1);
		}
		id3_file_close (file);
	} else {
		*comments = xnew0(struct comment, 1);
	}
	return 0;
}

static int mad_duration(struct input_plugin_data *ip_data)
{
	struct nomad *nomad;

	nomad = ip_data->private;
	return nomad_time_total(nomad);
}

const struct input_plugin_ops mad_ip_ops = {
	.open = mad_open,
	.close = mad_close,
	.read = mad_read,
	.seek = mad_seek,
	.read_comments = mad_read_comments,
	.duration = mad_duration
};

const char * const mad_extensions[] = { "mp3", "mp2", NULL };
const char * const mad_mime_types[] = { "audio/mpeg", NULL };
