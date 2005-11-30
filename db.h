/* 
 * Copyright 2004 Timo Hirvonen
 */

#ifndef _DB_H
#define _DB_H

struct db;

struct db *db_new(const char *filename_base);
int db_load(struct db *db);
int db_close(struct db *db);
int db_insert(struct db *db, char *key, void *data, unsigned int data_size);
int db_remove(struct db *db, const char *key);
int db_query(struct db *db, const char *key, void **datap, unsigned int *data_sizep);

#endif
