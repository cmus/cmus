/*
 * YouTube UI Module - youtube_ui.h
 *
 * Simple UI functions untuk YouTube download feature.
 * Menggunakan existing info_msg() dari cmus untuk display.
 */

#ifndef YOUTUBE_UI_H
#define YOUTUBE_UI_H

typedef enum {
	DOWNLOAD_PENDING,
	DOWNLOAD_IN_PROGRESS,
	DOWNLOAD_COMPLETED,
	DOWNLOAD_FAILED
} download_status;

typedef struct {
	const char *url;
	const char *title;
	download_status status;
	int progress;  /* 0-100 */
} youtube_download_info;

/*
 * Display download status message
 * Shows: "Downloading: <title>"
 */
void youtube_ui_show_download_start(const char *title);

/*
 * Display download completed message
 * Shows: "Downloaded: <filepath>"
 */
void youtube_ui_show_download_complete(const char *filepath);

/*
 * Display download error message
 * Shows: "Download failed: <error_message>"
 */
void youtube_ui_show_download_error(const char *error);

/*
 * Update download progress (optional)
 * Shows: "Downloading... 50%"
 */
void youtube_ui_update_progress(int percent);

#endif /* YOUTUBE_UI_H */
