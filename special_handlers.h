int is_link (const char* filename);
int (*get_special_filename_handler (const char* filename)) (const char*, void (*add_file)(const char*, int));
int needs_special_mimetype_handler(const char* filename);
const char* get_mimetype(char* filename);
