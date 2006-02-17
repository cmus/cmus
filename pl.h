/*
 * Copyright 2006 Timo Hirvonen
 */

#ifndef PL_H
#define PL_H

#include "editable.h"
#include "track_info.h"
#include "track.h"

extern struct editable pl_editable;
extern struct simple_track *pl_cur_track;

void pl_init(void);
struct track_info *pl_set_next(void);
struct track_info *pl_set_prev(void);
struct track_info *pl_set_selected(void);
void pl_add_track(struct track_info *track_info);
void pl_sel_current(void);
void pl_reshuffle(void);
int pl_for_each(int (*cb)(void *data, struct track_info *ti), void *data);

#endif
