#ifndef _FORMAT_PRINT_H
#define _FORMAT_PRINT_H

struct format_option {
	union {
		/* NULL is treated like "" */
		const char *fo_str;
		int fo_int;
		/* [h:]mm:ss. can be negative */
		int fo_time;
	};
	/* set to 1 if you want to disable printing */
	unsigned int empty : 1;
	enum { FO_STR, FO_INT, FO_TIME } type;
	char ch;
};

/* gcc < 4.6 and icc < 12.0 can't properly initialize anonymous unions */
#if (defined(__GNUC__) && defined(__GNUC_MINOR__) && (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 6))) || \
	(defined(__INTEL_COMPILER) && __INTEL_COMPILER < 1200)
#define UNION_INIT(f, v) { .f = v }
#else
#define UNION_INIT(f, v) .f = v
#endif

#define DEF_FO_STR(c)	{ UNION_INIT(fo_str,  NULL), .type = FO_STR,  .ch = c }
#define DEF_FO_INT(c)	{ UNION_INIT(fo_int,  0),    .type = FO_INT,  .ch = c }
#define DEF_FO_TIME(c)	{ UNION_INIT(fo_time, 0),    .type = FO_TIME, .ch = c }
#define DEF_FO_END	{ UNION_INIT(fo_str,  NULL), .type = 0,       .ch = 0 }

int format_print(char *str, int width, const char *format, const struct format_option *fopts);
int format_valid(const char *format);

#endif
