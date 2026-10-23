#include "youtube.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define DOWNLOAD_SUBDIR ".config/cmus/downloads"
#define MAX_CMD_LEN     2048
#define MAX_PATH_LEN    1024

static int ytdlp_is_installed(void) {
	return system("which yt-dlp > /dev/null 2>&1") == 0;
}

static int create_directory_if_not_exists(const char *path) {
	struct stat st;

	if (stat(path, &st) == -1) {
		if (mkdir(path, 0755) == -1) {
			perror("Failed to create directory");
			return -1;
		}
	}
	return 0;
}

static char *get_download_path(void) {
	static char download_path[MAX_PATH_LEN];
	const char *home;

	home = getenv("HOME");
	if (!home) {
		struct passwd *pw = getpwuid(getuid());
		home = pw ? pw->pw_dir : "/tmp";
	}

	snprintf(download_path, sizeof(download_path), "%s/%s", home, DOWNLOAD_SUBDIR);
	return download_path;
}

int check_ytdlp_installed(void) {
	if (!ytdlp_is_installed()) {
		fprintf(stderr, "Error: yt-dlp is not installed. Please install it to use this feature.\n");
		return 0;
	}
	return 1;
}

int youtube_url_is_valid(const char *url) {
	if (!url) {
		fprintf(stderr, "Error: URL is NULL\n");
		return 0;
	}

	if (strstr(url, "youtube.com") || strstr(url, "youtu.be")) {
		return 1;
	}

	fprintf(stderr, "Error: URL is not a valid YouTube URL\n");
	return 0;
}

int youtube_download(const char *url, char *output_path, size_t path_len) {
	char command[MAX_CMD_LEN];
	char result_path[MAX_PATH_LEN];
	const char *download_dir = get_download_path();

	if (!youtube_url_is_valid(url) || !check_ytdlp_installed()) {
		fprintf(stderr, "Error: Invalid URL or yt-dlp not installed\n");
		return -1;
	}

	if (create_directory_if_not_exists(download_dir) == -1) {
		fprintf(stderr, "Error: Failed to create download directory\n");
		return -1;
	}

	snprintf(command, sizeof(command),
		"yt-dlp -x "
		"--audio-format opus "
		"--audio-quality 0 "
		"-o '%s/%%(title)s.%%(ext)s' "
		"--print after_move:filepath "
		"'%s' 2>&1",
		download_dir, url
	);

	FILE *pipe = popen(command, "r");

	if (!pipe) {
		perror("Failed to run yt-dlp command");
		return -1;
	}

	if (fgets(result_path, sizeof(result_path), pipe) == NULL) {
		fprintf(stderr, "Error: Failed to read yt-dlp output\n");
		pclose(pipe);
		return -1;
	}

	int exit_code = pclose(pipe);
	if (exit_code != 0) {
		fprintf(stderr, "Error: yt-dlp command failed with exit code %d\n", exit_code);
		return -1;
	}

	// Remove trailing newline from result_path
	result_path[strcspn(result_path, "\n")] = '\0';

	snprintf(output_path, path_len, "%s", result_path);
	return 0;
}
