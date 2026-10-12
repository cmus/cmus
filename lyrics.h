/*
 * Local lyrics for the lyrics view (8): LRC sidecar files and lyrics tags.
 * Nothing is downloaded. Same GPL terms as the rest of cmus.
 */
#ifndef CMUS_LYRICS_H
#define CMUS_LYRICS_H

#include <stddef.h>
#include <stdint.h>

struct lyric_line {
	/* start time in milliseconds; -1 for untimed lyrics */
	int64_t time_ms;
	/* valid UTF-8 without control characters, safe to print */
	char *text;
	/* position in the file, keeps equal timestamps in file order */
	size_t order;
};

struct lyrics {
	/* sorted by time_ms, then by order */
	struct lyric_line *lines;
	size_t count;
	/* from an LRC [offset:] tag; positive values show lyrics earlier */
	int64_t offset_ms;
	/*
	 * 0, or an errno value: ENOENT (no file), EINVAL (not a regular
	 * file), EFBIG (over the size limits), ENOMEM, or a read error.
	 * Lines read before an error are kept.
	 */
	int error;
	/* set when lines came from an untimed tag; all lines have time_ms == -1 */
	int unsynchronized;
};

/* Parse LRC text of the given size (need not be NUL-terminated). */
struct lyrics *lyrics_parse(const char *text, size_t size);
/* Plain text, one line per non-empty line, all untimed. */
struct lyrics *lyrics_from_unsynchronized(const char *text, size_t size);
/*
 * Sidecar for a track: "song.lrc", else "song.ext.lrc". Never NULL unless
 * out of memory; error == ENOENT means neither file exists.
 */
struct lyrics *lyrics_load_track(const char *filename);
/*
 * Lyrics tag of a track, timed if it contains LRC timestamps. Opens the
 * file with its input plugin: call from the worker thread. NULL if none.
 */
struct lyrics *lyrics_load_tag(const char *filename);
/* Accepts NULL. */
void lyrics_free(struct lyrics *lyrics);
/*
 * Line to highlight at position_ms, shifted by the file's offset and by
 * delay_ms (positive = later): the first line of the active timestamp
 * group, or -1 before the first timestamp or for untimed lyrics.
 */
int lyrics_find(const struct lyrics *lyrics, int64_t position_ms, int delay_ms);

#endif
