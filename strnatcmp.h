#ifndef CMUS_STRNATCMP_H
#define CMUS_STRNATCMP_H

/*
 * compare naturally (i.e. "track 1" is less than "track 10")
 *
 * returns: difference between strings as int
 */
int strnatcmp(const char *, const char *);

#endif
