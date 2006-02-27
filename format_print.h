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

#define DEF_FO_STR(ch)	{ { .fo_str  = NULL }, 0, FO_STR,  ch }
#define DEF_FO_INT(ch)	{ { .fo_int  = 0    }, 0, FO_INT,  ch }
#define DEF_FO_TIME(ch)	{ { .fo_time = 0    }, 0, FO_TIME, ch }
#define DEF_FO_END	{ { .fo_str  = NULL }, 0, 0,       0  }

int format_print(char *str, int width, const char *format, const struct format_option *fopts);
int format_valid(const char *format);

#endif
