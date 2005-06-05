/* 
 * Copyright 2005 Timo Hirvonen
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

#ifndef _PLAY_QUEUE_H
#define _PLAY_QUEUE_H

#include <pl.h>
#include <uchar.h>

struct play_queue_entry {
	struct list_head node;

	struct track_info *track_info;
};

extern pthread_mutex_t play_queue_mutex;

static inline struct play_queue_entry *iter_to_play_queue_entry(struct iter *iter)
{
	return iter->data1;
}

extern struct window *play_queue_win;
extern struct searchable *play_queue_searchable;
extern int play_queue_changed;

extern void play_queue_init(void);
extern void play_queue_exit(void);

/* unlocked */
extern void __play_queue_append(struct track_info *track_info);
extern void __play_queue_prepend(struct track_info *track_info);

extern void play_queue_append(struct track_info *track_info);
extern void play_queue_prepend(struct track_info *track_info);

extern struct track_info *play_queue_remove(void);
extern int play_queue_ch(uchar ch);
extern int play_queue_key(int key);

#define play_queue_lock() cmus_mutex_lock(&play_queue_mutex)
#define play_queue_unlock() cmus_mutex_unlock(&play_queue_mutex)

#endif
