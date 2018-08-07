/*
 * Copyright 2011-2013 Various Authors
 * Copyright 2011 Johannes Weißl
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CMUS_AAC_H
#define CMUS_AAC_H

#include "../channelmap.h"
#include <neaacdec.h>

static inline channel_position_t channel_position_aac(unsigned char c)
{
	switch (c) {
	case FRONT_CHANNEL_CENTER:	return CHANNEL_POSITION_FRONT_CENTER;
	case FRONT_CHANNEL_LEFT:	return CHANNEL_POSITION_FRONT_LEFT;
	case FRONT_CHANNEL_RIGHT:	return CHANNEL_POSITION_FRONT_RIGHT;
	case SIDE_CHANNEL_LEFT:		return CHANNEL_POSITION_SIDE_LEFT;
	case SIDE_CHANNEL_RIGHT:	return CHANNEL_POSITION_SIDE_RIGHT;
	case BACK_CHANNEL_LEFT:		return CHANNEL_POSITION_REAR_LEFT;
	case BACK_CHANNEL_RIGHT:	return CHANNEL_POSITION_REAR_RIGHT;
	case BACK_CHANNEL_CENTER:	return CHANNEL_POSITION_REAR_CENTER;
	case LFE_CHANNEL:		return CHANNEL_POSITION_LFE;
	default:			return CHANNEL_POSITION_INVALID;
	}
}

#endif
