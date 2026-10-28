/*
 * Tests for cover.c: "make check-cover". Builds tracks with covers in all
 * the places cover_find() looks, in a temporary directory, and checks
 * what it finds, decodes and encodes; then feeds it garbage.
 */
#include "cover.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 8x8 PNGs of one colour */
static const unsigned char red_png[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08,
	0x08, 0x02, 0x00, 0x00, 0x00, 0x4b, 0x6d, 0x29, 0xdc, 0x00, 0x00, 0x00,
	0x11, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0x80,
	0x15, 0x31, 0x0c, 0x2d, 0x09, 0x00, 0x28, 0xff, 0x3f, 0xc1, 0xce, 0x77,
	0xc8, 0x4f, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42,
	0x60, 0x82,
};
static const unsigned char blue_png[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08,
	0x08, 0x02, 0x00, 0x00, 0x00, 0x4b, 0x6d, 0x29, 0xdc, 0x00, 0x00, 0x00,
	0x10, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x60, 0xf8, 0x8f,
	0x03, 0x0d, 0x29, 0x09, 0x00, 0xa9, 0x70, 0x3f, 0xc1, 0xfc, 0xcb, 0x45,
	0xfc, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
	0x82,
};

static char dir[] = "/tmp/cmus-cover-XXXXXX";

static char *path(const char *name)
{
	static char buf[4096];
	snprintf(buf, sizeof(buf), "%s/%s", dir, name);
	return buf;
}

static void write_file(const char *name, const void *data, size_t size)
{
	FILE *f = fopen(path(name), "wb");
	assert(f);
	assert(fwrite(data, 1, size, f) == size);
	fclose(f);
}

struct buf {
	unsigned char data[1 << 16];
	size_t len;
};

static void add(struct buf *b, const void *p, size_t n)
{
	assert(b->len + n <= sizeof(b->data));
	memcpy(b->data + b->len, p, n);
	b->len += n;
}

static void add_be32(struct buf *b, uint32_t v)
{
	unsigned char p[4] = { v >> 24, v >> 16, v >> 8, v };
	add(b, p, 4);
}

static void add_le32(struct buf *b, uint32_t v)
{
	unsigned char p[4] = { v, v >> 8, v >> 16, v >> 24 };
	add(b, p, 4);
}

/* a FLAC PICTURE block's contents */
static void picture_block(struct buf *b, uint32_t type, const unsigned char *png, size_t n)
{
	add_be32(b, type);
	add_be32(b, 9);
	add(b, "image/png", 9);
	add_be32(b, 4);
	add(b, "desc", 4);
	add_be32(b, 8);
	add_be32(b, 8);
	add_be32(b, 24);
	add_be32(b, 0);
	add_be32(b, n);
	add(b, png, n);
}

/* the colour of the picture found for name: 'r', 'b', or 0 for none */
static int found(const char *name)
{
	size_t size = 0;
	char *source = NULL;
	unsigned char *pic = cover_find(path(name), &size, &source);
	struct cover_image img = { 0, 0, NULL };
	int colour = 0;

	if (!pic)
		return 0;
	assert(source);
	assert(!cover_decode(pic, size, 8, 8, &img));
	assert(img.width == 8 && img.height == 8);
	colour = img.rgb[0] > 200 ? 'r' : img.rgb[2] > 200 ? 'b' : '?';
	cover_image_free(&img);
	free(pic);
	free(source);
	return colour;
}

static void test_flac(void)
{
	struct buf b = { .len = 0 };
	struct buf pic = { .len = 0 };
	unsigned char header[4];
	unsigned char streaminfo[34] = { 0 };

	add(&b, "fLaC", 4);
	header[0] = 0;          /* STREAMINFO, not last */
	header[1] = 0; header[2] = 0; header[3] = 34;
	add(&b, header, 4);
	add(&b, streaminfo, 34);
	/* a back cover (type 4) first, then the front cover (type 3): front wins */
	picture_block(&pic, 4, red_png, sizeof(red_png));
	header[0] = 6;
	header[1] = pic.len >> 16; header[2] = pic.len >> 8; header[3] = pic.len;
	add(&b, header, 4);
	add(&b, pic.data, pic.len);
	pic.len = 0;
	picture_block(&pic, 3, blue_png, sizeof(blue_png));
	header[0] = 0x80 | 6;
	header[1] = pic.len >> 16; header[2] = pic.len >> 8; header[3] = pic.len;
	add(&b, header, 4);
	add(&b, pic.data, pic.len);
	write_file("track.flac", b.data, b.len);
	assert(found("track.flac") == 'b');
}

static void id3_frame(struct buf *b, int version, const char *id, const unsigned char *data, size_t n)
{
	add(b, id, 4);
	if (version == 4) {
		unsigned char s[4] = { n >> 21 & 0x7f, n >> 14 & 0x7f, n >> 7 & 0x7f, n & 0x7f };
		add(b, s, 4);
	} else {
		add_be32(b, n);
	}
	add(b, "\0\0", 2);
	add(b, data, n);
}

static void apic(struct buf *frame, int encoding, int type, const unsigned char *png, size_t n)
{
	unsigned char enc = encoding, t = type;
	add(frame, &enc, 1);
	add(frame, "image/png", 10);            /* with its NUL */
	add(frame, &t, 1);
	if (encoding == 1)
		add(frame, "\xff\xfe" "d\0" "\0\0", 6);   /* UTF-16 description "d" */
	else
		add(frame, "d", 2);
	add(frame, png, n);
}

static void test_id3(int version)
{
	struct buf frames = { .len = 0 }, frame = { .len = 0 }, file = { .len = 0 };
	unsigned char header[10] = { 'I', 'D', '3', version, 0, 0 };
	char name[32];

	apic(&frame, 0, 0, red_png, sizeof(red_png));          /* "other" */
	id3_frame(&frames, version, "APIC", frame.data, frame.len);
	frame.len = 0;
	apic(&frame, 1, 3, blue_png, sizeof(blue_png));        /* front cover, UTF-16 */
	id3_frame(&frames, version, "APIC", frame.data, frame.len);
	add(&frames, "\0\0\0\0\0\0\0\0\0\0", 10);              /* padding */
	header[6] = frames.len >> 21 & 0x7f;
	header[7] = frames.len >> 14 & 0x7f;
	header[8] = frames.len >> 7 & 0x7f;
	header[9] = frames.len & 0x7f;
	add(&file, header, 10);
	add(&file, frames.data, frames.len);
	add(&file, "\xff\xfb\x90\x00", 4);                      /* an MP3 frame header */
	snprintf(name, sizeof(name), "v2%d.mp3", version);
	write_file(name, file.data, file.len);
	assert(found(name) == 'b');
}

/* one Ogg page with the given segments (CRC left 0: cover.c doesn't check it) */
static void ogg_page(struct buf *b, const unsigned char *data, size_t n, int continued, int ends)
{
	unsigned char h[27] = { 'O', 'g', 'g', 'S', 0, continued ? 1 : 0 };
	unsigned char lacing[255];
	int segments = 0;
	size_t left = n;

	h[14] = 0x42;                           /* serial */
	while (left >= 255 && segments < 255) {
		lacing[segments++] = 255;
		left -= 255;
	}
	if (ends && segments < 255)
		lacing[segments++] = left;
	h[26] = segments;
	add(b, h, 27);
	add(b, lacing, segments);
	add(b, data, n);
}

static char *base64(const unsigned char *p, size_t n)
{
	static const char *abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char *out = malloc(n / 3 * 4 + 5), *o = out;
	for (size_t i = 0; i < n; i += 3) {
		uint32_t v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
		*o++ = abc[v >> 18 & 63];
		*o++ = abc[v >> 12 & 63];
		*o++ = i + 1 < n ? abc[v >> 6 & 63] : '=';
		*o++ = i + 2 < n ? abc[v & 63] : '=';
	}
	*o = 0;
	return out;
}

static void test_ogg(int opus)
{
	struct buf file = { .len = 0 }, comment = { .len = 0 }, block = { .len = 0 };
	const char *key = "METADATA_BLOCK_PICTURE=";
	char *b64;

	ogg_page(&file, (const unsigned char *)(opus ? "OpusHead" : "\x01vorbis"), 8, 0, 1);
	if (opus)
		add(&comment, "OpusTags", 8);
	else
		add(&comment, "\x03vorbis", 7);
	add_le32(&comment, 4);
	add(&comment, "test", 4);
	add_le32(&comment, 3);
	add_le32(&comment, 11);
	add(&comment, "TITLE=Song!", 11);
	/* a long comment, so the packet spans two pages */
	add_le32(&comment, 312);
	add(&comment, "DESCRIPTION=", 12);
	for (int i = 0; i < 300; i++)
		add(&comment, "x", 1);
	picture_block(&block, 3, red_png, sizeof(red_png));
	b64 = base64(block.data, block.len);
	add_le32(&comment, strlen(key) + strlen(b64));
	add(&comment, key, strlen(key));
	add(&comment, b64, strlen(b64));
	free(b64);
	/* split over two pages: one 255 byte segment, the rest on the next page */
	ogg_page(&file, comment.data, 255, 0, 0);
	ogg_page(&file, comment.data + 255, comment.len - 255, 1, 1);
	write_file(opus ? "track.opus" : "track.ogg", file.data, file.len);
	assert(found(opus ? "track.opus" : "track.ogg") == 'r');
}

static void atom(struct buf *b, const char *name, const struct buf *content)
{
	add_be32(b, 8 + content->len);
	add(b, name, 4);
	add(b, content->data, content->len);
}

static void test_mp4(void)
{
	static struct buf data, covr, ilst, meta, udta, moov, file, ftyp;
	data.len = covr.len = ilst.len = meta.len = udta.len = moov.len = file.len = ftyp.len = 0;
	add_be32(&data, 14);                    /* type: PNG */
	add_be32(&data, 0);                     /* locale */
	add(&data, blue_png, sizeof(blue_png));
	atom(&covr, "data", &data);
	atom(&ilst, "covr", &covr);
	add_be32(&meta, 0);                     /* version and flags */
	atom(&meta, "ilst", &ilst);
	atom(&udta, "meta", &meta);
	atom(&moov, "udta", &udta);
	add(&ftyp, "M4A \0\0\0\0", 8);
	atom(&file, "ftyp", &ftyp);
	atom(&file, "moov", &moov);
	write_file("track.m4a", file.data, file.len);
	assert(found("track.m4a") == 'b');
}

static void test_sidecars(void)
{
	/* cover.png beats the embedded picture; song.jpg beats cover.png */
	write_file("Cover.PNG", red_png, sizeof(red_png));
	assert(found("track.m4a") == 'r');
	write_file("track.png", blue_png, sizeof(blue_png));
	assert(found("track.m4a") == 'b');
	/* folder.jpg ranks below cover */
	write_file("folder.png", blue_png, sizeof(blue_png));
	unlink(path("track.png"));
	assert(found("track.m4a") == 'r');
	unlink(path("Cover.PNG"));
	unlink(path("folder.png"));
	/* none at all */
	write_file("plain.mp3", "\xff\xfb\x90\x00", 4);
	assert(found("plain.mp3") == 0);
}

static void test_encoding(void)
{
	struct cover_image img = { 0, 0, NULL };
	size_t size;
	char *s;

	assert(!cover_decode(red_png, sizeof(red_png), 100, 50, &img));
	/* fitted into 100x50, at most twice as big */
	assert(img.width == 16 && img.height == 16);
	s = cover_sixel(&img, &size);
	assert(s && size == strlen(s));
	assert(!strncmp(s, "\033P0;1;0q\"1;1;16;16", 18));
	assert(strstr(s, ";2;100;0;0"));         /* the red in the palette */
	assert(!strcmp(s + size - 2, "\033\\"));
	free(s);
	s = cover_blocks(&img, COVER_BLOCKS_TRUECOLOR, &size);
	assert(s && strstr(s, "38;2;255;0;0") && strstr(s, "\xe2\x96\x80"));
	free(s);
	s = cover_blocks(&img, COVER_BLOCKS_256, &size);
	assert(s && strstr(s, "38;5;196"));      /* xterm's red */
	free(s);
	cover_image_free(&img);

	/* the whole thing: 8x8 picture in 20x10 cells of 10x20 pixels, enlarged to 16x16 */
	struct cover *c = cover_load(path("track.flac"), COVER_SIXEL, 20, 10, 10, 20);
	assert(c && !c->error && c->cols == 2 && c->rows == 1 && !strcmp(c->source, "embedded"));
	cover_free(c);
	c = cover_load(path("track.flac"), COVER_BLOCKS_TRUECOLOR, 20, 10, 10, 20);
	/* 16x16 screen pixels: 2 cells wide, 16 / 20 * 2 = 2 pixels (1 cell) high */
	assert(c && !c->error && c->cols == 2 && c->rows == 1);
	cover_free(c);
	/* 2 lines of 7 pixels: 12 pixels high, two sixel bands, not 14 (three) */
	c = cover_load(path("track.flac"), COVER_SIXEL, 20, 2, 10, 7);
	assert(c && !c->error && c->rows == 2);
	assert(!strncmp(c->data, "\033P0;1;0q\"1;1;12;12", 18));
	cover_free(c);
	c = cover_load(path("plain.mp3"), COVER_SIXEL, 20, 10, 10, 20);
	assert(c && c->error == ENOENT);
	cover_free(c);
}

/* random bytes after each format's magic must not crash or leak */
static void test_garbage(void)
{
	static const char *magic[] = { "ID3\x03\0\0\0\0\x10\0", "ID3\x04\0\x40\0\0\x10\0", "fLaC",
		"OggS", "\0\0\0\x10" "ftyp" };
	unsigned char data[4096];
	srand(1);
	for (int round = 0; round < 2000; round++) {
		const char *m = magic[round % 5];
		size_t mlen = round % 5 == 0 || round % 5 == 1 ? 10 : round % 5 == 4 ? 8 : 4;
		size_t n = mlen + rand() % (sizeof(data) - mlen);
		size_t size;
		memcpy(data, m, mlen);
		for (size_t i = mlen; i < n; i++)
			data[i] = rand() % 4 ? rand() : (round & 1 ? 0xff : 0);
		write_file("garbage", data, n);
		unsigned char *pic = cover_embedded(path("garbage"), &size);
		free(pic);
		struct cover_image img = { 0, 0, NULL };
		if (!cover_decode(data, n, 10, 10, &img))
			cover_image_free(&img);
	}
}

int main(void)
{
	assert(mkdtemp(dir));
	test_flac();
	test_id3(3);
	test_id3(4);
	test_ogg(0);
	test_ogg(1);
	test_mp4();
	test_sidecars();
	test_encoding();
	test_garbage();
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
	if (system(cmd)) {
	}
	printf("cover: all tests passed\n");
	return 0;
}
