#include "lyrics.h"
#include "input.h"
#include "keyval.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* The input plugins are not linked into this test: no tags, so
 * lyrics_load_tag() falls back to reading ID3v2 frames itself. */
struct input_plugin *ip_new(const char *filename)
{
	return NULL;
}
int ip_open(struct input_plugin *ip)
{
	return -1;
}
void ip_delete(struct input_plugin *ip)
{
}
int ip_read_comments(struct input_plugin *ip, struct keyval **comments)
{
	return -1;
}
const char *keyvals_get_val(const struct keyval *keyvals, const char *key)
{
	return NULL;
}
void keyvals_free(struct keyval *keyvals)
{
}

/*
 * Write an MP3-like file holding an ID3v2 tag with one USLT frame, load it
 * with lyrics_load_tag() and check the result: count lines, the last one
 * being "last". count < 0 means "no lyrics expected".
 *
 * USLT_EXT adds an extended header before the frame, which the reader
 * must skip; its size is counted differently in v2.3 and v2.4.
 */
#define USLT(version, frame, count, last) \
	test_uslt(version, 0, frame, sizeof(frame) - 1, count, last)
#define USLT_EXT(version, frame, count, last) \
	test_uslt(version, 1, frame, sizeof(frame) - 1, count, last)

static void test_uslt(int version, int extended, const char *frame, size_t size, int count,
	const char *last)
{
	char path[] = "tests/uslt-XXXXXX.mp3";
	unsigned char header[10] = { 'I', 'D', '3', version, 0, extended ? 0x40 : 0, 0, 0, 0, 0 };
	unsigned char fh[10] = { 'U', 'S', 'L', 'T' };
	/* v2.4: size 6 (syncsafe, counting itself), 1 flag byte, flags 0 */
	static const unsigned char ext4[6] = { 0, 0, 0, 6, 1, 0 };
	/* v2.3: size 6 (excluding itself), 2 flag bytes, 4 bytes of padding size */
	static const unsigned char ext3[10] = { 0, 0, 0, 6, 0, 0, 0, 0, 0, 0 };
	size_t ext_size = !extended ? 0 : version == 4 ? sizeof(ext4) : sizeof(ext3);
	int fd = mkstemps(path, 4);
	struct lyrics *l;
	size_t total;
	FILE *file;

	assert(fd >= 0);
	total = ext_size + 10 + size;
	header[6] = (total >> 21) & 0x7f;
	header[7] = (total >> 14) & 0x7f;
	header[8] = (total >> 7) & 0x7f;
	header[9] = total & 0x7f;
	if (version == 4) {
		fh[4] = (size >> 21) & 0x7f;
		fh[5] = (size >> 14) & 0x7f;
		fh[6] = (size >> 7) & 0x7f;
		fh[7] = size & 0x7f;
	} else {
		fh[6] = (size >> 8) & 0xff;
		fh[7] = size & 0xff;
	}
	file = fdopen(fd, "wb");
	assert(file);
	assert(fwrite(header, 1, 10, file) == 10);
	assert(fwrite(version == 4 ? ext4 : ext3, 1, ext_size, file) == ext_size);
	assert(fwrite(fh, 1, 10, file) == 10);
	assert(fwrite(frame, 1, size, file) == size && fclose(file) == 0);
	l = lyrics_load_tag(path);
	if (count < 0) {
		assert(!l || !l->count);
	} else {
		assert(l && !l->error && l->count == (size_t)count);
		assert(!strcmp(l->lines[count - 1].text, last));
	}
	lyrics_free(l);
	assert(!unlink(path));
}

/* Parse text that must be valid. */
static struct lyrics *parse(const char *text)
{
	struct lyrics *l = lyrics_parse(text, strlen(text));
	assert(l && !l->error);
	return l;
}

int main(void)
{
	/*
	 * BOM, CRLF, metadata, a line with two timestamps, out-of-order and
	 * equal timestamps, and an [offset:] after the lines. Expected order:
	 * 1.00 Primo, 2.50 Second, 2.50 Translation (file order), 4.50 Second.
	 */
	struct lyrics *l = parse("\xef\xbb\xbf[ar:Artist]\r\n[00:02.500][00:04.5]Second\r\n[00:01.00]Primo è qui\n[00:02.500]Translation\n[offset:+100]\n");
	assert(l->count == 4 && l->offset_ms == 100);
	assert(!strcmp(l->lines[0].text, "Primo è qui"));
	assert(lyrics_find(l, 899, 0) == -1);
	assert(lyrics_find(l, 900, 0) == 0); /* 900 + offset 100 = 1000 */
	assert(lyrics_find(l, 2400, 0) == 1); /* first of the 2.50 group */
	assert(lyrics_find(l, 4400, 0) == 3);
	assert(lyrics_find(l, 1000, 0) == 0); /* backwards seek */
	assert(lyrics_find(l, 900, 250) == -1); /* output delay */
	assert(lyrics_find(l, INT64_MAX, -250) == 3); /* saturates, no overflow */
	assert(lyrics_find(l, INT64_MIN, 250) == -1);
	assert(!strcmp(l->lines[1].text, "Second"));
	assert(!strcmp(l->lines[2].text, "Translation"));
	lyrics_free(l);
	/* empty text, invalid times, absurd minutes, control and invalid bytes */
	l = parse("[00:00]Start\n[00:01]\n[00:60]bad\n[00:02.1234]bad\n[999999999999:00]bad\n[00:03]\033[31mcontrol\xff\n[offset:-250]\n");
	assert(l->count == 3 && l->offset_ms == -250);
	assert(lyrics_find(l, 249, 0) == -1);
	assert(lyrics_find(l, 1250, 0) == 1);
	assert(!strcmp(l->lines[1].text, ""));
	assert(!strchr(l->lines[2].text, '\033'));
	assert(strchr(l->lines[2].text, '?'));
	lyrics_free(l);
	/* every truncated prefix of a timestamp is rejected */
	l = parse("[\n[1\n[1:\n[1:0\n[1:00.\n[1:00.1\n[1:00.12\n");
	assert(l->count == 0);
	lyrics_free(l);
	assert(lyrics_find(NULL, 0, 0) == -1);
	/* inputs over 1 MiB are refused, not truncated */
	char *large = malloc(1024 * 1024 + 1);
	assert(large);
	memset(large, 'x', 1024 * 1024 + 1);
	l = lyrics_parse(large, 1024 * 1024 + 1);
	assert(l && l->error == EFBIG);
	lyrics_free(l);
	free(large);
	/* Sidecar precedence, extension fallback and non-regular files. */
	char folder[] = "tests/lyrics-XXXXXX", track[128], sidecar[128], fallback[128];
	assert(mkdtemp(folder));
	snprintf(track, sizeof(track), "%s/song.flac", folder);
	snprintf(sidecar, sizeof(sidecar), "%s/song.lrc", folder);
	snprintf(fallback, sizeof(fallback), "%s/song.flac.lrc", folder);
	FILE *file = fopen(fallback, "w");
	assert(file && fputs("[00:00]Fallback\n", file) >= 0 && fclose(file) == 0);
	l = lyrics_load_track(track);
	assert(l && !l->error && l->count == 1 && !strcmp(l->lines[0].text, "Fallback"));
	lyrics_free(l);
	file = fopen(sidecar, "w");
	assert(file && fputs("[00:00]Primary\n", file) >= 0 && fclose(file) == 0);
	l = lyrics_load_track(track);
	assert(l && !l->error && !strcmp(l->lines[0].text, "Primary"));
	lyrics_free(l);
	assert(!unlink(sidecar) && !mkfifo(sidecar, 0600));
	l = lyrics_load_track(track); /* must neither block nor use the fallback */
	assert(l && l->error == EINVAL);
	lyrics_free(l);
	assert(!unlink(sidecar) && !unlink(fallback) && !rmdir(folder));
	/* Malformed-byte regression corpus: parser must tolerate arbitrary input. */
	unsigned int state = 42;
	for (int round = 0; round < 5000; round++) {
		char input[128];
		for (size_t i = 0; i < sizeof(input); i++) {
			state = state * 1664525u + 1013904223u;
			input[i] = state >> 24;
		}
		l = lyrics_parse(input, sizeof(input));
		assert(l);
		lyrics_free(l);
	}
	/* Unsynchronized lyrics: lines have no timestamps, never highlight. */
	const char *plain = "line one\nline two\n\nline three\n";
	l = lyrics_from_unsynchronized(plain, strlen(plain));
	assert(l && !l->error && l->count == 3 && l->unsynchronized);
	assert(l->lines[0].time_ms == -1 && l->lines[1].time_ms == -1);
	assert(lyrics_find(l, 0, 0) == -1);
	assert(lyrics_find(l, 1234567, 0) == -1);
	assert(!strcmp(l->lines[2].text, "line three"));
	lyrics_free(l);
	l = lyrics_from_unsynchronized("", 0);
	assert(l && !l->error && l->count == 0);
	lyrics_free(l);
	/* ID3v2 USLT frames read straight from an MP3 (no input plugin here). */
	USLT(3, "\x03" "eng" "desc\0" "[00:01.00]Ciao\n[00:02]Привет\n", 2, "Привет");
	USLT(4, "\x03" "eng" "\0" "plain words\nsecond line", 2, "second line");
	USLT(3, "\x00" "eng" "\0" "caf\xe9", 1, "café");
	USLT(3, "\x01" "eng" "\xff\xfe\0\0" "\xff\xfe" "h\0i\0", 1, "hi");
	USLT(3, "\x02" "eng" "\0\0" "\0h\0i", 1, "hi"); /* UTF-16BE */
	USLT(3, "\x01" "eng", -1, NULL); /* truncated frame: no lyrics */
	USLT_EXT(3, "\x03" "eng" "\0" "after extended header", 1, "after extended header");
	USLT_EXT(4, "\x03" "eng" "\0" "after extended header", 1, "after extended header");
	puts("lyrics parser and timing tests passed");
	return 0;
}
