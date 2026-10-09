/* Local LRC parser, independent of playback and curses.
 * Same GPL terms as the rest of cmus. */
#include "lyrics.h"
#include "input.h"
#include "keyval.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LRC_MAX_BYTES (1024 * 1024)
#define LRC_MAX_LINES 16384

static int timestamp(const char *p, const char **end, int64_t *ms) {
  int64_t minutes = 0;
  int seconds, fraction = 0, scale = 100;
  const char *start = p;

  if (*p++ != '[' || *p < '0' || *p > '9')
    return 0;
  while (*p >= '0' && *p <= '9') {
    minutes = minutes * 10 + *p++ - '0';
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

static char *clean_text(const char *text) {
  size_t size = strlen(text), in = 0, out = 0;
  char *copy = malloc(size + 1);
  if (!copy)
    return NULL;
  while (in < size) {
    unsigned char c = text[in];
    size_t n = c < 0x80                 ? 1
               : c >= 0xc2 && c <= 0xdf ? 2
               : c >= 0xe0 && c <= 0xef ? 3
               : c >= 0xf0 && c <= 0xf4 ? 4
                                        : 0;
    int valid = n && in + n <= size;
    for (size_t i = 1; valid && i < n; i++)
      valid = ((unsigned char)text[in + i] & 0xc0) == 0x80;
    if (valid && n > 2) {
      unsigned char next = text[in + 1];
      valid = !(c == 0xe0 && next < 0xa0) && !(c == 0xed && next >= 0xa0) &&
              !(c == 0xf0 && next < 0x90) && !(c == 0xf4 && next >= 0x90);
    }
    if (!valid) {
      copy[out++] = '?';
      in++;
    } else if ((n == 1 && (c < 0x20 || c == 0x7f)) ||
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

static int compare_lines(const void *a, const void *b) {
  const struct lyric_line *x = a, *y = b;
  if (x->time_ms != y->time_ms)
    return x->time_ms > y->time_ms ? 1 : -1;
  return (x->order > y->order) - (x->order < y->order);
}

static int append_one(struct lyrics *result, size_t *capacity,
                      size_t *text_bytes, const char *content,
                      int64_t time_ms) {
  struct lyric_line *grown;

  if (result->count == LRC_MAX_LINES) {
    result->error = EFBIG;
    return 0;
  }
  if (strlen(content) + 1 > 4 * LRC_MAX_BYTES - *text_bytes) {
    result->error = EFBIG;
    return 0;
  }
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
  result->lines[result->count] =
      (struct lyric_line){time_ms, clean, result->count};
  *text_bytes += strlen(clean) + 1;
  result->count++;
  return 1;
}

static int append_lines(struct lyrics *result, char *buffer, int timed) {
  size_t capacity = 0, text_bytes = 0;
  char *line = buffer;

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
      p = line;
      while (timestamp(p, &after, &ms))
        p = after;
      const char *content = p;
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

static char *latin1_to_utf8(const unsigned char *data, size_t size) {
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

static char *utf16_to_utf8(const unsigned char *data, size_t size,
                           int little_endian) {
  char *out = malloc(size * 3 / 2 + 1);
  size_t out_size = 0;
  if (!out)
    return NULL;
  for (size_t i = 0; i + 1 < size; i += 2) {
    unsigned int c = little_endian ? data[i] | (data[i + 1] << 8)
                                   : (data[i] << 8) | data[i + 1];

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

/* Read an ID3v2.3 or v2.4 USLT frame directly from an MP3 file. */
static char *mp3_lyrics(const char *filename) {
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
  if (version < 3 || version > 4) {
    fclose(file);
    return NULL;
  }

  remaining = ((size_t)(header[6] & 0x7f) << 21) |
              ((size_t)(header[7] & 0x7f) << 14) |
              ((size_t)(header[8] & 0x7f) << 7) | (size_t)(header[9] & 0x7f);

  if (header[5] & 0x40) {
    unsigned char extended[4];
    size_t extended_size;

    if (fread(extended, 1, 4, file) != 4) {
      fclose(file);
      return NULL;
    }
    remaining -= 4;

    if (version == 4) {
      extended_size = ((size_t)(extended[0] & 0x7f) << 21) |
                      ((size_t)(extended[1] & 0x7f) << 14) |
                      ((size_t)(extended[2] & 0x7f) << 7) |
                      (size_t)(extended[3] & 0x7f);
    } else {
      extended_size = ((size_t)extended[0] << 24) |
                      ((size_t)extended[1] << 16) | ((size_t)extended[2] << 8) |
                      (size_t)extended[3];
    }

    if (remaining < extended_size) {
      fclose(file);
      return NULL;
    }
    fseek(file, extended_size, SEEK_CUR);
    remaining -= extended_size;
  }

  while (remaining >= 10) {
    char id[5];
    size_t size;

    if (fread(frame, 1, 10, file) != 10)
      break;
    remaining -= 10;

    memcpy(id, frame, 4);
    id[4] = 0;

    if (version == 4) {
      size = ((size_t)(frame[4] & 0x7f) << 21) |
             ((size_t)(frame[5] & 0x7f) << 14) |
             ((size_t)(frame[6] & 0x7f) << 7) | (size_t)(frame[7] & 0x7f);
    } else {
      size = ((size_t)frame[4] << 24) | ((size_t)frame[5] << 16) |
             ((size_t)frame[6] << 8) | (size_t)frame[7];
    }

    if (!id[0])
      break;
    if (remaining < size)
      break;

    if (!strcmp(id, "USLT")) {
      unsigned char *data = malloc(size);
      char *text = NULL;
      unsigned char encoding;
      const unsigned char *p, *content;
      size_t used = 0;

      if (!data || fread(data, 1, size, file) != size) {
        free(data);
        break;
      }
      remaining -= size;
      fclose(file);

      encoding = data[0];
      p = data + 4;

      if (encoding == 1 || encoding == 2) {
        while (p + 1 < data + size) {
          if (!p[0] && !p[1]) {
            p += 2;
            break;
          }
          p += 2;
        }
        content = p;
        used = size - (size_t)(content - data);

        text = utf16_to_utf8(content, used, encoding == 1);
      } else {
        while (p < data + size && *p)
          p++;
        p++;
        content = p;
        used = size - (size_t)(content - data);

        if (encoding == 3) {
          text = malloc(used + 1);
          if (text) {
            memcpy(text, content, used);
            text[used] = 0;
          }
        } else {
          text = latin1_to_utf8(content, used);
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

void lyrics_free(struct lyrics *lyrics) {
  if (!lyrics)
    return;
  for (size_t i = 0; i < lyrics->count; i++)
    free(lyrics->lines[i].text);
  free(lyrics->lines);
  free(lyrics);
}

struct lyrics *lyrics_parse(const char *text, size_t size) {
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
  memcpy(buffer, text, size);
  buffer[size] = 0;
  for (size_t i = 0; i < size; i++)
    if (!buffer[i])
      buffer[i] = ' ';
  line = buffer;
  if (size >= 3 && !memcmp(line, "\xef\xbb\xbf", 3))
    line += 3;

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

  append_lines(result, line, 1);
  free(buffer);
  if (result->count > 1)
    qsort(result->lines, result->count, sizeof(*result->lines), compare_lines);
  return result;
}

struct lyrics *lyrics_from_unsynchronized(const char *text, size_t size) {
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

static struct lyrics *load_file(const char *path) {
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
  result = lyrics_parse(buffer ? buffer : "", error ? 0 : used);
  if (result && error)
    result->error = error;
  free(buffer);
  return result;
}

struct lyrics *lyrics_load_track(const char *filename) {
  size_t len = strlen(filename);
  char *path = malloc(len + 5);
  struct lyrics *result;
  if (!path)
    return NULL;
  memcpy(path, filename, len + 1);
  char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  char *extension = strrchr(base, '.');
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

struct lyrics *lyrics_load_tag(const char *filename) {
  struct input_plugin *ip;
  struct keyval *comments;
  struct lyrics *result = NULL;
  const char *text = NULL;
  size_t i;

  ip = ip_new(filename);
  if (ip_open(ip)) {
    ip_delete(ip);
    return NULL;
  }
  if (ip_read_comments(ip, &comments)) {
    ip_delete(ip);
    return NULL;
  }

  for (i = 0; comments[i].key; i++) {
    if (strcasestr(comments[i].key, "lyric")) {
      text = comments[i].val;
      break;
    }
  }

  keyvals_free(comments);
  ip_delete(ip);

  if (!text) {
    size_t filename_length = strlen(filename);
    if (filename_length > 4 &&
        !strcasecmp(filename + filename_length - 4, ".mp3"))
      text = mp3_lyrics(filename);
  }

  if (text) {
    int synced = strstr(text, "[00:") != NULL || strstr(text, "[01:") != NULL ||
                 strstr(text, "[02:") != NULL || strstr(text, "[03:") != NULL ||
                 strstr(text, "[04:") != NULL || strstr(text, "[05:") != NULL ||
                 strstr(text, "[06:") != NULL || strstr(text, "[07:") != NULL ||
                 strstr(text, "[08:") != NULL || strstr(text, "[09:") != NULL;

    result = synced ? lyrics_parse(text, strlen(text))
                    : lyrics_from_unsynchronized(text, strlen(text));
  }

  return result;
}

int lyrics_find(const struct lyrics *lyrics, int64_t position_ms,
                int delay_ms) {
  size_t lo = 0, hi;
  int64_t now;
  if (!lyrics || lyrics->error || !lyrics->count)
    return -1;
  int64_t delta = (int64_t)lyrics->offset_ms - delay_ms;
  now = delta > 0 && position_ms > INT64_MAX - delta   ? INT64_MAX
        : delta < 0 && position_ms < INT64_MIN - delta ? INT64_MIN
                                                       : position_ms + delta;
  hi = lyrics->count;
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
  if (lyrics->lines[lo].time_ms < 0)
    return -1;
  while (lo && lyrics->lines[lo - 1].time_ms == lyrics->lines[lo].time_ms)
    lo--;
  return (int)lo;
}
