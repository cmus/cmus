#include "youtube_ui.h"

#include <stdio.h>
#include <stdarg.h>

extern void info_msg(const char *fmt, ...);
extern void error_msg(const char *fmt, ...);

void youtube_ui_show_download_start(const char *title) {
	if (!title) {
		error_msg("youtube_ui: title is NULL");
		return;
	}

	fprintf(stderr, "[youtube_ui] Starting download for: %s\n", title);
	info_msg("YouTube: Downloading '%s'...", title);
}

void youtube_ui_show_download_complete(const char *filepath) {
	if (!filepath) {
		error_msg("youtube_ui: filepath is NULL");
		return;
	}

	fprintf(stderr, "[youtube_ui] Download complete - %s\n", filepath);
	info_msg("YouTube: Downloaded successfully to %s", filepath);
}

void youtube_ui_show_download_error(const char *error) {
	if (!error) {
		error_msg("youtube_ui: error message is NULL");
		return;
	}

	fprintf(stderr, "[youtube_ui] Download error - %s\n", error);
	error_msg("YouTube: Download failed - %s", error);
}

void youtube_ui_update_progress(int percent) {
	if (percent < 0 || percent > 100) {
		fprintf(stderr, "[youtube_ui] Invalid progress value: %d\n", percent);
		return;
	}

	fprintf(stderr, "[youtube_ui] Progress - %d%%\n", percent);

	if (percent % 10 == 0 || percent == 100) {
		info_msg("YouTube: Downloading... %d%%", percent);
	}
}
