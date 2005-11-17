/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <db.h>
#include <xmalloc.h>
#include <xstrjoin.h>
#include <file.h>

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>

struct db_entry {
	uint32_t data_pos;
	uint32_t data_size;
	uint32_t key_size;
	void *key;
};

struct db {
	/* always sorted by key */
	struct db_entry *entries;
	unsigned int nr_entries;
	unsigned int nr_allocated;

	/* insert queue, not sorted */
	struct db_entry *iq_entries;
	void **iq_datas;
	unsigned int iq_size;
	unsigned int iq_fill;

	char *idx_fn;
	char *dat_fn;

	int dat_fd;

	unsigned int index_dirty : 1;
};

static int db_entry_cmp(const void *a, const void *b)
{
	const struct db_entry *ae = a;
	const struct db_entry *be = b;

	return strcmp(ae->key, be->key);
}

static void array_remove(void *array, size_t nmemb, size_t size, int idx)
{
	char *a = array;
	char *s, *d;
	size_t c;

	d = a + idx * size;
	s = d + size;
	c = size * (nmemb - idx - 1);
	if (c > 0)
		memmove(d, s, c);
}

/* index {{{ */

static int index_load(struct db *db)
{
	int fd, size, pos, rc, i;
	char *buf;

	fd = open(db->idx_fn, O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	size = lseek(fd, 0, SEEK_END);
	if (size == -1)
		return -1;
	if (lseek(fd, 0, SEEK_SET) == -1)
		return -1;
	if (size < 4)
		return -2;
	buf = xnew(char, size);
	rc = read_all(fd, buf, size);
	if (rc == -1) {
		free(buf);
		return -1;
	}
	if (rc != size) {
		free(buf);
		return -2;
	}
	/* format: nr_rows, nr_rows * (pos, size, key_size, key) */
	pos = 0;
	db->nr_entries = ntohl(*(uint32_t *)(buf + pos)); pos += 4;
	db->entries = xnew(struct db_entry, db->nr_entries);
	db->nr_allocated = db->nr_entries;
	for (i = 0; i < db->nr_entries; i++) {
		struct db_entry *e = &db->entries[i];

		if (size - pos < 3 * 4)
			goto corrupt;
		e->data_pos = ntohl(*(uint32_t *)(buf + pos)); pos += 4;
		e->data_size = ntohl(*(uint32_t *)(buf + pos)); pos += 4;
		e->key_size = ntohl(*(uint32_t *)(buf + pos)); pos += 4;
		if (size - pos < e->key_size)
			goto corrupt;
		e->key = xmalloc(e->key_size);
		memcpy(e->key, buf + pos, e->key_size);
		pos += e->key_size;
	}
	free(buf);
	return 0;
corrupt:
	free(buf);
	free(db->entries);
	db->entries = NULL;
	db->nr_entries = 0;
	db->nr_allocated = 0;
	return -2;
}

static int index_save(struct db *db)
{
	uint32_t data;
	int fd, i;

	fd = creat(db->idx_fn, 0666);
	if (fd == -1)
		return -1;
	data = htonl(db->nr_entries);
	if (write_all(fd, &data, 4) != 4)
		goto err;
	for (i = 0; i < db->nr_entries; i++) {
		data = htonl(db->entries[i].data_pos);
		if (write_all(fd, &data, 4) != 4)
			goto err;
		data = htonl(db->entries[i].data_size);
		if (write_all(fd, &data, 4) != 4)
			goto err;
		data = htonl(db->entries[i].key_size);
		if (write_all(fd, &data, 4) != 4)
			goto err;
		if (write_all(fd, db->entries[i].key, db->entries[i].key_size) != db->entries[i].key_size)
			goto err;
	}
	close(fd);
	db->index_dirty = 0;
	return 0;
err:
	close(fd);
	return -1;
}

static void index_free(struct db *db)
{
	int i;

	for (i = 0; i < db->nr_entries; i++)
		free(db->entries[i].key);
	free(db->entries);
	db->entries = NULL;
	db->nr_entries = 0;
	db->nr_allocated = 0;
}

static struct db_entry *index_search(struct db *db, const void *key)
{
	struct db_entry k;

	k.key = (void *)key;
	return bsearch(&k, db->entries, db->nr_entries, sizeof(struct db_entry), db_entry_cmp);
}

static int index_remove(struct db *db, const void *key, unsigned int key_size)
{
	struct db_entry *e;

	e = index_search(db, key);
	if (e == NULL)
		return 0;

	free(e->key);

	array_remove(db->entries, db->nr_entries, sizeof(struct db_entry), e - db->entries);
	db->nr_entries--;
	db->index_dirty = 1;
	return 1;
}

/* }}} */

/* iq {{{ */

static int iq_flush(struct db *db)
{
	int pos, rc, i;

	/* write data */
	pos = lseek(db->dat_fd, 0, SEEK_END);
	if (pos == -1)
		return -1;
	for (i = 0; i < db->iq_fill; i++) {
		rc = write_all(db->dat_fd, db->iq_datas[i], db->iq_entries[i].data_size);
		if (rc == -1)
			return -1;
	}

	/* free datas */
	for (i = 0; i < db->iq_fill; i++)
		free(db->iq_datas[i]);

	/* update index */
	if (db->iq_fill + db->nr_entries > db->nr_allocated) {
		db->nr_allocated = db->iq_fill + db->nr_entries;
		db->entries = xrenew(struct db_entry, db->entries, db->nr_allocated);
	}
	for (i = 0; i < db->iq_fill; i++) {
		struct db_entry *s = &db->iq_entries[i];
		struct db_entry *d = &db->entries[db->nr_entries];

		d->data_pos = pos;
		d->data_size = s->data_size;
		d->key_size = s->key_size;
		d->key = s->key;
		db->nr_entries++;
		pos += d->data_size;
	}
	db->iq_fill = 0;
	qsort(db->entries, db->nr_entries, sizeof(struct db_entry), db_entry_cmp);
	db->index_dirty = 1;
	return 0;
}

static int iq_search(struct db *db, const void *key)
{
	int i;

	for (i = 0; i < db->iq_fill; i++) {
		if (strcmp(db->iq_entries[i].key, key) == 0)
			return i;
	}
	return -1;
}

static int iq_remove(struct db *db, const void *key, unsigned int key_size)
{
	int i;

	i = iq_search(db, key);
	if (i == -1)
		return 0;

	free(db->iq_entries[i].key);
	free(db->iq_datas[i]);

	array_remove(db->iq_entries, db->iq_fill, sizeof(struct db_entry), i);
	array_remove(db->iq_datas, db->iq_fill, sizeof(void *), i);
	db->iq_fill--;
	return 1;
}

/* }}} */

struct db *db_new(const char *filename_base)
{
	struct db *db;

	db = xnew(struct db, 1);
	db->index_dirty = 0;
	db->idx_fn = xstrjoin(filename_base, ".idx");
	db->dat_fn = xstrjoin(filename_base, ".dat");
	db->entries = NULL;
	db->nr_entries = 0;
	db->nr_allocated = 0;
	db->dat_fd = -1;

	db->iq_size = 128;
	db->iq_fill = 0;
	db->iq_entries = xnew(struct db_entry, db->iq_size);
	db->iq_datas = xnew(void *, db->iq_size);
	return db;
}

int db_load(struct db *db)
{
	int rc;

	rc = index_load(db);
	if (rc)
		return rc;

	db->dat_fd = open(db->dat_fn, O_RDWR | O_CREAT, 0666);
	if (db->dat_fd == -1) {
		index_free(db);
		return -1;
	}
	return 0;
}

int db_close(struct db *db)
{
	int rc = 0;

	if (db->iq_fill > 0)
		iq_flush(db);
	close(db->dat_fd);
	if (db->index_dirty)
		rc = index_save(db);

	index_free(db);
	free(db->iq_entries);
	free(db->iq_datas);
	free(db->idx_fn);
	free(db->dat_fn);
	free(db);
	return rc;
}

int db_insert(struct db *db, void *key, unsigned int key_size, void *data, unsigned int data_size)
{
	int i;

	if (db->iq_fill == db->iq_size) {
		int rc = iq_flush(db);
		if (rc)
			return rc;
	}
	i = db->iq_fill;
	db->iq_entries[i].data_pos = 0;
	db->iq_entries[i].data_size = data_size;
	db->iq_entries[i].key_size = key_size;
	db->iq_entries[i].key = key;
	db->iq_datas[i] = data;
	db->iq_fill++;
	return 0;
}

int db_remove(struct db *db, const void *key, unsigned int key_size)
{
	if (index_remove(db, key, key_size))
		return 1;
	if (iq_remove(db, key, key_size))
		return 1;
	return 0;
}

int db_query(struct db *db, const void *key, void **datap, unsigned int *data_sizep)
{
	struct db_entry *e;
	void *buf;
	int rc;

	*datap = NULL;
	*data_sizep = 0;

	e = index_search(db, key);
	if (e == NULL) {
		int i;
		
		i = iq_search(db, key);
		if (i == -1)
			return 0;
		*data_sizep = db->iq_entries[i].data_size;
		*datap = xmalloc(*data_sizep);
		memcpy(*datap, db->iq_datas[i], *data_sizep);
		return 1;
	}

	if (lseek(db->dat_fd, e->data_pos, SEEK_SET) == -1) {
		return -1;
	}

	buf = xmalloc(e->data_size);
	rc = read_all(db->dat_fd, buf, e->data_size);
	if (rc == -1) {
		free(buf);
		return -1;
	}
	if (rc != e->data_size) {
		free(buf);
		return -2;
	}

	*data_sizep = e->data_size;
	*datap = buf;
	return 1;
}
