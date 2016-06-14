typedef void (*adder)(const char*, int);
typedef int (*special_adder)(const char*, adder);
typedef int (*identify_func)(const char*);

struct special_filename_handlers {
	size_t count;
	identify_func *detectors;
	special_adder *handlers;
};

struct special_mimetype_handlers {
	size_t count;
	identify_func *detectors;
	const char **strings;
};

struct special_filename_handlers filename_handlers;
struct special_mimetype_handlers mimetype_handlers;

special_adder get_special_filename_handler (const char* filename);
const char* special_mimetype_handle(char* filename);
void register_special_filename_handler(identify_func detector, special_adder add_file_special);
void register_special_mimetype_handler(identify_func detector, const char *string);
void special_handlers_init(void);