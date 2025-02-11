/*
 * Copyright 2016 Various Authors
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

#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "cue.h"
#include "xmalloc.h"
#include "file.h"

#define ASCII_LOWER_TO_UPPER(c) ((c) & ~0x20)

struct cue_track_proto {
	struct list_head node;

	char *file;	/* owned by cue_parser */
	uint32_t nr;
	int32_t pregap;
	int32_t postgap;
	int32_t index0;
	int32_t index1;

	struct cue_meta meta;
};

struct cue_parser {
	const char *src;
	size_t len;
	bool err;

	struct list_head files;
	struct list_head tracks;
	size_t num_tracks;

	struct cue_meta meta;
};

struct cue_switch {
	const char *cmd;
	void (*parser)(struct cue_parser *p);
};

static struct cue_track_proto *cue_last_proto(struct cue_parser *p)
{
	if (list_empty(&p->tracks))
		return NULL;
	return list_entry(p->tracks.prev, struct cue_track_proto, node);
}

static inline void cue_consume(struct cue_parser *p)
{
	p->len--;
	p->src++;
}

static void cue_set_err(struct cue_parser *p)
{
	p->err = true;
}

static bool cue_str_eq(const char *a, size_t a_len, const char *b, size_t b_len)
{
	if (a_len != b_len)
		return false;
	for (size_t i = 0; i < a_len; i++) {
		if (ASCII_LOWER_TO_UPPER(a[i]) != ASCII_LOWER_TO_UPPER(b[i]))
			return false;
	}
	return true;
}

static void cue_skip_spaces(struct cue_parser *p)
{
	while (p->len > 0 && (*p->src == ' ' || *p->src == '\t'))
		cue_consume(p);
}

static size_t cue_extract_token(struct cue_parser *p, const char **start)
{
	cue_skip_spaces(p);

	bool quoted = p->len > 0 && *p->src == '"';
	if (quoted)
		cue_consume(p);

	*start = p->src;

	while (p->len > 0) {
		char c = *p->src;
		if (c == '\n' || c == '\r')
			break;
		if (quoted) {
			if (c == '"')
				break;
		} else {
			if (c == ' ' || c == '\t')
				break;
		}
		cue_consume(p);
	}

	if (quoted) {
		size_t len = p->src - *start;
		if (p->len > 0 && *p->src == '"')
			cue_consume(p);
		return len;
	}

	return p->src - *start;
}

static void cue_skip_line(struct cue_parser *p)
{
	while (p->len > 0 && *p->src != '\n' && *p->src != '\r')
		cue_consume(p);

	if (p->len > 0) {
		char c = *p->src;
		cue_consume(p);
		if (p->len > 0 && c == '\r' && *p->src == '\n')
			cue_consume(p);
	}
}

static char *cue_strdup(const char *start, size_t len)
{
	char *s = xnew(char, len + 1);
	s[len] = 0;
	memcpy(s, start, len);
	return s;
}

static uint32_t cue_parse_int(struct cue_parser *p, const char *start, size_t len)
{
	uint32_t val = 0;
	for (size_t i = 0; i < len; i++) {
		if (!isdigit(start[i])) {
			cue_set_err(p);
			return 0;
		}
		val = val * 10 + start[i] - '0';
	}
	return val;
}

static void cue_parse_str(struct cue_parser *p, char **dst)
{
	const char *start;
	size_t len = cue_extract_token(p, &start);
	if (!*dst)
		*dst = cue_strdup(start, len);
}

#define CUE_PARSE_STR(field) \
	static void cue_parse_##field(struct cue_parser *p) \
	{ \
		struct cue_track_proto *t = cue_last_proto(p); \
		if (t) \
			cue_parse_str(p, &t->meta.field); \
		else \
			cue_parse_str(p, &p->meta.field); \
	}

CUE_PARSE_STR(performer)
CUE_PARSE_STR(songwriter)
CUE_PARSE_STR(title)
CUE_PARSE_STR(genre)
CUE_PARSE_STR(date)
CUE_PARSE_STR(comment)
CUE_PARSE_STR(compilation);
CUE_PARSE_STR(discnumber);

static void cue_parse_file(struct cue_parser *p)
{
	struct cue_track_file *f = xnew(struct cue_track_file, 1);

	f->file = NULL;
	cue_parse_str(p, &f->file);

	list_add_tail(&f->node, &p->files);
}

static char *cue_get_last_file(struct cue_parser *p)
{
	if (list_empty(&p->files))
		return NULL;

	struct list_head *tail = list_prev(&p->files);
	return list_entry(tail, struct cue_track_file, node)->file;
}

static void cue_parse_track(struct cue_parser *p)
{
	char *curr_file = cue_get_last_file(p);

	if (!curr_file) {
		cue_set_err(p);
		return;
	}

	const char *nr;
	size_t len = cue_extract_token(p, &nr);

	uint32_t d = cue_parse_int(p, nr, len);
	if (p->err)
		return;

	struct cue_track_proto *t = xnew(struct cue_track_proto, 1);
	*t = (struct cue_track_proto) {
		.nr = d,
		.pregap = -1,
		.postgap = -1,
		.index0 = -1,
		.index1 = -1,
		.file = curr_file,
	};

	list_add_tail(&t->node, &p->tracks);
	p->num_tracks++;
}

static uint32_t cue_parse_time(struct cue_parser *p, const char *start, size_t len)
{
	uint32_t vals[] = { 0, 0, 0 };
	uint32_t *val = vals;
	for (size_t i = 0; i < len; i++) {
		if (start[i] == ':') {
			if (val != &vals[2]) {
				val++;
				continue;
			}
			break;
		}
		if (!isdigit(start[i])) {
			cue_set_err(p);
			return 0;
		}
		*val = *val * 10 + start[i] - '0';
	}
	return (vals[0] * 60 + vals[1]) * 75 + vals[2];
}

static void cue_parse_index(struct cue_parser *p)
{
	const char *nr;
	size_t nr_len = cue_extract_token(p, &nr);

	uint32_t d = cue_parse_int(p, nr, nr_len);
	if (p->err || d > 1)
		return;

	const char *offset_str;
	size_t offset_len = cue_extract_token(p, &offset_str);

	uint32_t offset = cue_parse_time(p, offset_str, offset_len);
	if (p->err)
		return;

	struct cue_track_proto *last = cue_last_proto(p);
	if (!last)
		return;

	if (d == 0)
		last->index0 = offset;
	else
		last->index1 = offset;
}

static void cue_parse_cmd(struct cue_parser *p, struct cue_switch *s)
{
	const char *start;
	size_t len = cue_extract_token(p, &start);

	while (s->cmd) {
		if (cue_str_eq(start, len, s->cmd, strlen(s->cmd))) {
			s->parser(p);
			return;
		}
		s++;
	}
}

static void cue_parse_rem(struct cue_parser *p)
{
	struct cue_switch cmds[] = {
		{ "DATE",        cue_parse_date        },
		{ "GENRE",       cue_parse_genre       },
		{ "COMMENT",     cue_parse_comment     },
		{ "COMPILATION", cue_parse_compilation },
		{ "DISCNUMBER",  cue_parse_discnumber  },
		{ 0 },
	};

	cue_parse_cmd(p, cmds);
}

static void cue_parse_gap(struct cue_parser *p, bool post)
{
	const char *gap_str;
	size_t gap_len = cue_extract_token(p, &gap_str);

	uint32_t gap = cue_parse_time(p, gap_str, gap_len);
	if (p->err)
		return;

	struct cue_track_proto *last = cue_last_proto(p);
	if (!last)
		return;

	if (post)
		last->postgap = gap;
	else
		last->pregap = gap;
}

static void cue_parse_pregap(struct cue_parser *p)
{
	cue_parse_gap(p, false);
}

static void cue_parse_postgap(struct cue_parser *p)
{
	cue_parse_gap(p, true);
}

static void cue_parse_line(struct cue_parser *p)
{
	struct cue_switch cmds[] = {
		{ "FILE",       cue_parse_file       },
		{ "PERFORMER",  cue_parse_performer  },
		{ "SONGWRITER", cue_parse_songwriter },
		{ "TITLE",      cue_parse_title      },
		{ "TRACK",      cue_parse_track      },
		{ "INDEX",      cue_parse_index      },
		{ "REM",        cue_parse_rem        },
		{ "PREGAP",     cue_parse_pregap     },
		{ "POSTGAP",    cue_parse_postgap    },
		{ 0 },
	};

	cue_parse_cmd(p, cmds);
	cue_skip_line(p);
}

static void cue_post_process(struct cue_parser *p)
{
	if (list_empty(&p->files) || p->num_tracks == 0)
		goto err;

	struct cue_track_proto *t;
	struct cue_track_proto *prev = NULL;
	int32_t last = -1;
	const char *last_file = NULL;

	/* TODO: fix timing calculations */
	list_for_each_entry(t, &p->tracks, node) {
		if (prev && prev->nr >= t->nr)
			goto err;

		if (t->index0 == -1 && t->index1 == -1)
			goto err;

		if (t->index0 == -1 || t->index1 == -1) {
			int32_t pregap = t->pregap != -1 ? t->pregap : 0;
			if (t->index1 != -1)
				t->index0 = t->index1 - pregap;
			else
				t->index1 = t->index0 + pregap;
		}

		if (last != -1 && (t->file == last_file && t->index0 < last))
			goto err;

		int32_t postgap = t->postgap != -1 ? t->postgap : 0;
		last = t->index1 + postgap;
		last_file = t->file;
		prev = t;
	}

	return;
err:
	cue_set_err(p);
}

static void cue_meta_move(struct cue_meta *l, struct cue_meta *r)
{
	*l = *r;
	*r = (struct cue_meta) { 0 };
}

static struct cue_sheet *cue_parser_to_sheet(struct cue_parser *p)
{
	struct cue_sheet *s = xnew(struct cue_sheet, 1);

	list_add(&s->files, &p->files);
	list_del_init(&p->files);

	s->tracks = xnew(struct cue_track, p->num_tracks);
	s->num_tracks = p->num_tracks;

	cue_meta_move(&s->meta, &p->meta);

	size_t i = 0;
	struct cue_track_proto *tp = NULL;
	struct cue_track_proto *prev_tp = NULL;
	list_for_each_entry(tp, &p->tracks, node) {
		struct cue_track *t = &s->tracks[i];
		t->file = tp->file;
		t->offset = tp->index1 / 75.0;
		t->length = -1;
		t->number = tp->nr;

		if (i > 0 && t->file == s->tracks[i - 1].file) {
			int32_t postgap = prev_tp->postgap != -1 ? prev_tp->postgap : 0;
			s->tracks[i - 1].length = (tp->index0 - prev_tp->index1 - postgap) / 75.0;
		}

		cue_meta_move(&t->meta, &tp->meta);

		prev_tp = tp;
		i++;
	}

	return s;
}

static void cue_meta_free(struct cue_meta *m)
{
	free(m->performer);
	free(m->songwriter);
	free(m->title);
	free(m->genre);
	free(m->date);
	free(m->comment);
	free(m->compilation);
	free(m->discnumber);
}

static void cue_free_files(struct list_head *files)
{
	struct cue_track_file *tf, *next;
	list_for_each_entry_safe(tf, next, files, node) {
		free(tf->file);
		free(tf);
	}
}

static void cue_parser_free(struct cue_parser *p)
{
	struct cue_track_proto *t, *next;
	list_for_each_entry_safe(t, next, &p->tracks, node) {
		cue_meta_free(&t->meta);
		free(t);
	}

	cue_free_files(&p->files);

	cue_meta_free(&p->meta);
}

struct cue_sheet *cue_parse(const char *src, size_t len)
{
	struct cue_sheet *res = NULL;

	struct cue_parser p = {
		.src = src,
		.len = len,
	};
	list_init(&p.tracks);
	list_init(&p.files);

	while (p.len > 0 && !p.err)
		cue_parse_line(&p);

	if (p.err)
		goto out;

	cue_post_process(&p);

	if (p.err)
		goto out;

	res = cue_parser_to_sheet(&p);

out:
	cue_parser_free(&p);
	return res;
}

struct cue_sheet *cue_from_file(const char *file)
{
	ssize_t size;
	char *buf = mmap_file(file, &size);
	if (size == -1)
		return NULL;
	struct cue_sheet *rv;

	// Check for UTF-8 BOM, and skip ahead if found
	if (size >= 3 && memcmp(buf, "\xEF\xBB\xBF", 3) == 0) {
		rv = cue_parse(buf + 3, size - 3);
	} else {
		rv = cue_parse(buf, size);
	}

	munmap(buf, size);
	return rv;
}

void cue_free(struct cue_sheet *s)
{
	size_t i;
	for (i = 0; i < s->num_tracks; i++)
		cue_meta_free(&s->tracks[i].meta);
	free(s->tracks);

	cue_free_files(&s->files);

	cue_meta_free(&s->meta);
	free(s);
}

struct cue_track *cue_get_track(struct cue_sheet *s, size_t n)
{
	size_t i;
	for (i = 0; i < s->num_tracks; i++) {
		struct cue_track *t = &s->tracks[i];
		if (t->number == n)
			return t;
	}
	return NULL;
}
