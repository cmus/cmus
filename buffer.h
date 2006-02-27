#ifndef _BUFFER_H
#define _BUFFER_H

#define CHUNK_SIZE (64 * 1024)

extern unsigned int buffer_nr_chunks;

void buffer_init(void);
void buffer_get_rpos(char **rpos, int *size);
void buffer_get_wpos(char **wpos, int *size);
void buffer_consume(int count);
void buffer_fill(int count);
void buffer_reset(void);
int buffer_get_filled_chunks(void);

#endif
