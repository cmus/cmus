#include "cache.h"
#include "misc.h"
#include "file.h"
#include "input.h"
#include "track_info.h"
#include "utils.h"
#include "xmalloc.h"
#include "xstrjoin.h"
#include "gbuf.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define CACHE_64_BIT	0x01
#define CACHE_BE	0x02

// Cmus Track Cache version X + 4 bytes flags
static char cache_header[8] = "CTC\0\0\0\0\0";

// host byte order
// mtime is either 32 or 64 bits
struct cache_entry {
	// size of this struct including size itself
	// NOTE: size does not include padding bytes
	unsigned int size;
	int duration;
	time_t mtime;

	// filename and N * (key, val)
	char strings[];
};

#define ALIGN(size) (((size) + sizeof(long) - 1) & ~(sizeof(long) - 1))
#define HASH_SIZE 1023

static struct track_info *hash_table[HASH_SIZE];
static char *cache_filename;
static int total;
static int removed;
static int new;

pthread_mutex_t cache_mutex = CMUS_MUTEX_INITIALIZER;

static unsigned int filename_hash(const char *filename)
{
	unsigned int hash = 0;
	int i;

	for (i = 0; filename[i]; i++)
		hash = (hash << 5) - hash + filename[i];
	return hash;
}

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
	ti->mtime = e->mtime;

	// count strings (filename + key/val pairs)
	count = 0;
	for (i = 0; i < str_size; i++) {
		if (!strings[i])
			count++;
	}
	count = (count - 1) / 2;

	// NOTE: filename already copied by track_info_new()
	pos = strlen(strings) + 1;
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

static struct track_info *lookup_cache_entry(const char *filename, unsigned int hash)
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
			removed++;
			track_info_unref(ti);
			return;
		}
		prev = t;
		t = next;
	}
}

void cache_remove_ti(struct track_info *ti)
{
	do_cache_remove_ti(ti, filename_hash(ti->filename));
}

static int read_cache(void)
{
	unsigned int size, offset = 0;
	struct stat st;
	char *buf;
	int fd;

	fd = open(cache_filename, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	fstat(fd,  &st);
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
		struct cache_entry *e = (struct cache_entry *)(buf + offset);
		struct track_info *ti;

		if (!valid_cache_entry(e, size - offset))
			goto corrupt;

		ti = cache_entry_to_ti(e);
		add_ti(ti, filename_hash(ti->filename));
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
	cache_header[3] = 0x02;

	cache_filename = xstrjoin(cmus_config_dir, "/cache");
	return read_cache();
}

static int ti_filename_cmp(const void *a, const void *b)
{
	const struct track_info *ai = *(const struct track_info **)a;
	const struct track_info *bi = *(const struct track_info **)b;

	return strcmp(ai->filename, bi->filename);
}

static struct track_info **get_track_infos(void)
{
	struct track_info **tis;
	int i, c;

	tis = xnew(struct track_info *, total);
	c = 0;
	for (i = 0; i < HASH_SIZE; i++) {
		struct track_info *ti = hash_table[i];

		while (ti) {
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

	count = 0;
	len = xnew(int, alloc);
	e.size = sizeof(e);
	e.duration = ti->duration;
	e.mtime = ti->mtime;
	len[count] = strlen(ti->filename) + 1;
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
	int i, fd;
	char *tmp;

	if (!new && !removed)
		return 0;

	tmp = xstrjoin(cmus_config_dir, "/cache.tmp");
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		return -1;

	tis = get_track_infos();

	gbuf_grow(&buf, 64 * 1024 - 1);
	gbuf_add_bytes(&buf, cache_header, sizeof(cache_header));
	offset = sizeof(cache_header);
	for (i = 0; i < total; i++)
		write_ti(fd, &buf, tis[i], &offset);
	flush_buffer(fd, &buf);
	gbuf_free(&buf);

	close(fd);
	if (rename(tmp, cache_filename))
		return -1;
	return 0;
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
		ti->mtime = ip_is_remote(ip) ? -1 : file_get_mtime(filename);
	}
	ip_delete(ip);
	return ti;
}

struct track_info *cache_get_ti(const char *filename)
{
	unsigned int hash = filename_hash(filename);
	struct track_info *ti;

	ti = lookup_cache_entry(filename, hash);
	if (!ti) {
		ti = ip_get_ti(filename);
		if (!ti)
			return NULL;
		add_ti(ti, hash);
		new++;
	}
	track_info_ref(ti);
	return ti;
}

struct track_info **cache_refresh(int *count)
{
	struct track_info **tis = get_track_infos();
	int i;

	for (i = 0; i < total; i++) {
		unsigned int hash;
		struct track_info *ti = tis[i];
		struct stat st;
		int rc = 0;

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
			if (!rc && ti->mtime == st.st_mtime) {
				// unchanged
				tis[i] = NULL;
				continue;
			}
		}

		hash = filename_hash(ti->filename);
		track_info_ref(ti);
		do_cache_remove_ti(ti, hash);

		if (!rc) {
			// changed
			struct track_info *new_ti = ip_get_ti(ti->filename);

			if (new_ti) {
				add_ti(new_ti, hash);
				new++;

				if (ti->ref == 1) {
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
		if (ti->ref == 1) {
			track_info_unref(ti);
			tis[i] = NULL;
		} else {
			ti->next = NULL;
		}
	}
	*count = total;
	return tis;
}
