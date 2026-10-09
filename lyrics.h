/* Synchronized local lyrics. Same GPL terms as the rest of cmus. */
#ifndef CMUS_LYRICS_H
#define CMUS_LYRICS_H

#include <stddef.h>
#include <stdint.h>

struct lyric_line {
  int64_t time_ms;
  char *text;
  size_t order;
};

struct lyrics {
  struct lyric_line *lines;
  size_t count;
  int64_t offset_ms;
  int error;
  /* set when lines came from an untimed tag; all lines have time_ms == -1 */
  int unsynchronized;
};

struct lyrics *lyrics_parse(const char *text, size_t size);
struct lyrics *lyrics_from_unsynchronized(const char *text, size_t size);
struct lyrics *lyrics_load_track(const char *filename);
struct lyrics *lyrics_load_tag(const char *filename);
void lyrics_free(struct lyrics *lyrics);
/* First line in the active timestamp group; -1 before the first timestamp. */
int lyrics_find(const struct lyrics *lyrics, int64_t position_ms, int delay_ms);

#endif
