/*
 * Copyright 2008-2013 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cache.h"
#include "misc.h"
#include "file.h"
#include "input.h"
#include "track_info.h"
#include "utils.h"
#include "xmalloc.h"
#include "xstrjoin.h"
#include "gbuf.h"
#include "options.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

#define CACHE_VERSION   0x0d

#define CACHE_64_BIT	0x01
#define CACHE_BE	0x02

#define CACHE_RESERVED_PATTERN  	0xff

#define CACHE_ENTRY_USED_SIZE		28
#define CACHE_ENTRY_RESERVED_SIZE	52
#define CACHE_ENTRY_TOTAL_SIZE	(CACHE_ENTRY_RESERVED_SIZE + CACHE_ENTRY_USED_SIZE)

// Cmus Track Cache version X + 4 bytes flags
static char cache_header[8] = "CTC\0\0\0\0\0";

// host byte order
// mtime is either 32 or 64 bits
struct cache_entry {
	// size of this struct including size itself
	uint32_t size;

	int32_t play_count;
	int64_t mtime;
	int32_t duration;
	int32_t bitrate;
	int32_t bpm;

	// when introducing new fields decrease the reserved space accordingly
	uint8_t _reserved[CACHE_ENTRY_RESERVED_SIZE];

	// filename, codec, codec_profile and N * (key, val)
	char strings[];
};

// make sure our mmap/sizeof-based code works
STATIC_ASSERT(CACHE_ENTRY_TOTAL_SIZE == sizeof(struct cache_entry));
STATIC_ASSERT(CACHE_ENTRY_TOTAL_SIZE == offsetof(struct cache_entry, strings));


#define ALIGN(size) (((size) + sizeof(long) - 1) & ~(sizeof(long) - 1))
#define HASH_SIZE 1023

static struct track_info *hash_table[HASH_SIZE];
static char *cache_filename;
static int total;

struct fifo_mutex cache_mutex = FIFO_MUTEX_INITIALIZER;


static void add_ti(struct track_info *ti, unsigned int hash)
{
	unsigned int pos = hash % HASH_SIZE;
	struct track_info *next = hash_table[pos];

	ti->next = next;
	hash_table[pos] = ti;
	total++;
}

static int valid_cache_entry(const struct cache_entry *e, unsigned int avail)
{
	unsigned int min_size = sizeof(*e);
	unsigned int str_size;
	int i, count;

	if (avail < min_size)
		return 0;

	if (e->size < min_size || e->size > avail)
		return 0;

	str_size = e->size - min_size;
	count = 0;
	for (i = 0; i < str_size; i++) {
		if (!e->strings[i])
			count++;
	}
	if (count % 2 == 0)
		return 0;
	if (e->strings[str_size - 1])
		return 0;
	return 1;
}

static struct track_info *cache_entry_to_ti(struct cache_entry *e)
{
	const char *strings = e->strings;
	struct track_info *ti = track_info_new(strings);
	struct keyval *kv;
	int str_size = e->size - sizeof(*e);
	int pos, i, count;

	ti->duration = e->duration;
	ti->bitrate = e->bitrate;
	ti->mtime = e->mtime;
	ti->play_count = e->play_count;
	ti->bpm = e->bpm;

	// count strings (filename + codec + codec_profile + key/val pairs)
	count = 0;
	for (i = 0; i < str_size; i++) {
		if (!strings[i])
			count++;
	}
	count = (count - 3) / 2;

	// NOTE: filename already copied by track_info_new()
	pos = strlen(strings) + 1;
	ti->codec = strings[pos] ? xstrdup(strings + pos) : NULL;
	pos += strlen(strings + pos) + 1;
	ti->codec_profile = strings[pos] ? xstrdup(strings + pos) : NULL;
	pos += strlen(strings + pos) + 1;
	kv = xnew(struct keyval, count + 1);
	for (i = 0; i < count; i++) {
		int size;

		size = strlen(strings + pos) + 1;
		kv[i].key = xstrdup(strings + pos);
		pos += size;

		size = strlen(strings + pos) + 1;
		kv[i].val = xstrdup(strings + pos);
		pos += size;
	}
	kv[i].key = NULL;
	kv[i].val = NULL;
	track_info_set_comments(ti, kv);
	return ti;
}

struct track_info *lookup_cache_entry(const char *filename, unsigned int hash)
{
	struct track_info *ti = hash_table[hash % HASH_SIZE];

	while (ti) {
		if (!strcmp(filename, ti->filename))
			return ti;
		ti = ti->next;
	}
	return NULL;
}

static void do_cache_remove_ti(struct track_info *ti, unsigned int hash)
{
	unsigned int pos = hash % HASH_SIZE;
	struct track_info *t = hash_table[pos];
	struct track_info *next, *prev = NULL;

	while (t) {
		next = t->next;
		if (t == ti) {
			if (prev) {
				prev->next = next;
			} else {
				hash_table[pos] = next;
			}
			total--;
			track_info_unref(ti);
			return;
		}
		prev = t;
		t = next;
	}
}

void cache_remove_ti(struct track_info *ti)
{
	do_cache_remove_ti(ti, hash_str(ti->filename));
}

static int read_cache(void)
{
	unsigned int size, offset = 0;
	struct stat st = {};
	char *buf;
	int fd;

	fd = open(cache_filename, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	fstat(fd, &st);
	if (st.st_size < sizeof(cache_header))
		goto close;
	size = st.st_size;

	buf = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (buf == MAP_FAILED) {
		close(fd);
		return -1;
	}

	if (memcmp(buf, cache_header, sizeof(cache_header)))
		goto corrupt;

	offset = sizeof(cache_header);
	while (offset < size) {
		struct cache_entry *e = (void *)(buf + offset);
		struct track_info *ti;

		if (!valid_cache_entry(e, size - offset))
			goto corrupt;

		ti = cache_entry_to_ti(e);
		add_ti(ti, hash_str(ti->filename));
		offset += ALIGN(e->size);
	}
	munmap(buf, size);
	close(fd);
	return 0;
corrupt:
	munmap(buf, size);
close:
	close(fd);
	// corrupt
	return -2;
}

int cache_init(void)
{
	unsigned int flags = 0;

#ifdef WORDS_BIGENDIAN
	flags |= CACHE_BE;
#endif
	if (sizeof(long) == 8)
		flags |= CACHE_64_BIT;

	cache_header[7] = flags & 0xff; flags >>= 8;
	cache_header[6] = flags & 0xff; flags >>= 8;
	cache_header[5] = flags & 0xff; flags >>= 8;
	cache_header[4] = flags & 0xff;

	/* assumed version */
	cache_header[3] = CACHE_VERSION;

	cache_filename = xstrjoin(cmus_config_dir, "/cache");
	return read_cache();
}

static int ti_filename_cmp(const void *a, const void *b)
{
	const struct track_info *ai = *(const struct track_info **)a;
	const struct track_info *bi = *(const struct track_info **)b;

	return strcmp(ai->filename, bi->filename);
}

static struct track_info **get_track_infos(bool reference)
{
	struct track_info **tis;
	int i, c;

	tis = xnew(struct track_info *, total);
	c = 0;
	for (i = 0; i < HASH_SIZE; i++) {
		struct track_info *ti = hash_table[i];

		while (ti) {
			if (reference)
				track_info_ref(ti);
			tis[c++] = ti;
			ti = ti->next;
		}
	}
	qsort(tis, total, sizeof(struct track_info *), ti_filename_cmp);
	return tis;
}

static void flush_buffer(int fd, struct gbuf *buf)
{
	if (buf->len) {
		write_all(fd, buf->buffer, buf->len);
		gbuf_clear(buf);
	}
}

static void write_ti(int fd, struct gbuf *buf, struct track_info *ti, unsigned int *offsetp)
{
	const struct keyval *kv = ti->comments;
	unsigned int offset = *offsetp;
	unsigned int pad;
	struct cache_entry e;
	int *len, alloc = 64, count, i;

	memset(e._reserved, CACHE_RESERVED_PATTERN, sizeof(e._reserved));

	count = 0;
	len = xnew(int, alloc);
	e.size = sizeof(e);
	e.duration = ti->duration;
	e.bitrate = ti->bitrate;
	e.mtime = ti->mtime;
	e.play_count = ti->play_count;
	e.bpm = ti->bpm;
	len[count] = strlen(ti->filename) + 1;
	e.size += len[count++];
	len[count] = (ti->codec ? strlen(ti->codec) : 0) + 1;
	e.size += len[count++];
	len[count] = (ti->codec_profile ? strlen(ti->codec_profile) : 0) + 1;
	e.size += len[count++];
	for (i = 0; kv[i].key; i++) {
		if (count + 2 > alloc) {
			alloc *= 2;
			len = xrenew(int, len, alloc);
		}
		len[count] = strlen(kv[i].key) + 1;
		e.size += len[count++];
		len[count] = strlen(kv[i].val) + 1;
		e.size += len[count++];
	}

	pad = ALIGN(offset) - offset;
	if (gbuf_avail(buf) < pad + e.size)
		flush_buffer(fd, buf);

	count = 0;
	if (pad)
		gbuf_set(buf, 0, pad);
	gbuf_add_bytes(buf, &e, sizeof(e));
	gbuf_add_bytes(buf, ti->filename, len[count++]);
	gbuf_add_bytes(buf, ti->codec ? ti->codec : "", len[count++]);
	gbuf_add_bytes(buf, ti->codec_profile ? ti->codec_profile : "", len[count++]);
	for (i = 0; kv[i].key; i++) {
		gbuf_add_bytes(buf, kv[i].key, len[count++]);
		gbuf_add_bytes(buf, kv[i].val, len[count++]);
	}

	free(len);
	*offsetp = offset + pad + e.size;
}

int cache_close(void)
{
	GBUF(buf);
	struct track_info **tis;
	unsigned int offset;
	int i, fd, rc;
	char *tmp;

	tmp = xstrjoin(cmus_config_dir, "/cache.tmp");
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		free(tmp);
		return -1;
	}

	tis = get_track_infos(false);

	gbuf_grow(&buf, 64 * 1024 - 1);
	gbuf_add_bytes(&buf, cache_header, sizeof(cache_header));
	offset = sizeof(cache_header);
	for (i = 0; i < total; i++)
		write_ti(fd, &buf, tis[i], &offset);
	flush_buffer(fd, &buf);
	gbuf_free(&buf);
	free(tis);

	close(fd);
	rc = rename(tmp, cache_filename);
	free(tmp);
	return rc;
}

static struct track_info *ip_get_ti(const char *filename)
{
	struct track_info *ti = NULL;
	struct input_plugin *ip;
	struct keyval *comments;
	int rc;

	ip = ip_new(filename);
	rc = ip_open(ip);
	if (rc) {
		ip_delete(ip);
		return NULL;
	}

	rc = ip_read_comments(ip, &comments);
	if (!rc) {
		ti = track_info_new(filename);
		track_info_set_comments(ti, comments);
		ti->duration = ip_duration(ip);
		ti->bitrate = ip_bitrate(ip);
		ti->codec = ip_codec(ip);
		ti->codec_profile = ip_codec_profile(ip);
		ti->mtime = ip_is_remote(ip) ? -1 : file_get_mtime(filename);
	}
	ip_delete(ip);
	return ti;
}

struct track_info *cache_get_ti(const char *filename, int force)
{
	unsigned int hash = hash_str(filename);
	struct track_info *ti;
	int reload = 0;

	ti = lookup_cache_entry(filename, hash);
	if (ti) {
		if ((!skip_track_info && ti->duration == 0 && !is_http_url(filename)) || force){
			do_cache_remove_ti(ti, hash);
			ti = NULL;
			reload = 1;
		}
	}
	if (!ti) {
		if (skip_track_info && !reload && !force) {
			struct growing_keyvals c = {NULL, 0, 0};

			ti = track_info_new(filename);

			keyvals_terminate(&c);
			track_info_set_comments(ti, c.keyvals);

			ti->duration = 0;
		} else {
		       	ti = ip_get_ti(filename);
		}
		if (!ti)
			return NULL;
		add_ti(ti, hash);
	}
	track_info_ref(ti);
	return ti;
}

struct track_info **cache_refresh(int *count, int force)
{
	struct track_info **tis = get_track_infos(true);
	int i, n = total;

	for (i = 0; i < n; i++) {
		unsigned int hash;
		struct track_info *ti = tis[i];
		struct stat st;
		int rc = 0;

		cache_yield();

		/*
		 * If no-one else has reference to tis[i] then it is set to NULL
		 * otherwise:
		 *
		 * unchanged: tis[i] = NULL
		 * deleted:   tis[i]->next = NULL
		 * changed:   tis[i]->next = new
		 */

		if (!is_url(ti->filename)) {
			rc = stat(ti->filename, &st);
			if (!rc && !force && ti->mtime == st.st_mtime) {
				// unchanged
				track_info_unref(ti);
				tis[i] = NULL;
				continue;
			}
		}

		hash = hash_str(ti->filename);
		do_cache_remove_ti(ti, hash);

		if (!rc) {
			// changed
			struct track_info *new_ti;

			// clear cache-only entries
			if (force && track_info_unique_ref(ti)) {
				track_info_unref(ti);
				tis[i] = NULL;
				continue;
			}

			new_ti = ip_get_ti(ti->filename);
			if (new_ti) {
				add_ti(new_ti, hash);

				if (track_info_unique_ref(ti)) {
					track_info_unref(ti);
					tis[i] = NULL;
				} else {
					track_info_ref(new_ti);
					ti->next = new_ti;
				}
				continue;
			}
			// treat as deleted
		}

		// deleted
		if (track_info_unique_ref(ti)) {
			track_info_unref(ti);
			tis[i] = NULL;
		} else {
			ti->next = NULL;
		}
	}
	*count = n;
	return tis;
}
