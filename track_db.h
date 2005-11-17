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

#ifndef _TRACK_DB_H
#define _TRACK_DB_H

#include <track_info.h>

struct track_db;

extern struct track_db *track_db_new(const char *filename_base);
extern int track_db_close(struct track_db *db);
extern void track_db_insert(struct track_db *db, const char *filename, struct track_info *ti);
extern struct track_info *track_db_get_track(struct track_db *db, const char *filename);

#endif
