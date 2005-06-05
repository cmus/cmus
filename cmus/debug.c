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

#include <debug.h>

#include <stdio.h>
#include <stdarg.h>

extern char *program_name;

#if DEBUG > 0

static FILE *debug_stream = NULL;

void debug_init(FILE *stream)
{
	debug_stream = stream;
}

void __debug_warn(const char *file, int line, const char *function, const char *fmt, ...)
{
	const char *format = "\n%s: %s:%d: %s: BUG: ";
	va_list ap;

	va_start(ap, fmt);
	fprintf(debug_stream, format, program_name, file, line, function);
	vfprintf(debug_stream, fmt, ap);
	fflush(debug_stream);
	if (debug_stream != stdout && debug_stream != stderr) {
		fprintf(stderr, format, program_name, file, line, function);
		vfprintf(stderr, fmt, ap);
		fflush(stderr);
	}
	va_end(ap);
}

#if DEBUG > 1

void __debug_print(const char *function, const char *fmt, ...)
{
	va_list ap;

	fprintf(debug_stream, "%s: ", function);
	va_start(ap, fmt);
	vfprintf(debug_stream, fmt, ap);
	va_end(ap);
	fflush(debug_stream);
}

#endif

#endif
