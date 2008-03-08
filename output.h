/*
 * Copyright 2005 Timo Hirvonen
 */

#ifndef _OUTPUT_H
#define _OUTPUT_H

#include "mixer.h"
#include "op.h"
#include "sf.h"

extern int volume_max;
extern int volume_l;
extern int volume_r;

void op_load_plugins(void);
void op_exit_plugins(void);

/*
 * select output plugin and open its mixer
 *
 * errors: OP_ERROR_{ERRNO, NO_PLUGIN}
 */
int op_select(const char *name);
int op_select_any(void);

/*
 * open selected plugin
 *
 * errors: OP_ERROR_{}
 */
int op_open(sample_format_t sf);

/*
 * drop pcm data
 *
 * errors: OP_ERROR_{ERRNO}
 */
int op_drop(void);

/*
 * close plugin
 *
 * errors: OP_ERROR_{}
 */
int op_close(void);

/*
 * returns bytes written or error
 *
 * errors: OP_ERROR_{ERRNO}
 */
int op_write(const char *buffer, int count);

/*
 * errors: OP_ERROR_{}
 */
int op_pause(void);
int op_unpause(void);

/*
 * returns space in output buffer in bytes or -1 if busy
 */
int op_buffer_space(void);

/*
 * errors: OP_ERROR_{}
 */
int op_reset(void);

void mixer_open(void);
void mixer_close(void);
int mixer_set_volume(int left, int right);
int mixer_read_volume(void);
int mixer_get_fds(int *fds);

/*
 * errors: OP_ERROR_{NO_PLUGIN, NOT_INITIALIZED, NOT_OPTION}
 */
int op_set_option(unsigned int id, const char *val);
int op_get_option(unsigned int id, char **val);

int op_for_each_option(void (*cb)(unsigned int id, const char *key));
char *op_get_error_msg(int rc, const char *arg);
void op_dump_plugins(void);
const char *op_get_current(void);

#endif
