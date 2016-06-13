struct special_filename_handlers {
	size_t count;
	int (**detectors) (const char* filename);
	int (**handlers) (const char* filename, void (*add_file)(const char*, int));
};

struct special_mimetype_handlers {
	size_t count;
	int (**detectors) (const char* filename);
	const char **strings;
};

struct special_filename_handlers filename_handlers;
struct special_mimetype_handlers mimetype_handlers;

int (*get_special_filename_handler (const char* filename)) (const char*, void (*add_file)(const char*, int));
const char* special_mimetype_handle(char* filename);
void register_special_filename_handler(int (*detector)(const char*), int (*add_file_special) (const char*, void (*add_file)(const char*, int)));
void register_special_mimetype_handler(int (*detector)(const char*), const char *string);
void special_handlers_init(void);
