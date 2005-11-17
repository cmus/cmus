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

#include <track_db.h>
#include <db.h>
#include <xmalloc.h>
#include <player.h>
#include <utils.h>
#include <debug.h>

#include <inttypes.h>

struct track_db {
	struct db *db;
};

/*
 * data format:
 *
 * u32 mtime
 * u32 duration
 * N
 *   string key
 *   string value
 */

struct track_db *track_db_new(const char *filename_base)
{
	struct track_db *db;
	int rc;

	db = xnew(struct track_db, 1);
	db->db = db_new(filename_base);
	rc = db_load(db->db);
	if (rc) {
		d_print("error: %s\n", rc == -1 ? strerror(errno) : "-2");
	}
	return db;
}

int track_db_close(struct track_db *db)
{
	int rc;

	rc = db_close(db->db);
	free(db);
	return rc;
}

void track_db_insert(struct track_db *db, const char *filename, struct track_info *ti)
{
	char *key, *data, *ptr;
	int data_size, i, rc;

	data_size = 8;
	for (i = 0; ti->comments[i].key; i++) {
		data_size += strlen(ti->comments[i].key) + 1;
		data_size += strlen(ti->comments[i].val) + 1;
	}
	data = xmalloc(data_size);
	*(uint32_t *)(data + 0) = ti->mtime;
	*(uint32_t *)(data + 4) = ti->duration;
	ptr = data + 8;
	for (i = 0; ti->comments[i].key; i++) {
		char *s;
		int size;

		s = ti->comments[i].key;
		size = strlen(s) + 1;
		memcpy(ptr, s, size);
		ptr += size;

		s = ti->comments[i].val;
		size = strlen(s) + 1;
		memcpy(ptr, s, size);
		ptr += size;
	}
	key = xstrdup(filename);
	rc = db_insert(db->db, key, strlen(key) + 1, data, data_size);
	if (rc) {
		d_print("error: %s\n", strerror(errno));
		free(data);
	}
}

static struct track_info *data_to_track_info(const void *data, unsigned int data_size)
{
	struct track_info *ti;
	const char *str;
	int count, i;

	if (data_size < 8)
		return NULL;
	ti = xnew(struct track_info, 1);
	ti->ref = 1;
	ti->filename = NULL;

	str = data;
	ti->mtime = *(uint32_t *)str; str += 4;
	ti->duration = *(uint32_t *)str; str += 4;

	count = 0;
	if (data_size > 8) {
		int pos = 0;

		while (pos < data_size - 8) {
			if (str[pos] == 0)
				count++;
			pos++;
		}
		if (str[data_size - 9] != 0 || count % 2) {
			free(ti);
			return NULL;
		}
		count /= 2;
	}
	ti->comments = xnew(struct keyval, count + 1);
	for (i = 0; i < count; i++) {
		int len;
		char *s;

		len = strlen(str);
		s = xmalloc(len + 1);
		memcpy(s, str, len + 1);
		str += len + 1;
		ti->comments[i].key = s;

		len = strlen(str);
		s = xmalloc(len + 1);
		memcpy(s, str, len + 1);
		str += len + 1;
		ti->comments[i].val = s;
	}
	ti->comments[i].key = NULL;
	ti->comments[i].val = NULL;
	return ti;
}

struct track_info *track_db_get_track(struct track_db *db, const char *filename)
{
	struct track_info *ti;
	struct keyval *comments;
	int duration;
	void *data;
	unsigned int data_size;
	int rc;
	time_t mtime = file_get_mtime(filename);

	rc = db_query(db->db, filename, &data, &data_size);
	if (rc == 1) {
		/* found */
		ti = data_to_track_info(data, data_size);
		free(data);
		if (mtime != -1 && ti->mtime == mtime) {
			/* mtime not changed, return data */
			ti->filename = xstrdup(filename);
			return ti;
		}

		/* db data not up to date, remove data  */
		db_remove(db->db, filename, strlen(filename) + 1);
		track_info_unref(ti);
	} else if (rc == 0) {
		/* not found */
	} else {
		/* error */
		d_print("error: %s\n", rc == -1 ? strerror(errno) : "-2");
		return NULL;
	}

	if (player_get_fileinfo(filename, &duration, &comments)) {
		d_print("INVALID: '%s'\n", filename);
		return NULL;
	}
	ti = xnew(struct track_info, 1);
	ti->ref = 1;
	ti->filename = xstrdup(filename);
	ti->comments = comments;
	ti->duration = duration;
	ti->mtime = mtime;
	track_db_insert(db, filename, ti);
	return ti;
}
