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

#ifndef _SCONF_H
#define _SCONF_H

#include <list.h>

enum {
	SCONF_ERROR_SUCCESS, SCONF_ERROR_ERRNO, SCONF_ERROR_SYNTAX,
	SCONF_ERROR_TYPE, SCONF_ERROR_NOTFOUND,
	NR_SCONF_ERRORS
};

/* this can return -SCONF_ERROR_{ERRNO,SYNTAX}.
 * on syntax error *line contains the line number where error occured. */
extern int sconf_load(struct list_head *head, const char *filename, int *line);

/* this can return -SCONF_ERROR_ERRNO */
extern int sconf_save(struct list_head *head, const char *filename);

extern void sconf_free(struct list_head *head);

/* these can return -SCONF_ERROR_{TYPE,NOTFOUND}.
 * *value is changed _only_ if function returns 0 so you can set *value to
 * some default value before calling these functions. */
extern int sconf_get_str_option(struct list_head *head, const char *name, char **value);
extern int sconf_get_int_option(struct list_head *head, const char *name, int *value);
extern int sconf_get_flt_option(struct list_head *head, const char *name, double *value);
extern int sconf_get_bool_option(struct list_head *head, const char *name, int *value);

/* these can not fail. return value might be changed to void some day. */
extern int sconf_set_str_option(struct list_head *head, const char *name, const char *value);
extern int sconf_set_int_option(struct list_head *head, const char *name, int value);
extern int sconf_set_flt_option(struct list_head *head, const char *name, double value);
extern int sconf_set_bool_option(struct list_head *head, const char *name, int value);

#endif
