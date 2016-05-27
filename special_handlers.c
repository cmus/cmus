#include "cue_utils.h"
#include "utils.h"
#include "special_handlers.h"

int is_link (const char* filename) {
	return is_cue_url(filename);
}

int (*get_special_filename_handler (const char* filename)) (const char*, void (*add_file)(const char*, int)) {
#ifdef CONFIG_CUE
	if (is_cue_filename(filename)) return add_file_cue;
	else
#endif

	return 0;
}

int needs_special_mimetype_handler(const char* filename) {
	if (
		is_cdda_url(filename)
#ifdef CONFIG_CUE
		|| is_cue_url(filename)
#endif
	) return 1;
	else return 0;
}

const char* get_mimetype(char* filename) {
	if (is_cue_url(filename)) return "application/x-cue";
	else if (is_cdda_url(filename)) return "x-content/audio-cdda";
	return 0;
}
