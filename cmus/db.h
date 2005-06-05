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

#ifndef _DB_H
#define _DB_H

struct db;

/* extern struct db *db_new(const char *filename_base, int (*key_cmp)(const void *, const void *)); */
extern struct db *db_new(const char *filename_base);
extern int db_load(struct db *db);
extern int db_close(struct db *db);
extern int db_insert(struct db *db, void *key, unsigned int key_size, void *data, unsigned int data_size);
extern int db_remove(struct db *db, const void *key, unsigned int key_size);
extern int db_query(struct db *db, const void *key, void **datap, unsigned int *data_sizep);

#endif
