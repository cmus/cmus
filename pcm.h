#ifndef _PCM_H
#define _PCM_H

typedef void (*pcm_conv_func)(char *dst, const char *src, int count);
typedef void (*pcm_conv_in_place_func)(char *buf, int count);

extern pcm_conv_func pcm_conv[8];
extern pcm_conv_in_place_func pcm_conv_in_place[8];

#endif
