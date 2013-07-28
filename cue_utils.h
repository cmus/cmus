/*
 * Copyright (C) 2008-2013 Various Authors
 * Copyright (C) 2011 Gregory Petrosyan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef __CUE_UTILS_H__
#define __CUE_UTILS_H__

#include <stdio.h>

/*
 * warning: this header does not contain include guards!
 */
#include <libcue/libcue.h>


/*
 * libcue developers think that printing parsing errors to stderr is a good idea
 * they are wrong
 */
Cd *cue_parse_file__no_stderr_garbage(FILE *f);

char *associated_cue(const char *filename);
int cue_get_ntracks(const char *filename);
char *construct_cue_url(const char *cue_filename, int track_n);


#endif
