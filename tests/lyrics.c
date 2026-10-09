#include "lyrics.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static struct lyrics *parse(const char *text) {
  struct lyrics *l = lyrics_parse(text, strlen(text));
  assert(l && !l->error);
  return l;
}

int main(void) {
  struct lyrics *l =
      parse("\xef\xbb\xbf[ar:Artist]\r\n[00:02.500][00:04.5]Second\r\n[00:01."
            "00]Primo è qui\n[00:02.500]Translation\n[offset:+100]\n");
  assert(l->count == 4 && l->offset_ms == 100);
  assert(!strcmp(l->lines[0].text, "Primo è qui"));
  assert(lyrics_find(l, 899, 0) == -1);
  assert(lyrics_find(l, 900, 0) == 0);
  assert(lyrics_find(l, 2400, 0) == 1);
  assert(lyrics_find(l, 4400, 0) == 3);
  assert(lyrics_find(l, 1000, 0) == 0);   /* backwards seek */
  assert(lyrics_find(l, 900, 250) == -1); /* output delay */
  assert(lyrics_find(l, INT64_MAX, -250) == 3);
  assert(lyrics_find(l, INT64_MIN, 250) == -1);
  assert(!strcmp(l->lines[1].text, "Second"));
  assert(!strcmp(l->lines[2].text, "Translation"));
  lyrics_free(l);
  l = parse("[00:00]Start\n[00:01]\n[00:60]bad\n[00:02.1234]bad\n[999999999999:"
            "00]bad\n[00:03]\033[31mcontrol\xff\n[offset:-250]\n");
  assert(l->count == 3 && l->offset_ms == -250);
  assert(lyrics_find(l, 249, 0) == -1);
  assert(lyrics_find(l, 1250, 0) == 1);
  assert(!strcmp(l->lines[1].text, ""));
  assert(!strchr(l->lines[2].text, '\033'));
  assert(strchr(l->lines[2].text, '?'));
  lyrics_free(l);
  l = parse("[\n[1\n[1:\n[1:0\n[1:00.\n[1:00.1\n[1:00.12\n");
  assert(l->count == 0);
  lyrics_free(l);
  assert(lyrics_find(NULL, 0, 0) == -1);
  char *large = malloc(1024 * 1024 + 1);
  assert(large);
  memset(large, 'x', 1024 * 1024 + 1);
  l = lyrics_parse(large, 1024 * 1024 + 1);
  assert(l && l->error == EFBIG);
  lyrics_free(l);
  free(large);
  /* Sidecar precedence, extension fallback and non-regular files. */
  char folder[] = "tests/lyrics-XXXXXX", track[128], sidecar[128],
       fallback[128];
  assert(mkdtemp(folder));
  snprintf(track, sizeof(track), "%s/song.flac", folder);
  snprintf(sidecar, sizeof(sidecar), "%s/song.lrc", folder);
  snprintf(fallback, sizeof(fallback), "%s/song.flac.lrc", folder);
  FILE *file = fopen(fallback, "w");
  assert(file && fputs("[00:00]Fallback\n", file) >= 0 && fclose(file) == 0);
  l = lyrics_load_track(track);
  assert(l && !l->error && l->count == 1 &&
         !strcmp(l->lines[0].text, "Fallback"));
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
  l = lyrics_from_unsynchronized("line one\nline two\n\nline three\n", 27);
  assert(l && !l->error && l->count == 3);
  assert(l->lines[0].time_ms == -1 && l->lines[1].time_ms == -1);
  assert(lyrics_find(l, 0, 0) == -1);
  assert(lyrics_find(l, 1234567, 0) == -1);
  assert(!strcmp(l->lines[2].text, "line three"));
  lyrics_free(l);
  l = lyrics_from_unsynchronized("", 0);
  assert(l && !l->error && l->count == 0);
  lyrics_free(l);
  puts("lyrics parser and timing tests passed");
  return 0;
}
