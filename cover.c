/*
 * Album covers for the cover view: finding the picture (beside the track
 * or in its tags), decoding it, scaling it, and encoding it as sixel
 * graphics or coloured half blocks. Nothing here touches curses or the
 * player, so tests/cover.c can link this file on its own.
 *
 * Same GPL terms as the rest of cmus. stb_image.h (public domain / MIT,
 * by Sean Barrett and contributors) decodes JPEG, PNG, BMP and GIF.
 *
 * Input is untrusted (files and tags from anywhere), so every size read
 * from a file is checked against what is left of it, and pictures are
 * limited to COVER_MAX_BYTES.
 */
#include "cover.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 16384
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* largest picture file or embedded picture read */
#define COVER_MAX_BYTES (32 * 1024 * 1024)
/* largest tag read in one piece (ID3v2, Ogg comments) */
#define TAG_MAX_BYTES (64 * 1024 * 1024)

/* ---- reading files ---------------------------------------------------- */

/* The whole of a regular file of at most max bytes; NULL otherwise. */
static unsigned char *read_file(const char *path, size_t max, size_t *size)
{
	struct stat st;
	unsigned char *data = NULL;
	size_t used = 0;
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (size_t)st.st_size > max)
		goto out;
	data = malloc(st.st_size);
	if (!data)
		goto out;
	while (used < (size_t)st.st_size) {
		ssize_t n = read(fd, data + used, st.st_size - used);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break;
		used += n;
	}
	if (used != (size_t)st.st_size) {
		free(data);
		data = NULL;
	}
	*size = used;
out:
	close(fd);
	return data;
}

/* exactly n bytes at the current position, or -1 */
static int read_exact(FILE *file, void *buf, size_t n)
{
	return fread(buf, 1, n, file) == n ? 0 : -1;
}

static uint32_t be32(const unsigned char *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t le32(const unsigned char *p)
{
	return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static uint32_t syncsafe32(const unsigned char *p)
{
	return (uint32_t)(p[0] & 0x7f) << 21 | (uint32_t)(p[1] & 0x7f) << 14 |
		(uint32_t)(p[2] & 0x7f) << 7 | (p[3] & 0x7f);
}

static unsigned char *copy_bytes(const unsigned char *p, size_t n, size_t *size)
{
	unsigned char *copy;

	if (!n || n > COVER_MAX_BYTES || !(copy = malloc(n)))
		return NULL;
	memcpy(copy, p, n);
	*size = n;
	return copy;
}

/* ---- picture blocks ---------------------------------------------------- */

/*
 * A FLAC PICTURE block (also what Ogg's METADATA_BLOCK_PICTURE holds):
 * type, MIME type, description, size and colour info, then the picture.
 * Returns a copy of the picture and its picture type in *type.
 */
static unsigned char *flac_picture(const unsigned char *p, size_t n, size_t *size, uint32_t *type)
{
	const unsigned char *end = p + n;
	uint32_t len;

	if (n < 32)
		return NULL;
	*type = be32(p);
	p += 4;
	len = be32(p);                          /* MIME type */
	p += 4;
	if (len > (size_t)(end - p))
		return NULL;
	p += len;
	if (end - p < 4)
		return NULL;
	len = be32(p);                          /* description */
	p += 4;
	if (len > (size_t)(end - p))
		return NULL;
	p += len;
	if (end - p < 20)                       /* width, height, depth, colours, size */
		return NULL;
	p += 16;
	len = be32(p);
	p += 4;
	if (len > (size_t)(end - p))
		return NULL;
	return copy_bytes(p, len, size);
}

/*
 * Keep the better of two pictures: the front cover (type 3) wins, else
 * the first one found. Frees whichever is dropped.
 */
static void keep_best(unsigned char **best, size_t *best_size, uint32_t *best_type,
		unsigned char *pic, size_t pic_size, uint32_t type)
{
	if (!pic)
		return;
	if (!*best || (type == 3 && *best_type != 3)) {
		free(*best);
		*best = pic;
		*best_size = pic_size;
		*best_type = type;
	} else {
		free(pic);
	}
}

static int base64_value(int c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}

/* Decode base64 (whitespace ignored, stops at '='); NULL if malformed. */
static unsigned char *base64_decode(const char *s, size_t n, size_t *size)
{
	unsigned char *out = malloc(n / 4 * 3 + 3);
	uint32_t acc = 0;
	int bits = 0;
	size_t used = 0;

	if (!out)
		return NULL;
	for (size_t i = 0; i < n && s[i] != '='; i++) {
		int v = base64_value((unsigned char)s[i]);
		if (v < 0) {
			if (isspace((unsigned char)s[i]))
				continue;
			free(out);
			return NULL;
		}
		acc = acc << 6 | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[used++] = acc >> bits & 0xff;
		}
	}
	*size = used;
	return out;
}

/* ---- ID3v2 (MP3 and others) -------------------------------------------- */

/* undo "unsynchronisation": every 0xff 0x00 was 0xff */
static size_t unsync(unsigned char *p, size_t n)
{
	size_t out = 0;

	for (size_t i = 0; i < n; i++) {
		p[out++] = p[i];
		if (p[i] == 0xff && i + 1 < n && p[i + 1] == 0)
			i++;
	}
	return out;
}

/* the end of a text in the given ID3 encoding, past its terminator */
static const unsigned char *skip_text(const unsigned char *p, const unsigned char *end, int encoding)
{
	if (encoding == 1 || encoding == 2) {   /* UTF-16: two zero bytes, aligned */
		while (end - p >= 2 && (p[0] || p[1]))
			p += 2;
		return end - p >= 2 ? p + 2 : end;
	}
	while (p < end && *p)
		p++;
	return p < end ? p + 1 : end;
}

/*
 * APIC (v2.3, v2.4): encoding, MIME type, picture type, description, data.
 * PIC (v2.2): encoding, 3 letter format, picture type, description, data.
 */
static unsigned char *apic(const unsigned char *p, size_t n, int v22, size_t *size, uint32_t *type)
{
	const unsigned char *end = p + n;
	int encoding;

	if (n < 4)
		return NULL;
	encoding = *p++;
	if (v22)
		p += 3;
	else
		p = skip_text(p, end, 0);
	if (p >= end)
		return NULL;
	*type = *p++;
	p = skip_text(p, end, encoding);
	return p < end ? copy_bytes(p, end - p, size) : NULL;
}

static unsigned char *id3_picture(FILE *file, size_t *size)
{
	unsigned char header[10], *tag, *p, *end, *best = NULL;
	size_t tag_size, best_size = 0;
	uint32_t best_type = 0;
	int version;

	if (read_exact(file, header, 10) || memcmp(header, "ID3", 3))
		return NULL;
	version = header[3];
	if (version < 2 || version > 4)
		return NULL;
	tag_size = syncsafe32(header + 6);
	if (!tag_size || tag_size > TAG_MAX_BYTES || !(tag = malloc(tag_size)))
		return NULL;
	if (read_exact(file, tag, tag_size)) {
		free(tag);
		return NULL;
	}
	/* the whole tag is unsynchronised (v2.2, v2.3; v2.4 marks frames instead) */
	if (header[5] & 0x80 && version < 4)
		tag_size = unsync(tag, tag_size);
	p = tag;
	end = tag + tag_size;

	/* extended header (v2.3: size excludes itself, v2.4: syncsafe, includes) */
	if (header[5] & 0x40 && version >= 3 && end - p >= 4) {
		size_t ext = version == 4 ? syncsafe32(p) : be32(p) + 4;
		if (ext > (size_t)(end - p)) {
			free(tag);
			return NULL;
		}
		p += ext;
	}

	while (end - p >= (version == 2 ? 6 : 10)) {
		size_t frame_size;
		int header_size = version == 2 ? 6 : 10;
		int is_picture, flags = 0;

		if (!p[0])                              /* padding */
			break;
		if (version == 2) {
			frame_size = (size_t)p[3] << 16 | p[4] << 8 | p[5];
			is_picture = !memcmp(p, "PIC", 3);
		} else {
			frame_size = version == 4 ? syncsafe32(p + 4) : be32(p + 4);
			flags = p[9];
			is_picture = !memcmp(p, "APIC", 4);
		}
		p += header_size;
		if (frame_size > (size_t)(end - p))
			break;
		/* compressed or encrypted frames are skipped */
		if (is_picture && !(flags & (version == 4 ? 0x0c : 0xc0))) {
			unsigned char *data = p;
			size_t data_size = frame_size;
			unsigned char *copy = NULL;
			uint32_t type = 0;
			size_t pic_size = 0;

			/* v2.4: a data length indicator comes first, and the frame may be unsynchronised */
			if (version == 4 && flags & 0x01 && data_size >= 4) {
				data += 4;
				data_size -= 4;
			}
			if (version == 4 && flags & 0x02) {
				copy = malloc(data_size);
				if (copy) {
					memcpy(copy, data, data_size);
					data_size = unsync(copy, data_size);
					data = copy;
				}
			}
			unsigned char *pic = apic(data, data_size, version == 2, &pic_size, &type);
			keep_best(&best, &best_size, &best_type, pic, pic_size, type);
			free(copy);
			if (best && best_type == 3)
				break;
		}
		p += frame_size;
	}
	free(tag);
	if (best)
		*size = best_size;
	return best;
}

/* ---- FLAC -------------------------------------------------------------- */

static unsigned char *vorbis_comment_picture(const unsigned char *p, size_t n, int little,
		size_t *size);

static unsigned char *flac_file_picture(FILE *file, size_t *size)
{
	unsigned char magic[4], block[4], *best = NULL;
	size_t best_size = 0;
	uint32_t best_type = 0;
	int last = 0;

	if (read_exact(file, magic, 4) || memcmp(magic, "fLaC", 4))
		return NULL;
	while (!last && !read_exact(file, block, 4)) {
		int type = block[0] & 0x7f;
		size_t len = (size_t)block[1] << 16 | block[2] << 8 | block[3];

		last = block[0] & 0x80;
		if (type == 6 || type == 4) {
			unsigned char *data = len <= TAG_MAX_BYTES ? malloc(len ? len : 1) : NULL;
			size_t pic_size = 0;
			uint32_t pic_type = 0;

			if (!data || read_exact(file, data, len)) {
				free(data);
				break;
			}
			if (type == 6) {
				unsigned char *pic = flac_picture(data, len, &pic_size, &pic_type);
				keep_best(&best, &best_size, &best_type, pic, pic_size, pic_type);
			} else if (!best) {
				/* rare: a METADATA_BLOCK_PICTURE in the Vorbis comments */
				unsigned char *pic = vorbis_comment_picture(data, len, 1, &pic_size);
				keep_best(&best, &best_size, &best_type, pic, pic_size, 0);
			}
			free(data);
			if (best && best_type == 3)
				break;
		} else if (fseek(file, len, SEEK_CUR)) {
			break;
		}
	}
	if (best)
		*size = best_size;
	return best;
}

/* ---- Ogg (Vorbis, Opus) ------------------------------------------------- */

/*
 * The comments of a Vorbis/Opus comment header (after its magic): vendor
 * string, then key=value pairs, all with little endian lengths. Looks for
 * METADATA_BLOCK_PICTURE (a base64 FLAC picture block) and the older
 * COVERART (a base64 image file).
 */
static unsigned char *vorbis_comment_picture(const unsigned char *p, size_t n, int little,
		size_t *size)
{
	const unsigned char *end = p + n;
	unsigned char *best = NULL;
	size_t best_size = 0;
	uint32_t best_type = 0, count, len;

	(void)little;
	if (n < 8)
		return NULL;
	len = le32(p);                          /* vendor */
	p += 4;
	if (len > (size_t)(end - p) || end - p - len < 4)
		return NULL;
	p += len;
	count = le32(p);
	p += 4;
	while (count-- && end - p >= 4) {
		len = le32(p);
		p += 4;
		if (len > (size_t)(end - p))
			break;
		if (len > 23 && !strncasecmp((const char *)p, "METADATA_BLOCK_PICTURE=", 23)) {
			size_t block_size, pic_size = 0;
			uint32_t type = 0;
			unsigned char *block = base64_decode((const char *)p + 23, len - 23, &block_size);
			if (block) {
				unsigned char *pic = flac_picture(block, block_size, &pic_size, &type);
				keep_best(&best, &best_size, &best_type, pic, pic_size, type);
			}
			free(block);
		} else if (len > 9 && !best && !strncasecmp((const char *)p, "COVERART=", 9)) {
			size_t pic_size;
			unsigned char *pic = base64_decode((const char *)p + 9, len - 9, &pic_size);
			if (pic && pic_size)
				keep_best(&best, &best_size, &best_type, pic, pic_size, 0);
			else
				free(pic);
		}
		p += len;
		if (best && best_type == 3)
			break;
	}
	if (best)
		*size = best_size;
	return best;
}

/*
 * The second packet of the first stream (the comment header), put back
 * together from the Ogg pages it spans.
 */
static unsigned char *ogg_comment_packet(FILE *file, size_t *size)
{
	unsigned char header[27], lacing[255];
	unsigned char *packet = NULL;
	size_t used = 0, capacity = 0;
	int index = 0;                  /* the packet being read: 0, then 1 */
	uint32_t serial = 0;
	int first = 1;

	while (!read_exact(file, header, 27) && !memcmp(header, "OggS", 4)) {
		int segments = header[26];
		uint32_t page_serial = le32(header + 14);

		if (first) {
			serial = page_serial;
			first = 0;
		}
		if (read_exact(file, lacing, segments))
			break;
		if (page_serial != serial) {    /* another stream: skip its page */
			long skip = 0;
			for (int i = 0; i < segments; i++)
				skip += lacing[i];
			if (fseek(file, skip, SEEK_CUR))
				break;
			continue;
		}
		for (int i = 0; i < segments; i++) {
			size_t len = lacing[i];
			if (index == 1) {
				if (used + len > TAG_MAX_BYTES)
					goto fail;
				if (used + len > capacity) {
					unsigned char *bigger;
					capacity = (used + len) * 2;
					bigger = realloc(packet, capacity);
					if (!bigger)
						goto fail;
					packet = bigger;
				}
				if (read_exact(file, packet + used, len))
					goto fail;
				used += len;
			} else if (len && fseek(file, len, SEEK_CUR)) {
				goto fail;
			}
			/* a segment shorter than 255 bytes ends the packet */
			if (len < 255) {
				if (index == 1) {
					*size = used;
					return packet;
				}
				index++;
			}
		}
	}
fail:
	free(packet);
	return NULL;
}

static unsigned char *ogg_picture(FILE *file, size_t *size)
{
	size_t n;
	unsigned char *packet = ogg_comment_packet(file, &n), *pic = NULL;

	if (!packet)
		return NULL;
	if (n > 7 && !memcmp(packet, "\x03vorbis", 7))
		pic = vorbis_comment_picture(packet + 7, n - 7, 1, size);
	else if (n > 8 && !memcmp(packet, "OpusTags", 8))
		pic = vorbis_comment_picture(packet + 8, n - 8, 1, size);
	free(packet);
	return pic;
}

/* ---- MP4 (M4A, AAC, ALAC) ----------------------------------------------- */

/*
 * Find the child atom `name` among the atoms between start and end;
 * returns its data's position and size through *pos and *len.
 */
static int mp4_find(FILE *file, long start, long end, const char *name, long *pos, long *len)
{
	long at = start;

	while (end - at >= 8) {
		unsigned char h[16];
		uint64_t size;
		int header = 8;

		if (fseek(file, at, SEEK_SET) || read_exact(file, h, 8))
			return -1;
		size = be32(h);
		if (size == 1) {                        /* 64 bit size follows */
			if (read_exact(file, h + 8, 8))
				return -1;
			size = (uint64_t)be32(h + 8) << 32 | be32(h + 12);
			header = 16;
		} else if (size == 0) {                 /* to the end */
			size = end - at;
		}
		if (size < (uint64_t)header || size > (uint64_t)(end - at))
			return -1;
		if (!memcmp(h + 4, name, 4)) {
			*pos = at + header;
			*len = (long)size - header;
			return 0;
		}
		at += (long)size;
	}
	return -1;
}

static unsigned char *mp4_picture(FILE *file, size_t *size)
{
	unsigned char h[8], *data;
	long end, pos, len, mpos, mlen;
	static const char *path[] = { "moov", "udta", "meta", "ilst", "covr", "data" };

	if (read_exact(file, h, 8) || memcmp(h + 4, "ftyp", 4))
		return NULL;
	if (fseek(file, 0, SEEK_END) || (end = ftell(file)) < 0)
		return NULL;
	pos = 0;
	len = end;
	for (size_t i = 0; i < sizeof(path) / sizeof(path[0]); i++) {
		if (mp4_find(file, pos, pos + len, path[i], &mpos, &mlen))
			return NULL;
		pos = mpos;
		len = mlen;
		/* meta is a "full atom" (4 bytes of version and flags) in MP4 files,
		 * but not in QuickTime's own: tell them apart by the zero version */
		if (!strcmp(path[i], "meta") && len >= 4) {
			unsigned char v[4];
			if (fseek(file, pos, SEEK_SET) || read_exact(file, v, 4))
				return NULL;
			if (!v[0] && !v[1] && !v[2] && !v[3]) {
				pos += 4;
				len -= 4;
			}
		}
	}
	/* data: type (4 bytes) and locale (4 bytes), then the picture */
	if (len <= 8 || len - 8 > COVER_MAX_BYTES || fseek(file, pos + 8, SEEK_SET))
		return NULL;
	data = malloc(len - 8);
	if (!data || read_exact(file, data, len - 8)) {
		free(data);
		return NULL;
	}
	*size = len - 8;
	return data;
}

unsigned char *cover_embedded(const char *filename, size_t *size)
{
	unsigned char magic[8];
	unsigned char *pic = NULL;
	FILE *file = fopen(filename, "rb");

	if (!file)
		return NULL;
	if (!read_exact(file, magic, 8)) {
		rewind(file);
		if (!memcmp(magic, "ID3", 3))
			pic = id3_picture(file, size);
		else if (!memcmp(magic, "fLaC", 4))
			pic = flac_file_picture(file, size);
		else if (!memcmp(magic, "OggS", 4))
			pic = ogg_picture(file, size);
		else if (!memcmp(magic + 4, "ftyp", 4))
			pic = mp4_picture(file, size);
	}
	fclose(file);
	return pic;
}

/* ---- picture files beside the track -------------------------------------- */

static const char *const image_extensions[] = { "jpg", "jpeg", "png", "bmp", "gif" };
/* in order of preference */
static const char *const cover_names[] = { "cover", "folder", "front", "album", "albumart" };

static int is_image_name(const char *name, const char *stem)
{
	size_t len = strlen(stem);

	if (strncasecmp(name, stem, len) || name[len] != '.')
		return 0;
	for (size_t i = 0; i < sizeof(image_extensions) / sizeof(image_extensions[0]); i++)
		if (!strcasecmp(name + len + 1, image_extensions[i]))
			return 1;
	return 0;
}

unsigned char *cover_find(const char *filename, size_t *size, char **source)
{
	size_t len = strlen(filename);
	char *dir = malloc(len + 2), *stem, *slash, *dot, *path;
	unsigned char *pic = NULL;
	char *best = NULL;
	int best_rank = 1 << 30;
	DIR *d;
	struct dirent *e;

	if (!dir)
		return NULL;
	memcpy(dir, filename, len + 1);
	slash = strrchr(dir, '/');
	if (slash) {
		*slash = 0;
		stem = strdup(slash + 1);
	} else {
		strcpy(dir, ".");
		stem = strdup(filename);
	}
	if (!stem) {
		free(dir);
		return NULL;
	}
	dot = strrchr(stem, '.');
	if (dot && dot != stem)
		*dot = 0;

	/* "song.jpg" first (rank 0), then cover, folder... (rank 1, 2...) */
	d = opendir(dir);
	while (d && (e = readdir(d))) {
		int rank = -1;
		if (is_image_name(e->d_name, stem))
			rank = 0;
		for (size_t i = 0; rank < 0 && i < sizeof(cover_names) / sizeof(cover_names[0]); i++)
			if (is_image_name(e->d_name, cover_names[i]))
				rank = 1 + (int)i;
		/* the same rank: the first by name, so the choice is stable */
		if (rank >= 0 && (rank < best_rank || (rank == best_rank && strcmp(e->d_name, best) < 0))) {
			free(best);
			best = strdup(e->d_name);
			best_rank = rank;
		}
	}
	if (d)
		closedir(d);

	if (best && (path = malloc(strlen(dir) + strlen(best) + 2))) {
		sprintf(path, "%s/%s", dir, best);
		pic = read_file(path, COVER_MAX_BYTES, size);
		free(path);
		if (pic && source)
			*source = strdup(best);
	}
	free(best);
	free(stem);
	free(dir);

	if (!pic) {
		pic = cover_embedded(filename, size);
		if (pic && source)
			*source = strdup("embedded");
	}
	return pic;
}

/* ---- decoding and scaling ------------------------------------------------- */

void cover_image_free(struct cover_image *image)
{
	free(image->rgb);
	image->rgb = NULL;
}

/*
 * Scale to exactly w x h: every new pixel is the average of the source
 * pixels it covers (or the nearest one when enlarging).
 */
static unsigned char *scale(const unsigned char *src, int sw, int sh, int w, int h)
{
	unsigned char *dst = malloc((size_t)w * h * 3);

	if (!dst)
		return NULL;
	for (int y = 0; y < h; y++) {
		int y0 = (int)((int64_t)y * sh / h), y1 = (int)((int64_t)(y + 1) * sh / h);
		if (y1 <= y0)
			y1 = y0 + 1;
		for (int x = 0; x < w; x++) {
			int x0 = (int)((int64_t)x * sw / w), x1 = (int)((int64_t)(x + 1) * sw / w);
			unsigned long sum[3] = { 0, 0, 0 }, n = 0;
			if (x1 <= x0)
				x1 = x0 + 1;
			for (int sy = y0; sy < y1 && sy < sh; sy++) {
				const unsigned char *p = src + ((size_t)sy * sw + x0) * 3;
				for (int sx = x0; sx < x1 && sx < sw; sx++, p += 3) {
					sum[0] += p[0];
					sum[1] += p[1];
					sum[2] += p[2];
					n++;
				}
			}
			unsigned char *q = dst + ((size_t)y * w + x) * 3;
			for (int c = 0; c < 3; c++)
				q[c] = n ? (unsigned char)((sum[c] + n / 2) / n) : 0;
		}
	}
	return dst;
}

/* decode, and scale to exactly w x h if both are given */
static int decode_to(const unsigned char *data, size_t size, int w, int h, struct cover_image *image)
{
	int sw, sh, channels;
	unsigned char *rgb;

	if (size > INT32_MAX)
		return -1;
	rgb = stbi_load_from_memory(data, (int)size, &sw, &sh, &channels, 3);
	if (!rgb)
		return -1;
	if (w <= 0 || h <= 0) {
		image->width = sw;
		image->height = sh;
		image->rgb = rgb;
		return 0;
	}
	image->rgb = scale(rgb, sw, sh, w, h);
	stbi_image_free(rgb);
	if (!image->rgb)
		return -1;
	image->width = w;
	image->height = h;
	return 0;
}

/* the size of a w x h picture fitted into max_w x max_h, enlarged at most 2x */
static void fit(int w, int h, int max_w, int max_h, int *fw, int *fh)
{
	double s = (double)max_w / w;

	if ((double)max_h / h < s)
		s = (double)max_h / h;
	if (s > 2)
		s = 2;
	*fw = (int)(w * s + 0.5);
	*fh = (int)(h * s + 0.5);
	if (*fw < 1)
		*fw = 1;
	if (*fh < 1)
		*fh = 1;
}

int cover_decode(const unsigned char *data, size_t size, int max_width, int max_height,
		struct cover_image *image)
{
	int w, h, comp, fw, fh;

	if (size > INT32_MAX || !stbi_info_from_memory(data, (int)size, &w, &h, &comp))
		return -1;
	fit(w, h, max_width, max_height, &fw, &fh);
	return decode_to(data, size, fw, fh, image);
}

/* ---- sixel ---------------------------------------------------------------- */

struct growbuf {
	char *data;
	size_t len, cap;
	int failed;
};

static void put(struct growbuf *b, const char *s, size_t n)
{
	if (b->failed)
		return;
	if (b->len + n + 1 > b->cap) {
		size_t cap = (b->cap ? b->cap * 2 : 4096);
		char *bigger;
		while (cap < b->len + n + 1)
			cap *= 2;
		bigger = realloc(b->data, cap);
		if (!bigger) {
			b->failed = 1;
			return;
		}
		b->data = bigger;
		b->cap = cap;
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = 0;
}

static void putf(struct growbuf *b, const char *fmt, int a, int c, int d, int e)
{
	char tmp[64];
	int n = snprintf(tmp, sizeof(tmp), fmt, a, c, d, e);
	put(b, tmp, n);
}

/*
 * A palette for the picture by median cut: the colours (at 5 bits per
 * channel) are split into up to `max` boxes, each time the most
 * populated box along its longest side, at the median. lut maps every
 * 15 bit colour to its box; the palette gets each box's average colour.
 */
struct box {
	int lo[3], hi[3];
	unsigned long count;
};

#define BIN(r, g, b) (((r) >> 3) << 10 | ((g) >> 3) << 5 | ((b) >> 3))

static int median_cut(const struct cover_image *img, int max, unsigned char palette[][3],
		unsigned char *lut)
{
	/* how many pixels fall in each 15 bit colour, and the sum of their real values */
	unsigned long *hist = calloc(32768 * 4, sizeof(*hist)), *sums;
	struct box boxes[256];
	int n = 1;
	size_t pixels = (size_t)img->width * img->height;

	if (!hist)
		return 0;
	sums = hist + 32768;
	for (size_t i = 0; i < pixels; i++) {
		const unsigned char *p = img->rgb + i * 3;
		int bin = BIN(p[0], p[1], p[2]);
		hist[bin]++;
		for (int k = 0; k < 3; k++)
			sums[k * 32768 + bin] += p[k];
	}
	boxes[0] = (struct box){ { 0, 0, 0 }, { 31, 31, 31 }, pixels };

	while (n < max) {
		int pick = -1;
		unsigned long most = 0;
		/* the box with the most pixels that can still be split */
		for (int i = 0; i < n; i++) {
			int side = 0;
			for (int c = 0; c < 3; c++)
				if (boxes[i].hi[c] > boxes[i].lo[c])
					side = 1;
			if (side && boxes[i].count > most) {
				most = boxes[i].count;
				pick = i;
			}
		}
		if (pick < 0)
			break;
		struct box *bx = &boxes[pick];
		/* shrink it to the colours it really has, then split the longest side */
		int lo[3] = { 31, 31, 31 }, hi[3] = { 0, 0, 0 };
		unsigned long along[32] = { 0 };
		for (int r = bx->lo[0]; r <= bx->hi[0]; r++)
			for (int g = bx->lo[1]; g <= bx->hi[1]; g++)
				for (int b = bx->lo[2]; b <= bx->hi[2]; b++) {
					unsigned long c = hist[r << 10 | g << 5 | b];
					if (!c)
						continue;
					int v[3] = { r, g, b };
					for (int k = 0; k < 3; k++) {
						if (v[k] < lo[k])
							lo[k] = v[k];
						if (v[k] > hi[k])
							hi[k] = v[k];
					}
				}
		int axis = 0;
		for (int k = 1; k < 3; k++)
			if (hi[k] - lo[k] > hi[axis] - lo[axis])
				axis = k;
		memcpy(bx->lo, lo, sizeof(lo));
		memcpy(bx->hi, hi, sizeof(hi));
		if (hi[axis] == lo[axis]) {     /* one colour: done with this box */
			bx->count = 0;
			continue;
		}
		for (int r = lo[0]; r <= hi[0]; r++)
			for (int g = lo[1]; g <= hi[1]; g++)
				for (int b = lo[2]; b <= hi[2]; b++) {
					int v[3] = { r, g, b };
					along[v[axis]] += hist[r << 10 | g << 5 | b];
				}
		unsigned long half = 0, total = 0;
		for (int k = lo[axis]; k <= hi[axis]; k++)
			total += along[k];
		int cut = lo[axis];
		for (int k = lo[axis]; k < hi[axis]; k++) {
			half += along[k];
			cut = k;
			if (half * 2 >= total)
				break;
		}
		struct box other = *bx;
		bx->hi[axis] = cut;
		other.lo[axis] = cut + 1;
		bx->count = half;
		other.count = total - half;
		boxes[n++] = other;
	}

	/* every box's average colour, and the lookup table */
	for (int i = 0; i < n; i++) {
		double sum[3] = { 0, 0, 0 }, total = 0;
		for (int r = boxes[i].lo[0]; r <= boxes[i].hi[0]; r++)
			for (int g = boxes[i].lo[1]; g <= boxes[i].hi[1]; g++)
				for (int b = boxes[i].lo[2]; b <= boxes[i].hi[2]; b++) {
					int bin = r << 10 | g << 5 | b;
					lut[bin] = (unsigned char)i;
					for (int k = 0; k < 3; k++)
						sum[k] += sums[k * 32768 + bin];
					total += hist[bin];
				}
		for (int k = 0; k < 3; k++)
			palette[i][k] = total ? (unsigned char)(sum[k] / total + 0.5) :
				(unsigned char)((boxes[i].lo[k] + boxes[i].hi[k]) * 4 + 4);
	}
	free(hist);
	return n;
}

char *cover_sixel(const struct cover_image *img, size_t *size)
{
	struct growbuf out = { 0 };
	unsigned char palette[256][3];
	unsigned char *lut = malloc(32768), *index = NULL;
	int colours;
	size_t pixels = (size_t)img->width * img->height;

	if (!lut)
		return NULL;
	colours = median_cut(img, 255, palette, lut);
	index = malloc(pixels ? pixels : 1);
	if (!colours || !index) {
		free(lut);
		free(index);
		return NULL;
	}
	/*
	 * Every 15 bit colour to its nearest palette colour (median_cut only
	 * filled in the colours the picture has; dithering reaches others),
	 * then Floyd-Steinberg dithering, so gradients don't turn into bands.
	 */
	for (int bin = 0; bin < 32768; bin++) {
		int r = (bin >> 10) * 8 + 4, g = (bin >> 5 & 31) * 8 + 4, b = (bin & 31) * 8 + 4;
		long best = -1;
		for (int i = 0; i < colours; i++) {
			long d = (long)(r - palette[i][0]) * (r - palette[i][0]) +
				(long)(g - palette[i][1]) * (g - palette[i][1]) +
				(long)(b - palette[i][2]) * (b - palette[i][2]);
			if (best < 0 || d < best) {
				best = d;
				lut[bin] = (unsigned char)i;
			}
		}
	}
	int *err = calloc((size_t)(img->width + 2) * 2 * 3, sizeof(int));
	if (!err) {
		free(lut);
		free(index);
		return NULL;
	}
	for (int y = 0; y < img->height; y++) {
		/* this row's and the next row's error, in 1/16 */
		int *cur = err + (y & 1) * (img->width + 2) * 3, *next = err + (~y & 1) * (img->width + 2) * 3;
		memset(next, 0, (size_t)(img->width + 2) * 3 * sizeof(int));
		for (int x = 0; x < img->width; x++) {
			const unsigned char *p = img->rgb + ((size_t)y * img->width + x) * 3;
			int v[3];
			for (int k = 0; k < 3; k++) {
				v[k] = p[k] + cur[(x + 1) * 3 + k] / 16;
				v[k] = v[k] < 0 ? 0 : v[k] > 255 ? 255 : v[k];
			}
			int i = lut[BIN(v[0], v[1], v[2])];
			index[(size_t)y * img->width + x] = (unsigned char)i;
			for (int k = 0; k < 3; k++) {
				int e = v[k] - palette[i][k];
				cur[(x + 2) * 3 + k] += e * 7;
				next[x * 3 + k] += e * 3;
				next[(x + 1) * 3 + k] += e * 5;
				next[(x + 2) * 3 + k] += e;
			}
		}
	}
	free(err);
	free(lut);

	/* P2 = 1: pixels not drawn stay as they are; 1:1 pixels, and the size */
	put(&out, "\033P0;1;0q", 8);
	putf(&out, "\"1;1;%d;%d", img->width, img->height, 0, 0);
	for (int i = 0; i < colours; i++)
		putf(&out, "#%d;2;%d;%d;%d", i, (palette[i][0] * 100 + 127) / 255,
				(palette[i][1] * 100 + 127) / 255, (palette[i][2] * 100 + 127) / 255);

	unsigned char *bits = malloc(img->width);
	char used[256];
	if (!bits) {
		free(index);
		free(out.data);
		return NULL;
	}
	for (int band = 0; band < img->height; band += 6) {
		int rows = img->height - band < 6 ? img->height - band : 6, first = 1;
		memset(used, 0, sizeof(used));
		for (int y = 0; y < rows; y++)
			for (int x = 0; x < img->width; x++)
				used[index[(size_t)(band + y) * img->width + x]] = 1;
		for (int c = 0; c < colours; c++) {
			if (!used[c])
				continue;
			memset(bits, 0, img->width);
			for (int y = 0; y < rows; y++)
				for (int x = 0; x < img->width; x++)
					if (index[(size_t)(band + y) * img->width + x] == c)
						bits[x] |= 1 << y;
			if (!first)
				put(&out, "$", 1);
			first = 0;
			putf(&out, "#%d", c, 0, 0, 0);
			/* run-length: !count char for runs of more than three */
			for (int x = 0; x < img->width;) {
				int run = 1;
				while (x + run < img->width && bits[x + run] == bits[x])
					run++;
				char ch = (char)('?' + bits[x]);
				if (run > 3) {
					putf(&out, "!%d", run, 0, 0, 0);
					put(&out, &ch, 1);
				} else {
					for (int k = 0; k < run; k++)
						put(&out, &ch, 1);
				}
				x += run;
			}
		}
		put(&out, "-", 1);
	}
	put(&out, "\033\\", 2);
	free(bits);
	free(index);
	if (out.failed) {
		free(out.data);
		return NULL;
	}
	*size = out.len;
	return out.data;
}

/* ---- blocks --------------------------------------------------------------- */

/* the nearest of xterm's 256 colours: the 6x6x6 cube or the grey ramp */
static int xterm256(const unsigned char *p)
{
	static const int levels[6] = { 0, 95, 135, 175, 215, 255 };
	int cube[3], best, grey, gi;
	long d_cube = 0, d_grey = 0;

	for (int c = 0; c < 3; c++) {
		int k = p[c] < 48 ? 0 : p[c] < 115 ? 1 : (p[c] - 35) / 40;
		cube[c] = k;
		d_cube += (long)(p[c] - levels[k]) * (p[c] - levels[k]);
	}
	best = 16 + cube[0] * 36 + cube[1] * 6 + cube[2];
	gi = ((p[0] + p[1] + p[2]) / 3 - 3) / 10;
	if (gi < 0)
		gi = 0;
	if (gi > 23)
		gi = 23;
	grey = 8 + gi * 10;
	for (int c = 0; c < 3; c++)
		d_grey += (long)(p[c] - grey) * (p[c] - grey);
	return d_grey < d_cube ? 232 + gi : best;
}

static void colour(struct growbuf *out, int background, const unsigned char *p, int truecolor)
{
	if (truecolor)
		putf(out, background ? "\033[48;2;%d;%d;%dm" : "\033[38;2;%d;%d;%dm", p[0], p[1], p[2], 0);
	else
		putf(out, background ? "\033[48;5;%dm" : "\033[38;5;%dm", xterm256(p), 0, 0, 0);
}

char *cover_blocks(const struct cover_image *img, enum cover_mode mode, size_t *size)
{
	struct growbuf out = { 0 };
	int spaces = mode == COVER_SPACES_256, truecolor = mode == COVER_BLOCKS_TRUECOLOR;
	int step = spaces ? 1 : 2;

	for (int y = 0; y < img->height; y += step) {
		const unsigned char *last_top = NULL, *last_bottom = NULL;
		if (y)
			putf(&out, "\033[0m\033[1B\033[%dD", img->width, 0, 0, 0);
		for (int x = 0; x < img->width; x++) {
			const unsigned char *top = img->rgb + ((size_t)y * img->width + x) * 3;
			const unsigned char *bottom = y + 1 < img->height ? top + (size_t)img->width * 3 : top;
			if (spaces) {
				if (!last_bottom || memcmp(top, last_bottom, 3))
					colour(&out, 1, top, 0);
				last_bottom = top;
				put(&out, " ", 1);
				continue;
			}
			if (!last_top || memcmp(top, last_top, 3))
				colour(&out, 0, top, truecolor);
			if (!last_bottom || memcmp(bottom, last_bottom, 3))
				colour(&out, 1, bottom, truecolor);
			last_top = top;
			last_bottom = bottom;
			put(&out, "\xe2\x96\x80", 3);           /* ▀ upper half block */
		}
	}
	put(&out, "\033[0m", 4);
	if (out.failed) {
		free(out.data);
		return NULL;
	}
	*size = out.len;
	return out.data;
}

/* ---- all together ----------------------------------------------------------- */

void cover_free(struct cover *cover)
{
	if (!cover)
		return;
	free(cover->data);
	free(cover->source);
	free(cover);
}

struct cover *cover_load(const char *filename, enum cover_mode mode, int cols, int rows,
		int cell_width, int cell_height)
{
	struct cover *cover = calloc(1, sizeof(*cover));
	struct cover_image image = { 0, 0, NULL };
	unsigned char *bytes;
	size_t size = 0;
	int w, h, comp, fw, fh, pw, ph, max_h;

	if (!cover)
		return NULL;
	if (cols < 1 || rows < 1 || cell_width < 1 || cell_height < 1) {
		cover->error = EINVAL;
		return cover;
	}
	bytes = cover_find(filename, &size, &cover->source);
	if (!bytes) {
		cover->error = ENOENT;
		return cover;
	}
	if (size > INT32_MAX || !stbi_info_from_memory(bytes, (int)size, &w, &h, &comp)) {
		free(bytes);
		cover->error = EINVAL;
		return cover;
	}
	cover->width = w;
	cover->height = h;

	/*
	 * Fit it into the cells, in screen pixels. Sixel is drawn in bands of
	 * six pixel rows, and tmux gives the picture as many lines as its bands
	 * take: a height between two multiples of six would spill a line.
	 */
	max_h = rows * cell_height;
	if (mode == COVER_SIXEL && max_h >= 6)
		max_h -= max_h % 6;
	fit(w, h, cols * cell_width, max_h, &fw, &fh);
	if (mode == COVER_SIXEL) {
		pw = fw;
		ph = fh;
	} else {
		/* a pixel per cell across, two (or one) per cell down */
		int per_cell = mode == COVER_SPACES_256 ? 1 : 2;
		pw = (fw + cell_width / 2) / cell_width;
		ph = (fh * per_cell + cell_height / 2) / cell_height;
		if (pw < 1)
			pw = 1;
		if (ph < 1)
			ph = 1;
		if (pw > cols)
			pw = cols;
		if (ph > rows * per_cell)
			ph = rows * per_cell;
	}
	if (decode_to(bytes, size, pw, ph, &image)) {
		free(bytes);
		cover->error = EINVAL;
		return cover;
	}
	free(bytes);

	if (mode == COVER_SIXEL) {
		cover->data = cover_sixel(&image, &cover->size);
		cover->cols = (image.width + cell_width - 1) / cell_width;
		cover->rows = (image.height + cell_height - 1) / cell_height;
	} else {
		cover->data = cover_blocks(&image, mode, &cover->size);
		cover->cols = image.width;
		cover->rows = mode == COVER_SPACES_256 ? image.height : (image.height + 1) / 2;
	}
	cover_image_free(&image);
	if (!cover->data)
		cover->error = ENOMEM;
	return cover;
}
