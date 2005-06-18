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

#ifndef _TABEXP_H
#define _TABEXP_H

struct tabexp {
	char *head;
	char **tails;
	int nr_tails;
	int index;
	/* tabexp is in resetted state when this is called
	 * if no matches then tabexp must be left in resetted state
	 */
	void (*load_matches)(struct tabexp *tabexp, const char *src);

	/* the private_data arg given to tabexp_new() */
	void *private_data;
};

extern struct tabexp *tabexp_new(void (*load_matches)(struct tabexp *tabexp, const char *src), void *private_data);
extern void tabexp_free(struct tabexp *tabexp);

/* head = NULL
 * tails = NULL
 * nr_tails = 0
 * index = -1
 */
extern void tabexp_reset(struct tabexp *tabexp);

/* return expanded src or NULL */
extern char *tabexp_expand(struct tabexp *tabexp, const char *src);

#endif
