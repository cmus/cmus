#ifndef _PATH_H
#define _PATH_H

void path_strip(char *str);
char *path_absolute_cwd(const char *src, const char *cwd);
char *path_absolute(const char *src);

#endif
