/*
 * ttman - text to man converter
 *
 * Copyright 2006 Timo Hirvonen <tihirvon@gmail.com>
 *
 * This file is licensed under the GPLv2.
 */
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

struct token {
	struct token *next;
	struct token *prev;
	enum {
		TOK_TEXT,	// max one line w/o \n
		TOK_NL,		// \n
		TOK_ITALIC,	// `
		TOK_BOLD,	// *
		TOK_INDENT,	// \t

		// keywords (@...)
		TOK_H1,
		TOK_H2,
		TOK_LI,
		TOK_BR,
		TOK_PRE,
		TOK_ENDPRE,	// must be after TOK_PRE
		TOK_RAW,
		TOK_ENDRAW,	// must be after TOK_RAW
		TOK_TITLE,	// WRITE 2 2001-12-13 "Linux 2.0.32" "Linux Programmer's Manual"
	} type;
	int line;

	// not NUL-terminated
	const char *text;
	// length of text
	int len;
};

static const char *program;
static const char *filename;
static char tmp_file[1024];
static FILE *outfile;
static int cur_line = 1;
static struct token head = { &head, &head, TOK_TEXT, 0, NULL, 0 };

#define CONST_STR(str) { str, sizeof(str) - 1 }
static const struct {
	const char *str;
	int len;
} token_names[] = {
	CONST_STR("text"),
	CONST_STR("nl"),
	CONST_STR("italic"),
	CONST_STR("bold"),
	CONST_STR("indent"),

	// keywords
	CONST_STR("h1"),
	CONST_STR("h2"),
	CONST_STR("li"),
	CONST_STR("br"),
	CONST_STR("pre"),
	CONST_STR("endpre"),
	CONST_STR("raw"),
	CONST_STR("endraw"),
	CONST_STR("title")
};
#define NR_TOKEN_NAMES (sizeof(token_names) / sizeof(token_names[0]))
#define BUG() die("BUG in %s\n", __FUNCTION__)

#ifdef __GNUC__
#define CMUS_NORETURN	__attribute__((__noreturn__))
#else
#define CMUS_NORETURN
#endif

static CMUS_NORETURN void quit(void)
{
	if (tmp_file[0])
		unlink(tmp_file);
	exit(1);
}

static CMUS_NORETURN void die(const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", program);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	quit();
}

static CMUS_NORETURN void syntax(int line, const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: error: ", filename, line);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	quit();
}

static inline const char *keyword_name(int type)
{
	if (type < TOK_H1 || type > TOK_TITLE)
		die("BUG: no keyword name for type %d\n", type);
	return token_names[type].str;
}

static void *xmalloc(size_t size)
{
	void *ret = malloc(size);

	if (!ret)
		die("OOM when allocating %ul bytes\n", size);
	return ret;
}

static char *memdup(const char *str, int len)
{
	char *s = xmalloc(len + 1);
	memcpy(s, str, len);
	s[len] = 0;
	return s;
}

static struct token *new_token(int type)
{
	struct token *tok = xmalloc(sizeof(struct token));

	tok->prev = NULL;
	tok->next = NULL;
	tok->type = type;
	tok->line = cur_line;
	return tok;
}

static void free_token(struct token *tok)
{
	struct token *prev = tok->prev;
	struct token *next = tok->next;

	if (tok == &head)
		BUG();

	prev->next = next;
	next->prev = prev;
	free(tok);
}

static void emit_token(struct token *tok)
{
	tok->prev = head.prev;
	tok->next = &head;
	head.prev->next = tok;
	head.prev = tok;
}

static void emit(int type)
{
	struct token *tok = new_token(type);
	tok->len = 0;
	tok->text = NULL;
	emit_token(tok);
}

static int emit_keyword(const char *buf, int size)
{
	int i, len;

	for (len = 0; len < size; len++) {
		if (!isalnum((unsigned char)buf[len]))
			break;
	}

	if (!len)
		syntax(cur_line, "keyword expected\n");

	for (i = TOK_H1; i < NR_TOKEN_NAMES; i++) {
		if (len != token_names[i].len)
			continue;
		if (!strncmp(buf, token_names[i].str, len)) {
			emit(i);
			return len;
		}
	}
	syntax(cur_line, "invalid keyword '@%s'\n", memdup(buf, len));
}

static int emit_text(const char *buf, int size)
{
	struct token *tok;
	int i;

	for (i = 0; i < size; i++) {
		int c = buf[i];
		if (c == '@' || c == '`' || c == '*' || c == '\n' || c == '\\' || c == '\t')
			break;
	}
	tok = new_token(TOK_TEXT);
	tok->text = buf;
	tok->len = i;
	emit_token(tok);
	return i;
}

static void tokenize(const char *buf, int size)
{
	int pos = 0;

	while (pos < size) {
		struct token *tok;
		int ch;

		ch = buf[pos++];
		switch (ch) {
		case '@':
			pos += emit_keyword(buf + pos, size - pos);
			break;
		case '`':
			emit(TOK_ITALIC);
			break;
		case '*':
			emit(TOK_BOLD);
			break;
		case '\n':
			emit(TOK_NL);
			cur_line++;
			break;
		case '\t':
			emit(TOK_INDENT);
			break;
		case '\\':
			tok = new_token(TOK_TEXT);
			tok->text = buf + pos;
			tok->len = 1;
			pos++;
			if (pos == size || buf[pos] == '\n') {
				// just one '\\'
				tok->text--;
			}

			if (tok->text[0] == '\\') {
				tok->text = "\\\\";
				tok->len = 2;
			}

			emit_token(tok);
			break;
		default:
			pos--;
			pos += emit_text(buf + pos, size - pos);
			break;
		}
	}
}

static int is_empty_line(const struct token *tok)
{
	while (tok != &head) {
		int i;

		switch (tok->type) {
		case TOK_TEXT:
			for (i = 0; i < tok->len; i++) {
				if (tok->text[i] != ' ')
					return 0;
			}
			break;
		case TOK_INDENT:
			break;
		case TOK_NL:
			return 1;
		default:
			return 0;
		}
		tok = tok->next;
	}
	return 1;
}

static struct token *remove_line(struct token *tok)
{
	while (tok != &head) {
		struct token *next = tok->next;
		int type = tok->type;

		free_token(tok);
		tok = next;
		if (type == TOK_NL)
			break;
	}
	return tok;
}

static struct token *skip_after(struct token *tok, int type)
{
	struct token *save = tok;

	while (tok != &head) {
		if (tok->type == type) {
			tok = tok->next;
			if (tok->type != TOK_NL)
				syntax(tok->line, "newline expected after @%s\n",
						keyword_name(type));
			return tok->next;
		}
		if (tok->type >= TOK_H1)
			syntax(tok->line, "keywords not allowed betweed @%s and @%s\n",
					keyword_name(type-1), keyword_name(type));
		tok = tok->next;
	}
	syntax(save->prev->line, "missing @%s\n", keyword_name(type));
}

static struct token *get_next_line(struct token *tok)
{
	while (tok != &head) {
		int type = tok->type;

		tok = tok->next;
		if (type == TOK_NL)
			break;
	}
	return tok;
}

static struct token *get_indent(struct token *tok, int *ip)
{
	int i = 0;

	while (tok != &head && tok->type == TOK_INDENT) {
		tok = tok->next;
		i++;
	}
	*ip = i;
	return tok;
}

// line must be non-empty
static struct token *check_line(struct token *tok, int *ip)
{
	struct token *start;
	int tok_type;

	start = tok = get_indent(tok, ip);

	tok_type = tok->type;
	switch (tok_type) {
	case TOK_TEXT:
	case TOK_BOLD:
	case TOK_ITALIC:
	case TOK_BR:
		tok = tok->next;
		while (tok != &head) {
			switch (tok->type) {
			case TOK_TEXT:
			case TOK_BOLD:
			case TOK_ITALIC:
			case TOK_BR:
			case TOK_INDENT:
				break;
			case TOK_NL:
				return start;
			default:
				syntax(tok->line, "@%s not allowed inside paragraph\n",
						keyword_name(tok->type));
			}
			tok = tok->next;
		}
		break;
	case TOK_H1:
	case TOK_H2:
	case TOK_TITLE:
		if (*ip)
			goto indentation;

		// check arguments
		tok = tok->next;
		while (tok != &head) {
			switch (tok->type) {
			case TOK_TEXT:
			case TOK_INDENT:
				break;
			case TOK_NL:
				return start;
			default:
				syntax(tok->line, "@%s can contain only text\n",
						keyword_name(tok_type));
			}
			tok = tok->next;
		}
		break;
	case TOK_LI:
		// check arguments
		tok = tok->next;
		while (tok != &head) {
			switch (tok->type) {
			case TOK_TEXT:
			case TOK_BOLD:
			case TOK_ITALIC:
			case TOK_INDENT:
				break;
			case TOK_NL:
				return start;
			default:
				syntax(tok->line, "@%s not allowed inside @li\n",
						keyword_name(tok->type));
			}
			tok = tok->next;
		}
		break;
	case TOK_PRE:
		// checked later
		break;
	case TOK_RAW:
		if (*ip)
			goto indentation;
		// checked later
		break;
	case TOK_ENDPRE:
	case TOK_ENDRAW:
		syntax(tok->line, "@%s not expected\n", keyword_name(tok->type));
		break;
	case TOK_NL:
	case TOK_INDENT:
		BUG();
		break;
	}
	return start;
indentation:
	syntax(tok->line, "indentation before @%s\n", keyword_name(tok->type));
}

static void insert_nl_before(struct token *next)
{
	struct token *prev = next->prev;
	struct token *new = new_token(TOK_NL);

	new->prev = prev;
	new->next = next;
	prev->next = new;
	next->prev = new;
}

static void normalize(void)
{
	struct token *tok = head.next;
	/*
	 * >= 0 if previous line was text (== amount of indent)
	 *   -1 if previous block was @pre (amount of indent doesn't matter)
	 *   -2 otherwise (@h1 etc., indent was 0)
	 */
	int prev_indent = -2;

	while (tok != &head) {
		struct token *start;
		int i, new_para = 0;

		// remove empty lines
		while (is_empty_line(tok)) {
			tok = remove_line(tok);
			new_para = 1;
			if (tok == &head)
				return;
		}

		// skips indent
		start = tok;
		tok = check_line(tok, &i);

		switch (tok->type) {
		case TOK_TEXT:
		case TOK_ITALIC:
		case TOK_BOLD:
		case TOK_BR:
			// normal text
			if (new_para && prev_indent >= -1) {
				// previous line/block was text or @pre
				// and there was a empty line after it
				insert_nl_before(start);
			}

			if (!new_para && prev_indent == i) {
				// join with previous line
				struct token *nl = start->prev;

				if (nl->type != TOK_NL)
					BUG();

				if ((nl->prev != &head && nl->prev->type == TOK_BR) ||
						tok->type == TOK_BR) {
					// don't convert \n after/before @br to ' '
					free_token(nl);
				} else {
					// convert "\n" to " "
					nl->type = TOK_TEXT;
					nl->text = " ";
					nl->len = 1;
				}

				// remove indent
				while (start->type == TOK_INDENT) {
					struct token *next = start->next;
					free_token(start);
					start = next;
				}
			}

			prev_indent = i;
			tok = get_next_line(tok);
			break;
		case TOK_PRE:
		case TOK_RAW:
			// these can be directly after normal text
			// but not joined with the previous line
			if (new_para && prev_indent >= -1) {
				// previous line/block was text or @pre
				// and there was a empty line after it
				insert_nl_before(start);
			}
			tok = skip_after(tok->next, tok->type + 1);
			prev_indent = -1;
			break;
		case TOK_H1:
		case TOK_H2:
		case TOK_LI:
		case TOK_TITLE:
			// remove white space after H1, H2, L1 and TITLE
			tok = tok->next;
			while (tok != &head) {
				int type = tok->type;
				struct token *next;

				if (type == TOK_TEXT) {
					while (tok->len && *tok->text == ' ') {
						tok->text++;
						tok->len--;
					}
					if (tok->len)
						break;
				}
				if (type != TOK_INDENT)
					break;

				// empty TOK_TEXT or TOK_INDENT
				next = tok->next;
				free_token(tok);
				tok = next;
			}
			// not normal text. can't be joined
			prev_indent = -2;
			tok = get_next_line(tok);
			break;
		case TOK_NL:
		case TOK_INDENT:
		case TOK_ENDPRE:
		case TOK_ENDRAW:
			BUG();
			break;
		}
	}
}

#define output(...) fprintf(outfile, __VA_ARGS__)

static void output_buf(const char *buf, int len)
{
	fwrite(buf, 1, len, outfile);
}

static void output_text(struct token *tok)
{
	char buf[1024];
	const char *str = tok->text;
	int len = tok->len;
	int pos = 0;

	while (len) {
		int c = *str++;

		if (pos >= sizeof(buf) - 1) {
			output_buf(buf, pos);
			pos = 0;
		}
		if (c == '-')
			buf[pos++] = '\\';
		buf[pos++] = c;
		len--;
	}

	if (pos)
		output_buf(buf, pos);
}

static int bold = 0;
static int italic = 0;
static int indent = 0;

static struct token *output_pre(struct token *tok)
{
	int bol = 1;

	if (tok->type != TOK_NL)
		syntax(tok->line, "newline expected after @pre\n");

	output(".nf\n");
	tok = tok->next;
	while (tok != &head) {
		if (bol) {
			int i;

			tok = get_indent(tok, &i);
			if (i != indent && tok->type != TOK_NL)
				syntax(tok->line, "indent changed in @pre\n");
		}

		switch (tok->type) {
		case TOK_TEXT:
			if (bol && tok->len && tok->text[0] == '.')
				output("\\&");
			output_text(tok);
			break;
		case TOK_NL:
			output("\n");
			bol = 1;
			tok = tok->next;
			continue;
		case TOK_ITALIC:
			output("`");
			break;
		case TOK_BOLD:
			output("*");
			break;
		case TOK_INDENT:
			// FIXME: warn
			output(" ");
			break;
		case TOK_ENDPRE:
			output(".fi\n");
			tok = tok->next;
			if (tok != &head && tok->type == TOK_NL)
				tok = tok->next;
			return tok;
		default:
			BUG();
			break;
		}
		bol = 0;
		tok = tok->next;
	}
	return tok;
}

static struct token *output_raw(struct token *tok)
{
	if (tok->type != TOK_NL)
		syntax(tok->line, "newline expected after @raw\n");

	tok = tok->next;
	while (tok != &head) {
		switch (tok->type) {
		case TOK_TEXT:
			if (tok->len == 2 && !strncmp(tok->text, "\\\\", 2)) {
				/* ugly special case
				 * "\\" (\) was converted to "\\\\" (\\) because
				 * nroff does escaping too.
				 */
				output("\\");
			} else {
				output_buf(tok->text, tok->len);
			}
			break;
		case TOK_NL:
			output("\n");
			break;
		case TOK_ITALIC:
			output("`");
			break;
		case TOK_BOLD:
			output("*");
			break;
		case TOK_INDENT:
			output("\t");
			break;
		case TOK_ENDRAW:
			tok = tok->next;
			if (tok != &head && tok->type == TOK_NL)
				tok = tok->next;
			return tok;
		default:
			BUG();
			break;
		}
		tok = tok->next;
	}
	return tok;
}

static struct token *output_para(struct token *tok)
{
	int bol = 1;

	while (tok != &head) {
		switch (tok->type) {
		case TOK_TEXT:
			output_text(tok);
			break;
		case TOK_ITALIC:
			italic ^= 1;
			if (italic) {
				output("\\fI");
			} else {
				output("\\fR");
			}
			break;
		case TOK_BOLD:
			bold ^= 1;
			if (bold) {
				output("\\fB");
			} else {
				output("\\fR");
			}
			break;
		case TOK_BR:
			if (bol) {
				output(".br\n");
			} else {
				output("\n.br\n");
			}
			bol = 1;
			tok = tok->next;
			continue;
		case TOK_NL:
			output("\n");
			return tok->next;
		case TOK_INDENT:
			output(" ");
			break;
		default:
			BUG();
			break;
		}
		bol = 0;
		tok = tok->next;
	}
	return tok;
}

static struct token *title(struct token *tok, const char *cmd)
{
	output("%s", cmd);
	return output_para(tok->next);
}

static struct token *dump_one(struct token *tok)
{
	int i;

	tok = get_indent(tok, &i);
	if (tok->type != TOK_RAW) {
		while (indent < i) {
			output(".RS\n");
			indent++;
		}
		while (indent > i) {
			output(".RE\n");
			indent--;
		}
	}

	switch (tok->type) {
	case TOK_TEXT:
	case TOK_ITALIC:
	case TOK_BOLD:
	case TOK_BR:
		if (tok->type == TOK_TEXT && tok->len && tok->text[0] == '.')
			output("\\&");
		tok = output_para(tok);
		break;
	case TOK_H1:
		tok = title(tok, ".SH ");
		break;
	case TOK_H2:
		tok = title(tok, ".SS ");
		break;
	case TOK_LI:
		tok = title(tok, ".TP\n");
		break;
	case TOK_PRE:
		tok = output_pre(tok->next);
		break;
	case TOK_RAW:
		tok = output_raw(tok->next);
		break;
	case TOK_TITLE:
		tok = title(tok, ".TH ");
		// must be after .TH
		// no hyphenation, adjust left
		output(".nh\n.ad l\n");
		break;
	case TOK_NL:
		output("\n");
		tok = tok->next;
		break;
	case TOK_ENDPRE:
	case TOK_ENDRAW:
	case TOK_INDENT:
		BUG();
		break;
	}
	return tok;
}

static void dump(void)
{
	struct token *tok = head.next;

	while (tok != &head)
		tok = dump_one(tok);
}

static void process(void)
{
	struct stat s = {};
	const char *buf;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd == -1)
		die("opening `%s' for reading: %s\n", filename, strerror(errno));
	fstat(fd, &s);
	if (s.st_size) {
		buf = mmap(NULL, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (buf == MAP_FAILED)
			die("mmap: %s\n", strerror(errno));

		tokenize(buf, s.st_size);
		normalize();
	}
	close(fd);
	dump();
}

int main(int argc, char *argv[])
{
	const char *dest;
	int fd;

	program = argv[0];
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <in> <out>\n", program);
		return 1;
	}
	filename = argv[1];
	dest = argv[2];

	snprintf(tmp_file, sizeof(tmp_file), "%s.XXXXXX", dest);
	fd = mkstemp(tmp_file);
	if (fd < 0)
		die("creating %s: %s\n", tmp_file, strerror(errno));
	outfile = fdopen(fd, "w");
	if (!outfile)
		die("opening %s: %s\n", tmp_file, strerror(errno));

	process();
	if (rename(tmp_file, dest))
		die("renaming %s to %s: %s\n", tmp_file, dest, strerror(errno));
	return 0;
}
