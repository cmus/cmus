#ifndef YOUTUBE_H
#define YOUTUBE_H

#include <stddef.h>

/*
 * Check if yt-dlp is installed on the system
 * Returns: 1 if installed, 0 otherwise (error message printed to stderr)
 */
int check_ytdlp_installed(void);

/*
 * Validate if URL is a valid YouTube URL
 * Returns: 1 if valid YouTube URL, 0 otherwise (error message printed to stderr)
 */
int youtube_url_is_valid(const char *url);

/*
 * Download audio from YouTube URL
 * Returns: 0 on success, -1 on error
 */
int youtube_download(const char *url, char *output_path, size_t path_len);

#endif // YOUTUBE_H
