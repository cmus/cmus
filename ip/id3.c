/*
 * Copyright 2005 Timo Hirvonen
 */

#include <id3.h>
#include <xmalloc.h>
#include <utf8_encode.h>
#include <uchar.h>
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

char *id3_v1_charset = "ISO-8859-1";

#define NR_GENRES 148
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

/* #define id3_debug(...) d_print(__VA_ARGS__) */
#define id3_debug(...) do { } while (0)

static char *utf16_to_utf8(const char *buf, int buf_size)
{
	char *out;
	int i, idx;

	out = xnew(char, (buf_size / 2) * 4);
	i = 0;
	idx = 0;
	while (buf_size - i >= 2) {
		uchar u;
		
		u = buf[i] + (buf[i + 1] << 8);
		if (u_set_char(out, &idx, u)) {
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

static char *utf16be_to_utf8(const char *buf, int buf_size)
{
	char *out;
	int i, idx;

	out = xnew(char, (buf_size / 2) * 4);
	i = 0;
	idx = 0;
	while (buf_size - i >= 2) {
		uchar u;
		
		u = buf[i + 1] + (buf[i] << 8);
		if (u_set_char(out, &idx, u)) {
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

static uint32_t u32_unsync(const unsigned char *buf)
{
	uint32_t b0, b1, b2, b3;

	b3 = buf[0];
	b2 = buf[1];
	b1 = buf[2];
	b0 = buf[3];
	return (b3 << 21) | (b2 << 14) | (b1 << 7) | b0;
}

static int v2_header_footer_parse(struct v2_header *header, const char *buf)
{
	header->ver_major = buf[3];
	header->ver_minor = buf[4];
	header->flags = buf[5];
	header->size = u32_unsync((const unsigned char *)(buf + 6));
	return 1;
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
	header->size = u32_unsync((const unsigned char *)buf);
	return 1;
}

static int is_frame_id_char(char ch)
{
	return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

static int v2_frame_header_parse(struct v2_frame_header *header, const char *buf)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (!is_frame_id_char(buf[i]))
			return 0;
		header->id[i] = buf[i];
	}
	header->size = u32_unsync((const unsigned char *)(buf + 4));
	header->flags = (buf[8] << 8) | buf[9];
	if (header->size == 0)
		return 0;
/* 	id3_debug("%c%c%c%c %d\n", header->id[0], header->id[1], header->id[2], header->id[3], header->size); */
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

/*
 * Frames:
 *
 * TALB  album
 * TIT2  title
 * TPE1  artist
 * TYER  year
 * TCON  genre
 * TRCK  tracknumber  1 or 1/10
 * TPOS  discnumber   1 or 1/2
 *
 * http://www.id3.org/id3v2.4.0-structure.txt
 */
static int v2_add_frame(ID3 *id3, struct v2_frame_header *fh, const char *buf)
{
	static const char * const frames[NUM_ID3_KEYS] = { "TPE1", "TALB", "TIT2", "TYER", "TCON", "DISC", "TRCK" };
	int i;

	if (buf[0] > 3)
		return 0;

	for (i = 0; i < NUM_ID3_KEYS; i++) {
		if (strncmp(fh->id, frames[i], 4) == 0) {
			char *in, *out;
			int rc;

			switch (buf[0]) {
			case 0x00: /* ISO-8859-1 */
			case 0x03: /* UTF-8 */
				in = xstrndup(buf + 1, fh->size - 1);
				if (u_is_valid(in)) {
					out = in;
				} else {
					rc = utf8_encode(in, "ISO-8859-1", &out);
					free(in);
					if (rc)
						return 0;
				}
				break;
			case 0x01: /* UTF-16 */
				out = utf16_to_utf8(buf + 1, fh->size - 1);
				if (out == NULL)
					return 0;
				break;
			case 0x02: /* UTF-16BE */
				out = utf16be_to_utf8(buf + 1, fh->size - 1);
				if (out == NULL)
					return 0;
				break;
			}
			if (i > 4) {
				char *slash = strchr(out, '/');

				if (slash)
					*slash = 0;
			}
			free(id3->v2[i]);
			id3->v2[i] = out;
			id3_debug("%s '%s'\n", frames[i], out);
			return 1;
		}
	}
	return 0;
}

static int v2_read(ID3 *id3, int fd, const struct v2_header *header)
{
	char *buf;
	int rc, buf_size;
	int frame_start, i;

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

	i = frame_start;
	while (i < buf_size - 10) {
		struct v2_frame_header fh;

		if (!v2_frame_header_parse(&fh, buf + i)) {
			break;
		}
		i += 10;
		if (fh.size > buf_size - i) {
			id3_debug("frame too big\n");
			break;
		}

		if (v2_add_frame(id3, &fh, buf + i))
			id3->has_v2 = 1;
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
			d_print("v2 with footer\n");
			return 10 + header.size + 10;
		}
		/* header */
		d_print("v2\n");
		return 10 + header.size;
	}
	if (buf_size >= 3 && is_v1(buf)) {
		d_print("v1\n");
		return 128;
	}
	return 0;
}

ID3 *id3_new(void)
{
	ID3 *id3 = xnew0(ID3, 1);
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
	if (utf8_encode(in, id3_v1_charset, &out))
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
