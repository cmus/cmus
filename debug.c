/* 
 * Copyright 2004-2005 Timo Hirvonen
 */

#include <debug.h>
#include <prog.h>

#include <stdio.h>
#include <stdarg.h>

#if DEBUG > 1
static FILE *debug_stream = NULL;
#endif

void debug_init(void)
{
#if DEBUG > 1
	char filename[512];
	const char *tmp = getenv("TMPDIR");

	if (!tmp || !tmp[0])
		tmp = "/tmp";

	snprintf(filename, sizeof(filename), "%s/cmus-debug", tmp);
	debug_stream = fopen(filename, "w");
	if (debug_stream == NULL)
		die_errno("error opening `%s' for writing", filename);
#endif
}

/* This function must be defined even if debugging is disabled in the program
 * because debugging might still be enabled in some plugin.
 */
void __debug_bug(const char *function, const char *fmt, ...)
{
	const char *format = "\n%s: BUG: ";
	va_list ap;

	/* debug_stream exists only if debugging is enabled */
#if DEBUG > 1
	fprintf(debug_stream, format, function);
	va_start(ap, fmt);
	vfprintf(debug_stream, fmt, ap);
	va_end(ap);
#endif

	/* always print bug message to stderr */
	fprintf(stderr, format, function);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(127);
}

void __debug_print(const char *function, const char *fmt, ...)
{
#if DEBUG > 1
	va_list ap;

	fprintf(debug_stream, "%s: ", function);
	va_start(ap, fmt);
	vfprintf(debug_stream, fmt, ap);
	va_end(ap);
	fflush(debug_stream);
#endif
}
