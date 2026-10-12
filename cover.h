/*
 * Album covers for the cover view (9): found beside the track or in its
 * tags, decoded, scaled to the space the view has, and turned into what
 * the terminal draws, sixel graphics or coloured half blocks.
 * Nothing here touches curses or the player. Same GPL terms as the rest
 * of cmus.
 */
#ifndef CMUS_COVER_H
#define CMUS_COVER_H

#include <stddef.h>

/* how the picture is drawn */
enum cover_mode {
	COVER_SIXEL,
	/* "▀" with the foreground for the top pixel, the background for the bottom */
	COVER_BLOCKS_TRUECOLOR,
	COVER_BLOCKS_256,
	/* no UTF-8: one space per cell, coloured with the background */
	COVER_SPACES_256,
};

/* an RGB picture, 3 bytes per pixel, rows without padding */
struct cover_image {
	int width, height;
	unsigned char *rgb;
};

struct cover {
	/*
	 * Escape sequences that draw the picture with its top left corner at
	 * the cursor; the cursor is left anywhere (save and restore it).
	 */
	char *data;
	size_t size;
	/* the cells the picture covers */
	int cols, rows;
	/* where it came from: a file name, or "embedded" */
	char *source;
	/* the picture's own size in pixels */
	int width, height;
	/* 0, or an errno value: ENOENT (no cover), EINVAL (can't decode), ENOMEM */
	int error;
};

/*
 * The cover of a track, as image file bytes (JPEG, PNG...), first found:
 *  - "song.jpg" / "song.png" beside "song.flac";
 *  - cover, folder, front, album or albumart .jpg/.jpeg/.png in the
 *    track's directory (any letter case);
 *  - the picture embedded in the track (ID3v2 APIC, FLAC PICTURE, Ogg
 *    METADATA_BLOCK_PICTURE, MP4 covr), preferring the front cover.
 * *source gets a malloc'd description. NULL if none.
 */
unsigned char *cover_find(const char *filename, size_t *size, char **source);

/* Only the picture embedded in the file's tags. */
unsigned char *cover_embedded(const char *filename, size_t *size);

/*
 * Decode image bytes and scale them to fit max_width x max_height pixels
 * (keeping the aspect ratio, never enlarging by more than 2x). Returns 0, or
 * -1 if the bytes are not an image it can read.
 */
int cover_decode(const unsigned char *data, size_t size, int max_width, int max_height,
		struct cover_image *image);
void cover_image_free(struct cover_image *image);

/* Sixel graphics for the picture, with up to 255 colours chosen for it. */
char *cover_sixel(const struct cover_image *image, size_t *size);

/*
 * Half blocks for the picture: two pixels per cell, one above the other
 * (one per cell for COVER_SPACES_256). Rows are separated by "go down one
 * line and back to the first column".
 */
char *cover_blocks(const struct cover_image *image, enum cover_mode mode, size_t *size);

/*
 * Everything at once: find, decode, scale to cols x rows cells of
 * cell_width x cell_height pixels, and encode. Call from the worker thread.
 * Never NULL unless out of memory; check ->error.
 */
struct cover *cover_load(const char *filename, enum cover_mode mode, int cols, int rows,
		int cell_width, int cell_height);
/* Accepts NULL. */
void cover_free(struct cover *cover);

#endif
