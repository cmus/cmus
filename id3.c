/*
 * Copyright 2005 Timo Hirvonen
 */

#include "id3.h"
#include <sconf.h>
#include <xmalloc.h>
#include <utf8_encode.h>
#include <uchar.h>
#include <utils.h>
#include <debug.h>

#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <stdio.h>

/*
 * position:
 *
 *    0 "ID3"
 *  -10 "3DI"
 * -128 "TAG"
 * -138 "3DI"
 *
 * if v2 is at beginning _and_ at end then there must be a seek tag at beginning
 */

struct ID3 {
	char v1[128];
	char *v2[NUM_ID3_KEYS];

	unsigned int has_v1 : 1;
	unsigned int has_v2 : 1;
};

struct v2_header {
	unsigned char ver_major;
	unsigned char ver_minor;
	unsigned char flags;
	uint32_t size;
};

struct v2_extended_header {
	uint32_t size;
};

struct v2_frame_header {
	char id[4];
	uint32_t size;
	uint16_t flags;
};

#define V2_HEADER_UNSYNC	(1 << 7)
#define V2_HEADER_EXTENDED	(1 << 6)
#define V2_HEADER_EXPERIMENTAL	(1 << 5)
#define V2_HEADER_FOOTER	(1 << 4)

#define V2_FRAME_COMPRESSED	(1 << 3) /* great idea!!1 */
#define V2_FRAME_ENCRYPTHED	(1 << 2) /* wow, this is very neat! */
#define V2_FRAME_UNSYNC		(1 << 1)
#define V2_FRAME_LEN_INDICATOR	(1 << 0)

static char *default_charset = NULL;

#define NR_GENRES 148
/* genres {{{ */
static const char *genres[NR_GENRES] = {
	"Blues",
	"Classic Rock",
	"Country",
	"Dance",
	"Disco",
	"Funk",
	"Grunge",
	"Hip-Hop",
	"Jazz",
	"Metal",
	"New Age",
	"Oldies",
	"Other",
	"Pop",
	"R&B",
	"Rap",
	"Reggae",
	"Rock",
	"Techno",
	"Industrial",
	"Alternative",
	"Ska",
	"Death Metal",
	"Pranks",
	"Soundtrack",
	"Euro-Techno",
	"Ambient",
	"Trip-Hop",
	"Vocal",
	"Jazz+Funk",
	"Fusion",
	"Trance",
	"Classical",
	"Instrumental",
	"Acid",
	"House",
	"Game",
	"Sound Clip",
	"Gospel",
	"Noise",
	"Alt",
	"Bass",
	"Soul",
	"Punk",
	"Space",
	"Meditative",
	"Instrumental Pop",
	"Instrumental Rock",
	"Ethnic",
	"Gothic",
	"Darkwave",
	"Techno-Industrial",
	"Electronic",
	"Pop-Folk",
	"Eurodance",
	"Dream",
	"Southern Rock",
	"Comedy",
	"Cult",
	"Gangsta Rap",
	"Top 40",
	"Christian Rap",
	"Pop/Funk",
	"Jungle",
	"Native American",
	"Cabaret",
	"New Wave",
	"Psychedelic",
	"Rave",
	"Showtunes",
	"Trailer",
	"Lo-Fi",
	"Tribal",
	"Acid Punk",
	"Acid Jazz",
	"Polka",
	"Retro",
	"Musical",
	"Rock & Roll",
	"Hard Rock",
	"Folk",
	"Folk/Rock",
	"National Folk",
	"Swing",
	"Fast-Fusion",
	"Bebob",
	"Latin",
	"Revival",
	"Celtic",
	"Bluegrass",
	"Avantgarde",
	"Gothic Rock",
	"Progressive Rock",
	"Psychedelic Rock",
	"Symphonic Rock",
	"Slow Rock",
	"Big Band",
	"Chorus",
	"Easy Listening",
	"Acoustic",
	"Humour",
	"Speech",
	"Chanson",
	"Opera",
	"Chamber Music",
	"Sonata",
	"Symphony",
	"Booty Bass",
	"Primus",
	"Porn Groove",
	"Satire",
	"Slow Jam",
	"Club",
	"Tango",
	"Samba",
	"Folklore",
	"Ballad",
	"Power Ballad",
	"Rhythmic Soul",
	"Freestyle",
	"Duet",
	"Punk Rock",
	"Drum Solo",
	"A Cappella",
	"Euro-House",
	"Dance Hall",
	"Goa",
	"Drum & Bass",
	"Club-House",
	"Hardcore",
	"Terror",
	"Indie",
	"BritPop",
	"Negerpunk",
	"Polsk Punk",
	"Beat",
	"Christian Gangsta Rap",
	"Heavy Metal",
	"Black Metal",
	"Crossover",
	"Contemporary Christian",
	"Christian Rock",
	"Merengue",
	"Salsa",
	"Thrash Metal",
	"Anime",
	"JPop",
	"Synthpop"
};
/* }}} */

#if 1
#define id3_debug(...) d_print(__VA_ARGS__)
#else
#define id3_debug(...) do { } while (0)
#endif

static int utf16_is_special(const uchar uch)
{
	if (UTF16_IS_HSURROGATE(uch) || UTF16_IS_LSURROGATE(uch) || UTF16_IS_BOM(uch))
		return -1;
	return 0;
}

static char *utf16_to_utf8(const unsigned char *buf, int buf_size)
{
	char *out;
	int i, idx;

	out = xnew(char, (buf_size / 2) * 4 + 1);
	i = idx = 0;
	while (buf_size - i >= 2) {
		uchar u;

		u = buf[i] + (buf[i + 1] << 8);
		if (u_is_unicode(u)) {
			if (utf16_is_special(u) == 0)
				u_set_char(out, &idx, u);
		} else {
			free(out);
			return NULL;
		}
		if (u == 0)
			return out;
		i += 2;
	}
	u_set_char(out, &idx, 0);
	return out;
}

static char *utf16be_to_utf8(const unsigned char *buf, int buf_size)
{
	char *out;
	int i, idx;

	out = xnew(char, (buf_size / 2) * 4 + 1);
	i = 0;
	idx = 0;
	while (buf_size - i >= 2) {
		uchar u;

		u = buf[i + 1] + (buf[i] << 8);
		if (u_is_unicode(u)) {
		       if (utf16_is_special(u) == 0)
				u_set_char(out, &idx, u);
		} else {
			free(out);
			return NULL;
		}
		if (u == 0)
			return out;
		i += 2;
	}
	u_set_char(out, &idx, 0);
	return out;
}

static int is_v1(const char *buf)
{
	return buf[0] == 'T' && buf[1] == 'A' && buf[2] == 'G';
}

static int u32_unsync(const unsigned char *buf, uint32_t *up)
{
	uint32_t b, u = 0;
	int i;

	for (i = 0; i < 4; i++) {
		b = buf[i];
		if (b >= 0x80)
			return 0;
		u <<= 7;
		u |= b;
	}
	*up = u;
	return 1;
}

static void get_u24(const unsigned char *buf, uint32_t *up)
{
	uint32_t b, u = 0;
	int i;

	for (i = 0; i < 3; i++) {
		b = buf[i];
		u <<= 8;
		u |= b;
	}
	*up = u;
}

static int v2_header_footer_parse(struct v2_header *header, const char *buf)
{
	header->ver_major = buf[3];
	header->ver_minor = buf[4];
	header->flags = buf[5];
	if (header->ver_major == 0xff || header->ver_minor == 0xff)
		return 0;
	return u32_unsync((const unsigned char *)(buf + 6), &header->size);
}

static int v2_header_parse(struct v2_header *header, const char *buf)
{
	if (buf[0] != 'I' || buf[1] != 'D' || buf[2] != '3')
		return 0;
	return v2_header_footer_parse(header, buf);
}

static int v2_footer_parse(struct v2_header *header, const char *buf)
{
	if (buf[0] != '3' || buf[1] != 'D' || buf[2] != 'I')
		return 0;
	return v2_header_footer_parse(header, buf);
}

static int v2_extended_header_parse(struct v2_extended_header *header, const char *buf)
{
	return u32_unsync((const unsigned char *)buf, &header->size);
}

static int is_frame_id_char(char ch)
{
	return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

/* XXXYYY
 *
 * X = [A-Z0-9]
 * Y = byte
 *
 * XXX is frame
 * YYY is frame size excluding this 6 byte header
 */
static int v2_2_0_frame_header_parse(struct v2_frame_header *header, const char *buf)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (!is_frame_id_char(buf[i]))
			return 0;
		header->id[i] = buf[i];
	}
	header->id[3] = 0;
	get_u24((const unsigned char *)(buf + 3), &header->size);
	header->flags = 0;
	if (header->size == 0)
		return 0;
	id3_debug("%c%c%c %d\n", header->id[0], header->id[1], header->id[2], header->size);
	return 1;
}

/* XXXXYYYYZZ
 *
 * X = [A-Z0-9]
 * Y = byte
 * Z = byte
 *
 * XXXX is frame
 * YYYY is frame size excluding this 10 byte header
 * ZZ   is flags
 */
static int v2_3_0_frame_header_parse(struct v2_frame_header *header, const char *buf)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (!is_frame_id_char(buf[i]))
			return 0;
		header->id[i] = buf[i];
	}
	if (!u32_unsync((const unsigned char *)(buf + 4), &header->size))
		return 0;
	header->flags = (buf[8] << 8) | buf[9];
	if (header->size == 0)
		return 0;
	id3_debug("%c%c%c%c %d\n", header->id[0], header->id[1], header->id[2], header->id[3], header->size);
	return 1;
}

static int read_all(int fd, char *buf, size_t size)
{
	size_t pos = 0;

	while (pos < size) {
		int rc = read(fd, buf + pos, size - pos);

		if (rc == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -1;
		}
		pos += rc;
	}
	return 0;
}

/* (.*).+   = .+
 * (RX)     = Remix
 * (CR)     = Cover
 * ([0-9]+) = genres[id]
 *
 */
static char *parse_genre(char *str)
{
	char *id, *s;
	int i;

	if (*str != '(')
		return str;

	id = str + 1;
	s = strchr(id, ')');
	if (s == NULL) {
		/* ([^)]* */
		return str;
	}

	if (s == id) {
		/* ().* */
		return str;
	}

	if (s[1]) {
		/* (.+)GENRE => GENRE */
		s = xstrdup(s + 1);
		free(str);
		return s;
	}

	/* (.+) */
	if (strncmp(id, "RX", s - id) == 0) {
		free(str);
		return xstrdup("Remix");
	}
	if (strncmp(id, "CR", s - id) == 0) {
		free(str);
		return xstrdup("Cover");
	}

	i = 0;
	while (id < s) {
		char ch = *id++;

		if (ch < '0')
			return str;
		if (ch > '9')
			return str;
		i *= 10;
		i += ch - '0';
	}
	if (i >= NR_GENRES)
		return str;
	free(str);
	return xstrdup(genres[i]);
}

/* http://www.id3.org/id3v2.4.0-structure.txt */
static struct {
	const char name[8];
	enum id3_key key;
} frame_tab[] = {
	/* >= 2.3.0 */
	{ "TPE1", ID3_ARTIST },
	{ "TALB", ID3_ALBUM },
	{ "TIT2", ID3_TITLE },
	{ "TYER", ID3_DATE },
	{ "TCON", ID3_GENRE },
	{ "TPOS", ID3_DISC },
	{ "TRCK", ID3_TRACK },

	/* obsolete frames (2.2.0) */
	{ "TP1",  ID3_ARTIST },
	{ "TAL",  ID3_ALBUM },
	{ "TT2",  ID3_TITLE },
	{ "TYE",  ID3_DATE },
	{ "TCO",  ID3_GENRE },
	{ "TPA",  ID3_DISC },
	{ "TRK",  ID3_TRACK },

	{ "", -1 }
};

static void v2_add_frame(ID3 *id3, struct v2_frame_header *fh, const char *buf)
{
	int i, encoding = *buf++, len = fh->size - 1;

	if (encoding > 3)
		return;

	for (i = 0; frame_tab[i].key != -1; i++) {
		enum id3_key key = frame_tab[i].key;
		char *in, *out;
		int rc;

		if (strncmp(fh->id, frame_tab[i].name, 4))
			continue;

		switch (encoding) {
		case 0x00: /* ISO-8859-1 */
			in = xstrndup(buf, len);
			rc = utf8_encode(in, default_charset, &out);
			free(in);
			if (rc)
				return;
			break;
		case 0x03: /* UTF-8 */
			in = xstrndup(buf, len);
			if (u_is_valid(in)) {
				out = in;
			} else {
				rc = utf8_encode(in, default_charset, &out);
				free(in);
				if (rc)
					return;
			}
			break;
		case 0x01: /* UTF-16 */
			out = utf16_to_utf8(buf, len);
			if (out == NULL)
				return;
			break;
		case 0x02: /* UTF-16BE */
			out = utf16be_to_utf8(buf, len);
			if (out == NULL)
				return;
			break;
		}
		if (key == ID3_TRACK || key == ID3_DISC)
			fix_track_or_disc(out);
		if (key == ID3_GENRE) {
			id3_debug("genre before: '%s'\n", out);
			out = parse_genre(out);
		}
		free(id3->v2[key]);
		id3->v2[key] = out;
		id3->has_v2 = 1;
		id3_debug("%s '%s'\n", frame_tab[i].name, out);
		break;
	}
}

static int v2_read(ID3 *id3, int fd, const struct v2_header *header)
{
	char *buf;
	int rc, buf_size;
	int frame_start, i;
	int frame_header_size;

	buf_size = header->size;
	buf = xnew(char, buf_size);
	rc = read_all(fd, buf, buf_size);
	if (rc) {
		free(buf);
		return rc;
	}

	frame_start = 0;
	if (header->flags & V2_HEADER_EXTENDED) {
		struct v2_extended_header ext;

		v2_extended_header_parse(&ext, buf);
		if (ext.size > buf_size) {
			id3_debug("extended header corrupted\n");
			free(buf);
			return -2;
		}
		frame_start = ext.size;
		/* should check if update flag is set */
	}

	frame_header_size = 10;
	if (header->ver_major == 2)
		frame_header_size = 6;

	i = frame_start;
	while (i < buf_size - frame_header_size) {
		struct v2_frame_header fh;

		if (header->ver_major == 2) {
			if (!v2_2_0_frame_header_parse(&fh, buf + i))
				break;
		} else if (!v2_3_0_frame_header_parse(&fh, buf + i)) {
			break;
		}

		i += frame_header_size;
		if (fh.size > buf_size - i) {
			id3_debug("frame too big\n");
			break;
		}

		v2_add_frame(id3, &fh, buf + i);
		i += fh.size;
	}

	free(buf);
	return 0;
}

int id3_tag_size(const char *buf, int buf_size)
{
	struct v2_header header;

	if (buf_size < 10)
		return 0;
	if (v2_header_parse(&header, buf)) {
		if (header.flags & V2_HEADER_FOOTER) {
			/* header + data + footer */
			id3_debug("v2.%d.%d with footer\n", header.ver_major, header.ver_minor);
			return 10 + header.size + 10;
		}
		/* header */
		id3_debug("v2.%d.%d\n", header.ver_major, header.ver_minor);
		return 10 + header.size;
	}
	if (buf_size >= 3 && is_v1(buf)) {
		id3_debug("v1\n");
		return 128;
	}
	return 0;
}

ID3 *id3_new(void)
{
	ID3 *id3 = xnew0(ID3, 1);

	if (default_charset == NULL) {
		if (!sconf_get_str_option("mad.charset", &default_charset))
			default_charset = "ISO-8859-1";
	}
	return id3;
}

void id3_free(ID3 *id3)
{
	int i;

	for (i = 0; i < NUM_ID3_KEYS; i++)
		free(id3->v2[i]);
	free(id3);
}

int id3_read_tags(ID3 *id3, int fd, unsigned int flags)
{
	off_t off;
	int rc;

	if (flags & ID3_V2) {
		struct v2_header header;
		char buf[138];

		rc = read_all(fd, buf, 10);
		if (rc)
			goto rc_error;
		if (v2_header_parse(&header, buf)) {
			rc = v2_read(id3, fd, &header);
			if (rc)
				goto rc_error;
			/* get v1 if needed */
		} else {
			/* get v2 from end and optionally v1 */

			off = lseek(fd, -138, SEEK_END);
			if (off == -1)
				goto error;
			rc = read_all(fd, buf, 138);
			if (rc)
				goto rc_error;

			if (is_v1(buf + 10)) {
				if (flags & ID3_V1) {
					memcpy(id3->v1, buf + 10, 128);
					id3->has_v1 = 1;
				}
				if (v2_footer_parse(&header, buf)) {
					/* footer at end of file - 128 */
					off = lseek(fd, -(header.size + 138), SEEK_END);
					if (off == -1)
						goto error;
					rc = v2_read(id3, fd, &header);
					if (rc)
						goto rc_error;
				}
			} else if (v2_footer_parse(&header, buf + 128)) {
				/* footer at end of file */
				off = lseek(fd, -(header.size + 10), SEEK_END);
				if (off == -1)
					goto error;
				rc = v2_read(id3, fd, &header);
				if (rc)
					goto rc_error;
			}
			return 0;
		}
	}
	if (flags & ID3_V1) {
		off = lseek(fd, -128, SEEK_END);
		if (off == -1)
			goto error;
		rc = read_all(fd, id3->v1, 128);
		if (rc)
			goto rc_error;
		id3->has_v1 = is_v1(id3->v1);
	}
	return 0;
error:
	rc = -1;
rc_error:
	return rc;
}

static char *v1_get_str(const char *buf, int len)
{
	char in[32];
	char *out;
	int i;

	for (i = len - 1; i >= 0; i--) {
		if (buf[i] != 0 && buf[i] != ' ')
			break;
	}
	if (i == -1)
		return NULL;
	memcpy(in, buf, i + 1);
	in[i + 1] = 0;
	if (u_is_valid(in))
		return xstrdup(in);
	if (utf8_encode(in, default_charset, &out))
		return NULL;
	return out;
}

char *id3_get_comment(ID3 *id3, enum id3_key key)
{
	if (id3->has_v2) {
		if (id3->v2[key])
			return xstrdup(id3->v2[key]);
	}
	if (id3->has_v1) {
		switch (key) {
		case ID3_ARTIST:
			return v1_get_str(id3->v1 + 33, 30);
		case ID3_ALBUM:
			return v1_get_str(id3->v1 + 63, 30);
		case ID3_TITLE:
			return v1_get_str(id3->v1 + 3, 30);
		case ID3_DATE:
			return v1_get_str(id3->v1 + 93, 4);
		case ID3_GENRE:
			{
				unsigned char idx = id3->v1[127];

				if (idx >= NR_GENRES)
					return NULL;
				return xstrdup(genres[idx]);
			}
		case ID3_DISC:
			return NULL;
		case ID3_TRACK:
			{
				char *t;

				if (id3->v1[125] != 0)
					return NULL;
				t = xnew(char, 4);
				snprintf(t, 4, "%d", ((unsigned char *)id3->v1)[126]);
				return t;
			}
		}
	}
	return NULL;
}
