/*
 * Copyright 2011-2013 Various Authors
 * Copyright 2011 Johannes Weißl
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

#ifndef _DISCID_H
#define _DISCID_H

char *get_default_cdda_device(void);
int parse_cdda_url(const char *url, char **disc_id, int *start_track, int *end_track);
char *gen_cdda_url(const char *disc_id, int start_track, int end_track);
char *complete_cdda_url(const char *device, const char *url);
int get_disc_id(const char *device, char **disc_id, int *num_tracks);

#endif
