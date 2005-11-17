/* 
 * Copyright 2005 Timo Hirvonen
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _WORKER_H
#define _WORKER_H

#define WORKER_TYPE_NONE (0U)
#define WORKER_TYPE_ANY (~0U)

extern void worker_init(void);
extern void worker_exit(void);

/*
 * @type: >0 && <WORKER_TYPE_ANY
 * @cb:   callback
 * @data: data to the @cb
 */
extern void worker_add_job(unsigned int type, void (*cb)(void *data), void *data);

/*
 * @type: job type. >0, use WORKER_TYPE_ANY to remove all
 */
extern void worker_remove_jobs(unsigned int type);

/*
 * @type: type of this job
 *
 * returns: 0 or 1
 *
 * long jobs should call this to see whether it should cancel
 * call from job function _only_
 */
extern int worker_cancelling(void);

#endif
