/*
 * Copyright 2007 Johannes Weißl
 */

#ifndef _APE_H
#define _APE_H

typedef struct APE APE;

extern APE *ape_new(void);
extern void ape_free(APE *ape);
extern int ape_read_tags(APE *ape, int fd, int slow);
extern char *ape_get_comment(APE *ape, char **val);

#endif
