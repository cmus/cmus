/*
 * Lyrics for the lyrics view: an LRC parser plus loaders for sidecar
 * files and lyrics tags. Nothing here touches curses or the player, so
 * tests/lyrics.c can link this file on its own.
 *
 * Same GPL terms as the rest of cmus.
 *
 * LRC is a plain text format where each line starts with one or more
 * timestamps:
 *
 *	[00:12.34]First line
 *	[00:15.00][01:20.00]A chorus line sung twice
 *	[offset:+250]
 *
 * Lines are kept sorted by time; a line with several timestamps is stored
 * once per timestamp. Untimed text (from a tag) is stored with
 * time_ms == -1 and the "unsynchronized" flag set.
 *
 * Input is untrusted (files from the internet, tags written by anything),
 * so every size is bounded and text is sanitised before it reaches curses.
 */
#include "lyrics.h"
#include "input.h"
#include "keyval.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* largest .lrc file or lyrics tag read; real files are a few KiB */
#define LRC_MAX_BYTES (1024 * 1024)
/* most lines kept, counting each timestamp of a repeated line */
#define LRC_MAX_LINES 16384

/*
 * Parse one "[mm:ss]", "[mm:ss.x]", "[mm:ss.xx]" or "[mm:ss.xxx]" at p.
 * Minutes may have any number of digits; ':' is accepted instead of '.'
 * before the fraction, as some editors write it. The fraction is read as
 * a decimal fraction of a second, so ".5", ".50" and ".500" are all 500 ms.
 *
 * On success stores the time in *ms, points *end just past the ']' and
 * returns 1. Anything else, including metadata tags like "[ar:Artist]",
 * returns 0 and leaves *end and *ms alone.
 */
static int timestamp(const char *p, const char **end, int64_t *ms)
{
	int64_t minutes = 0;
	int seconds, fraction = 0, scale = 100;
	const char *start = p;

	if (*p++ != '[' || *p < '0' || *p > '9')
		return 0;
	while (*p >= '0' && *p <= '9') {
		minutes = minutes * 10 + *p++ - '0';
		/* far beyond any track; also keeps the arithmetic below from overflowing */
		if (minutes > 1000000)
			return 0;
	}
	if (*p++ != ':' || p[0] < '0' || p[0] > '5' || p[1] < '0' || p[1] > '9')
		return 0;
	seconds = (p[0] - '0') * 10 + p[1] - '0';
	p += 2;
	if (*p == '.' || *p == ':') {
		p++;
		if (*p < '0' || *p > '9')
			return 0;
		while (*p >= '0' && *p <= '9') {
			/* more than three digits: not a time we can represent */
			if (!scale)
				return 0;
			fraction += (*p++ - '0') * scale;
			scale /= 10;
		}
	}
	if (*p++ != ']')
		return 0;
	*end = p;
	*ms = (minutes * 60 + seconds) * 1000 + fraction;
	return p > start;
}

/*
 * Return a malloc'd copy of text that is safe to print with curses:
 * invalid UTF-8 bytes (bad lead bytes, truncated or overlong sequences,
 * UTF-16 surrogates, code points past U+10FFFF) become '?', and C0/C1
 * control characters (including tabs and escape sequences) become a
 * space. The result is never longer than the input.
 */
static char *clean_text(const char *text)
{
	size_t size = strlen(text), in = 0, out = 0;
	char *copy = malloc(size + 1);
	if (!copy)
		return NULL;
	while (in < size) {
		unsigned char c = text[in];
		/* length of the sequence announced by the lead byte; 0 = invalid lead */
		size_t n = c < 0x80              ? 1
			: c >= 0xc2 && c <= 0xdf ? 2
			: c >= 0xe0 && c <= 0xef ? 3
			: c >= 0xf0 && c <= 0xf4 ? 4
						 : 0;
		int valid = n && in + n <= size;
		for (size_t i = 1; valid && i < n; i++)
			valid = ((unsigned char)text[in + i] & 0xc0) == 0x80;
		/* reject overlong forms, surrogates and values above U+10FFFF */
		if (valid && n > 2) {
			unsigned char next = text[in + 1];
			valid = !(c == 0xe0 && next < 0xa0) && !(c == 0xed && next >= 0xa0) &&
				!(c == 0xf0 && next < 0x90) && !(c == 0xf4 && next >= 0x90);
		}
		if (!valid) {
			/* skip one byte only, to resynchronise on the next lead byte */
			copy[out++] = '?';
			in++;
		} else if ((n == 1 && (c < 0x20 || c == 0x7f)) ||
			/* U+0080..U+009F, the C1 controls */
			(n == 2 && c == 0xc2 && (unsigned char)text[in + 1] < 0xa0)) {
			copy[out++] = ' ';
			in += n;
		} else {
			memcpy(copy + out, text + in, n);
			out += n;
			in += n;
		}
	}
	copy[out] = 0;
	return copy;
}

/*
 * qsort() comparator: by time, then by position in the file. qsort() is
 * not stable, so "order" keeps lines that share a timestamp in the order
 * they were written.
 */
static int compare_lines(const void *a, const void *b)
{
	const struct lyric_line *x = a, *y = b;
	if (x->time_ms != y->time_ms)
		return x->time_ms > y->time_ms ? 1 : -1;
	return (x->order > y->order) - (x->order < y->order);
}

/*
 * Append one line with the given time (-1 for untimed text).
 *
 * *capacity is the allocated size of result->lines and *text_bytes the
 * total text stored so far. Both limits matter: a 1 MiB file of lines
 * like "[00:01][00:02]...[99:59]x" could otherwise expand into a huge
 * number of copies of the same text.
 *
 * Returns 1 on success; on failure sets result->error and returns 0.
 */
static int append_one(struct lyrics *result, size_t *capacity, size_t *text_bytes,
	const char *content, int64_t time_ms)
{
	struct lyric_line *grown;

	if (result->count == LRC_MAX_LINES) {
		result->error = EFBIG;
		return 0;
	}
	/* written so that it cannot overflow: text_bytes never exceeds the cap */
	if (strlen(content) + 1 > 4 * LRC_MAX_BYTES - *text_bytes) {
		result->error = EFBIG;
		return 0;
	}
	/* grow geometrically; at most LRC_MAX_LINES entries, so this cannot overflow */
	if (result->count == *capacity) {
		*capacity = *capacity ? *capacity * 2 : 64;
		grown = realloc(result->lines, *capacity * sizeof(*grown));
		if (!grown) {
			result->error = ENOMEM;
			return 0;
		}
		result->lines = grown;
	}
	char *clean = clean_text(content);
	if (!clean) {
		result->error = ENOMEM;
		return 0;
	}
	result->lines[result->count] = (struct lyric_line){ time_ms, clean, result->count };
	*text_bytes += strlen(clean) + 1;
	result->count++;
	return 1;
}

/*
 * Split buffer (NUL-terminated, modified in place) into lines and append
 * them. "\r\n" line endings are accepted.
 *
 * timed: every leading timestamp of a line produces one entry with the
 *        text after the last timestamp. Lines without a timestamp (LRC
 *        metadata, comments, blank lines) are ignored.
 * untimed: every non-empty line is appended with time -1.
 *
 * Returns 0 if a limit was hit or memory ran out (result->error says
 * which); the lines appended so far are kept.
 */
static int append_lines(struct lyrics *result, char *buffer, int timed)
{
	size_t capacity = 0, text_bytes = 0;
	char *line = buffer;

	/* result is empty in practice, but keep the byte limit exact anyway */
	for (size_t i = 0; i < result->count; i++)
		text_bytes += strlen(result->lines[i].text) + 1;

	while (line) {
		const char *p, *after;
		int64_t ms;
		char *next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		size_t len = strlen(line);
		if (len && line[len - 1] == '\r')
			line[--len] = 0;

		if (timed) {
			/* first pass: find the text after the last timestamp */
			p = line;
			while (timestamp(p, &after, &ms))
				p = after;
			const char *content = p;
			/* second pass: one entry per timestamp */
			p = line;
			while (timestamp(p, &after, &ms)) {
				if (!append_one(result, &capacity, &text_bytes, content, ms))
					return 0;
				p = after;
			}
		} else {
			if (len && !append_one(result, &capacity, &text_bytes, line, -1))
				return 0;
		}
		line = next;
	}
	return 1;
}

/*
 * ID3 text encodings, converted to malloc'd, NUL-terminated UTF-8.
 * Embedded NULs are kept; lyrics_parse() turns them into spaces later.
 */

/* ISO-8859-1: each byte is its own code point, so at most 2 bytes out per byte in. */
static char *latin1_to_utf8(const unsigned char *data, size_t size)
{
	char *out = malloc(size * 2 + 1);
	size_t out_size = 0;
	if (!out)
		return NULL;
	for (size_t i = 0; i < size; i++) {
		if (data[i] < 0x80) {
			out[out_size++] = data[i];
		} else {
			out[out_size++] = 0xc0 | (data[i] >> 6);
			out[out_size++] = 0x80 | (data[i] & 0x3f);
		}
	}
	out[out_size] = 0;
	return out;
}

/*
 * UTF-16 without BOM. Every 2 input bytes give at most 3 output bytes,
 * and a surrogate pair (4 bytes) gives 4, hence size * 3 / 2. Unpaired
 * surrogates are encoded as-is and become '?' in clean_text(). A trailing
 * odd byte is ignored.
 */
static char *utf16_to_utf8(const unsigned char *data, size_t size, int little_endian)
{
	char *out = malloc(size * 3 / 2 + 1);
	size_t out_size = 0;
	if (!out)
		return NULL;
	for (size_t i = 0; i + 1 < size; i += 2) {
		unsigned int c = little_endian ? data[i] | (data[i + 1] << 8)
					       : (data[i] << 8) | data[i + 1];

		/* a high surrogate followed by a low one: combine into one code point */
		if (c >= 0xd800 && c < 0xdc00 && i + 3 < size) {
			unsigned int low = little_endian ? data[i + 2] | (data[i + 3] << 8)
							 : (data[i + 2] << 8) | data[i + 3];
			if (low >= 0xdc00 && low < 0xe000) {
				c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
				i += 2;
			}
		}

		if (c < 0x80) {
			out[out_size++] = c;
		} else if (c < 0x800) {
			out[out_size++] = 0xc0 | (c >> 6);
			out[out_size++] = 0x80 | (c & 0x3f);
		} else if (c < 0x10000) {
			out[out_size++] = 0xe0 | (c >> 12);
			out[out_size++] = 0x80 | ((c >> 6) & 0x3f);
			out[out_size++] = 0x80 | (c & 0x3f);
		} else {
			out[out_size++] = 0xf0 | (c >> 18);
			out[out_size++] = 0x80 | ((c >> 12) & 0x3f);
			out[out_size++] = 0x80 | ((c >> 6) & 0x3f);
			out[out_size++] = 0x80 | (c & 0x3f);
		}
	}
	out[out_size] = 0;
	return out;
}

/*
 * Read the first USLT (unsynchronised lyrics) frame of an ID3v2.3 or
 * v2.4 tag at the start of an MP3 file. Needed because id3.c does not
 * parse USLT and the mad plugin uses id3.c; with ffmpeg the frame arrives
 * as a "lyrics-XXX" comment instead (see comment.c).
 *
 * Returns malloc'd UTF-8 text, or NULL if there is no usable frame.
 * Sizes come from the file, so each one is checked against what is left
 * of the tag before it is used.
 */
static char *mp3_lyrics(const char *filename)
{
	FILE *file = fopen(filename, "rb");
	unsigned char header[10], frame[10];
	int version;
	size_t remaining;

	if (!file)
		return NULL;
	if (fread(header, 1, 10, file) != 10 || memcmp(header, "ID3", 3)) {
		fclose(file);
		return NULL;
	}

	version = header[3];
	/* v2.3 tags with unsynchronisation are rare and not handled here */
	if (version < 3 || version > 4 || (version == 3 && (header[5] & 0x80))) {
		fclose(file);
		return NULL;
	}

	/* tag size, without the 10 byte header: a "syncsafe" integer (7 bits per byte) */
	remaining = ((size_t)(header[6] & 0x7f) << 21) | ((size_t)(header[7] & 0x7f) << 14) |
		((size_t)(header[8] & 0x7f) << 7) | (size_t)(header[9] & 0x7f);

	/* extended header: we only need to skip it */
	if (header[5] & 0x40) {
		unsigned char extended[4];
		size_t extended_size;

		if (remaining < 4 || fread(extended, 1, 4, file) != 4) {
			fclose(file);
			return NULL;
		}
		remaining -= 4;

		/*
		 * v2.4: syncsafe, and counts the 4 size bytes just read.
		 * v2.3: plain 32 bit, and excludes them.
		 */
		if (version == 4) {
			extended_size = ((size_t)(extended[0] & 0x7f) << 21) |
				((size_t)(extended[1] & 0x7f) << 14) |
				((size_t)(extended[2] & 0x7f) << 7) | (size_t)(extended[3] & 0x7f);
			extended_size = extended_size >= 4 ? extended_size - 4 : 0;
		} else {
			extended_size = ((size_t)extended[0] << 24) | ((size_t)extended[1] << 16) |
				((size_t)extended[2] << 8) | (size_t)extended[3];
		}

		if (remaining < extended_size) {
			fclose(file);
			return NULL;
		}
		fseek(file, extended_size, SEEK_CUR);
		remaining -= extended_size;
	}

	/* frames: 4 byte id, 4 byte size, 2 byte flags, then the data */
	while (remaining >= 10) {
		char id[5];
		size_t size;

		if (fread(frame, 1, 10, file) != 10)
			break;
		remaining -= 10;

		memcpy(id, frame, 4);
		id[4] = 0;

		/* frame sizes are syncsafe in v2.4 only */
		if (version == 4) {
			size = ((size_t)(frame[4] & 0x7f) << 21) |
				((size_t)(frame[5] & 0x7f) << 14) |
				((size_t)(frame[6] & 0x7f) << 7) | (size_t)(frame[7] & 0x7f);
		} else {
			size = ((size_t)frame[4] << 24) | ((size_t)frame[5] << 16) |
				((size_t)frame[6] << 8) | (size_t)frame[7];
		}

		/* a zero byte where an id should be: we reached the padding */
		if (!id[0])
			break;
		if (remaining < size)
			break;

		/* compressed, encrypted or unsynchronised frames are skipped */
		if (!strcmp(id, "USLT") && !(frame[9] & (version == 4 ? 0x0f : 0xe0))) {
			/* encoding (1), language (3), description, lyrics */
			unsigned char *data = size >= 4 && size <= 4 * LRC_MAX_BYTES ? malloc(size) : NULL;
			const unsigned char *p, *end;
			char *text = NULL;
			int wide;

			if (!data || fread(data, 1, size, file) != size) {
				free(data);
				break;
			}
			fclose(file);

			end = data + size;
			/* encodings: 0 ISO-8859-1, 1 UTF-16 with BOM, 2 UTF-16BE, 3 UTF-8 */
			wide = data[0] == 1 || data[0] == 2;
			p = data + 4;
			/* skip the description and its terminator */
			if (wide) {
				while (p + 1 < end && (p[0] || p[1]))
					p += 2;
				p = p + 2 <= end ? p + 2 : end;
				/* UTF-16 with BOM: the BOM tells the byte order */
				if (data[0] == 1 && p + 1 < end && p[0] == 0xff && p[1] == 0xfe)
					text = utf16_to_utf8(p + 2, end - p - 2, 1);
				else if (data[0] == 1 && p + 1 < end && p[0] == 0xfe && p[1] == 0xff)
					text = utf16_to_utf8(p + 2, end - p - 2, 0);
				/* no BOM: little endian for 1, as most writers do; 2 is big endian */
				else
					text = utf16_to_utf8(p, end - p, data[0] == 1);
			} else {
				while (p < end && *p)
					p++;
				p = p < end ? p + 1 : end;
				if (data[0] == 3) {
					text = malloc(end - p + 1);
					if (text) {
						memcpy(text, p, end - p);
						text[end - p] = 0;
					}
				} else {
					text = latin1_to_utf8(p, end - p);
				}
			}

			free(data);
			return text;
		}

		fseek(file, size, SEEK_CUR);
		remaining -= size;
	}

	fclose(file);
	return NULL;
}

/* Accepts NULL, like free(). */
void lyrics_free(struct lyrics *lyrics)
{
	if (!lyrics)
		return;
	for (size_t i = 0; i < lyrics->count; i++)
		free(lyrics->lines[i].text);
	free(lyrics->lines);
	free(lyrics);
}

/*
 * Parse LRC text (not necessarily NUL-terminated). Never returns a
 * partial object without saying so: on failure result->error is set
 * (EFBIG or ENOMEM) and the lines parsed so far are kept. Returns NULL
 * only if the result itself cannot be allocated.
 */
struct lyrics *lyrics_parse(const char *text, size_t size)
{
	struct lyrics *result = calloc(1, sizeof(*result));
	char *buffer, *line;
	if (!result)
		return NULL;
	if (size > LRC_MAX_BYTES) {
		result->error = EFBIG;
		return result;
	}
	buffer = malloc(size + 1);
	if (!buffer) {
		result->error = ENOMEM;
		return result;
	}
	/* work on a NUL-terminated copy; embedded NULs would end lines early */
	memcpy(buffer, text, size);
	buffer[size] = 0;
	for (size_t i = 0; i < size; i++)
		if (!buffer[i])
			buffer[i] = ' ';
	line = buffer;
	/* UTF-8 byte order mark, written by some Windows editors */
	if (size >= 3 && !memcmp(line, "\xef\xbb\xbf", 3))
		line += 3;

	/*
	 * [offset:MS] may appear anywhere, usually in the header. Positive
	 * values make lyrics appear earlier, as in other players. Values
	 * beyond an hour are nonsense and ignored. The newlines are restored
	 * afterwards, as append_lines() splits the same buffer again.
	 */
	for (char *scan = line; scan;) {
		char *nl = strchr(scan, '\n');
		if (nl)
			*nl = 0;
		if (!strncmp(scan, "[offset:", 8)) {
			char *end;
			errno = 0;
			long value = strtol(scan + 8, &end, 10);
			if (end != scan + 8 && !strcmp(end, "]") && !errno && value >= -3600000 &&
				value <= 3600000)
				result->offset_ms = value;
		}
		if (!nl)
			break;
		*nl = '\n';
		scan = nl + 1;
	}

	/* on error, result->error is set and the lines read so far are kept */
	append_lines(result, line, 1);
	free(buffer);
	/* timestamps may come in any order, e.g. a chorus listed once with all its times */
	if (result->count > 1)
		qsort(result->lines, result->count, sizeof(*result->lines), compare_lines);
	return result;
}

/* Plain text lyrics (no timestamps): one line per non-empty input line. */
struct lyrics *lyrics_from_unsynchronized(const char *text, size_t size)
{
	struct lyrics *result = calloc(1, sizeof(*result));
	char *buffer;
	if (!result)
		return NULL;
	if (size > LRC_MAX_BYTES) {
		result->error = EFBIG;
		return result;
	}
	buffer = malloc(size + 1);
	if (!buffer) {
		result->error = ENOMEM;
		return result;
	}
	memcpy(buffer, text, size);
	buffer[size] = 0;
	for (size_t i = 0; i < size; i++)
		if (!buffer[i])
			buffer[i] = ' ';
	append_lines(result, buffer, 0);
	free(buffer);
	result->unsynchronized = 1;
	return result;
}

/*
 * Read and parse an .lrc file. Always returns a result (unless out of
 * memory) so the caller can tell "no file" (error == ENOENT) from "file
 * exists but is unusable".
 *
 * O_NONBLOCK and the S_ISREG check stop a FIFO or device named song.lrc
 * from hanging the worker thread. The read loop asks for one byte more
 * than the limit, so a file that grew after fstat() is still caught.
 */
static struct lyrics *load_file(const char *path)
{
	struct lyrics *result;
	struct stat st;
	char *buffer = NULL;
	size_t used = 0;
	int error = 0, fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		error = errno;
	else if (fstat(fd, &st))
		error = errno;
	else if (!S_ISREG(st.st_mode))
		error = EINVAL;
	else if (st.st_size > LRC_MAX_BYTES)
		error = EFBIG;
	else if (!(buffer = malloc(LRC_MAX_BYTES + 1)))
		error = ENOMEM;
	while (!error && used <= LRC_MAX_BYTES) {
		ssize_t n = read(fd, buffer + used, LRC_MAX_BYTES + 1 - used);
		if (!n)
			break;
		if (n < 0) {
			if (errno == EINTR)
				continue;
			error = errno;
			break;
		}
		used += n;
	}
	if (fd >= 0)
		close(fd);
	/* parse even on error, so the error is reported through the result */
	result = lyrics_parse(buffer ? buffer : "", error ? 0 : used);
	if (result && error)
		result->error = error;
	free(buffer);
	return result;
}

/*
 * Lyrics from a sidecar file: "dir/song.lrc" first, then "dir/song.flac.lrc".
 * Only the first one that exists is used, even if it is empty or broken,
 * so that a user's own file is never silently ignored.
 */
struct lyrics *lyrics_load_track(const char *filename)
{
	size_t len = strlen(filename);
	/* filename + ".lrc"; the extension is cut or kept, never longer */
	char *path = malloc(len + 5);
	struct lyrics *result;
	if (!path)
		return NULL;
	memcpy(path, filename, len + 1);
	char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	char *extension = strrchr(base, '.');
	/* ".hidden" has no extension */
	if (extension && extension != base)
		*extension = 0;
	strcat(path, ".lrc");
	result = load_file(path);
	if (result && result->error == ENOENT) {
		lyrics_free(result);
		memcpy(path, filename, len);
		memcpy(path + len, ".lrc", 5);
		result = load_file(path);
	}
	free(path);
	return result;
}

/*
 * Lyrics stored in the track's tags: a "lyrics" comment (Vorbis, FLAC,
 * MP4, ffmpeg), else an ID3v2 USLT frame read directly from an MP3.
 *
 * Opens the track with its input plugin, which can be slow (network
 * filesystems, ffmpeg probing), so this runs on the worker thread only.
 * Returns NULL if there are no lyrics.
 */
struct lyrics *lyrics_load_tag(const char *filename)
{
	struct input_plugin *ip;
	struct keyval *comments;
	struct lyrics *result;
	char *text = NULL;
	size_t len;

	ip = ip_new(filename);
	if (!ip_open(ip) && !ip_read_comments(ip, &comments)) {
		const char *val = keyvals_get_val(comments, "lyrics");
		/* copy: val belongs to comments, freed on the next line */
		if (val && *val)
			text = strdup(val);
		keyvals_free(comments);
	}
	ip_delete(ip);

	len = strlen(filename);
	if (!text && len > 4 && !strcasecmp(filename + len - 4, ".mp3"))
		text = mp3_lyrics(filename);
	if (!text || !*text) {
		free(text);
		return NULL;
	}

	/* timed (LRC) text if any line has a timestamp, plain text otherwise */
	result = lyrics_parse(text, strlen(text));
	if (result && !result->error && !result->count) {
		lyrics_free(result);
		result = lyrics_from_unsynchronized(text, strlen(text));
	}
	free(text);
	return result;
}

/*
 * Index of the line to highlight at position_ms, or -1 if none (no
 * lyrics, untimed lyrics, or before the first timestamp).
 *
 * The file's [offset:] moves lyrics earlier and delay_ms (from
 * :lyrics-offset) moves them later. Lines are sorted, so this is a binary
 * search for the last line with time <= now; it then steps back to the
 * first line with the same time, so the caller can highlight every line
 * of a group that starts together.
 */
int lyrics_find(const struct lyrics *lyrics, int64_t position_ms, int delay_ms)
{
	size_t lo = 0, hi;
	int64_t now;
	if (!lyrics || lyrics->error || !lyrics->count)
		return -1;
	int64_t delta = (int64_t)lyrics->offset_ms - delay_ms;
	/* position + delta, saturating instead of overflowing */
	now = delta > 0 && position_ms > INT64_MAX - delta     ? INT64_MAX
		: delta < 0 && position_ms < INT64_MIN - delta ? INT64_MIN
							       : position_ms + delta;
	hi = lyrics->count;
	/* invariant: lines before lo start at or before now, lines from hi on after it */
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (lyrics->lines[mid].time_ms <= now)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (!lo)
		return -1;
	lo--;
	/* untimed lines have time -1 and are never "current" */
	if (lyrics->lines[lo].time_ms < 0)
		return -1;
	while (lo && lyrics->lines[lo - 1].time_ms == lyrics->lines[lo].time_ms)
		lo--;
	return (int)lo;
}
