#include "utils.h"
#include "special_handlers.h"
#include "xmalloc.h"

special_adder get_special_filename_handler (const char* filename) {
	int i;
	for (i=0; i<filename_handlers.count; i++) {
		if (filename_handlers.detectors[i](filename))
			return filename_handlers.handlers[i];
	}

	return 0;
}

const char* special_mimetype_handle(char* filename) {
	int i;
	for (i=0; i<mimetype_handlers.count; i++) {
		if (mimetype_handlers.detectors[i](filename))
			return mimetype_handlers.strings[i];
	}
	return NULL;
}

void register_special_filename_handler(identify_func detector, special_adder add_file_special) {
	int count = filename_handlers.count;
	filename_handlers.count++;
	filename_handlers.detectors = xrealloc(filename_handlers.detectors, sizeof(int(*)(const char* filename)) * (count + 1));
	filename_handlers.handlers = xrealloc(filename_handlers.handlers, sizeof(int(*)(const char* filename)) * (count + 1));

	filename_handlers.detectors[count] = detector;
	filename_handlers.handlers[count] = add_file_special;
}

void register_special_mimetype_handler(identify_func detector, const char *string) {
	int count = mimetype_handlers.count;
	mimetype_handlers.count++;
	mimetype_handlers.detectors = xrealloc(mimetype_handlers.detectors, sizeof(identify_func) * (count + 1));
	mimetype_handlers.strings = xrealloc(mimetype_handlers.strings, sizeof(const char*) * (count + 1));
	mimetype_handlers.detectors[count] = detector;
	mimetype_handlers.strings[count] = string;
}

void special_handlers_init(void) {
	filename_handlers = (struct special_filename_handlers) {
		.count = 0,
		.detectors = NULL,
		.handlers = NULL,
	};

	mimetype_handlers = (struct special_mimetype_handlers) {
		.count = 0,
		.detectors = NULL,
		.strings = NULL,
	};
}
