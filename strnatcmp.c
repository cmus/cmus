/*
 * Copyright 2008-2015 Various Authors
 * Copyright 2004-2005 Timo Hirvonen
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

#include "strnatcmp.h"

#include <string.h>
#include <ctype.h>
#include <inttypes.h>

struct strprop {
        const char *o_ptr, *strcpy;
        size_t cpylen;
        char *last_digit;
};

static void adjust_cpy_pos(struct strprop *prop)
{
        while (*prop->strcpy != '\0' &&
               (isspace(*prop->strcpy) ||
                iscntrl(*prop->strcpy) ||
                !isprint(*prop->strcpy)))
                prop->strcpy++;

        prop->cpylen = strlen(prop->strcpy);
}

static int find_num_at_pos(struct strprop *prop, const char *pos)
{
        prop->last_digit = (char*)prop->strcpy+*pos;

        while (*prop->last_digit != '\0' && isdigit(*prop->last_digit))
                prop->last_digit++;

        return strtoimax(prop->strcpy+*pos, &prop->last_digit, 10);
}

int strnatcmp(const char *str1, const char *str2)
{
        int result = 0;

        if (str1 == NULL)
                return -1;

        if (str2 == NULL)
                return 1;

        struct strprop prop1 = {.o_ptr = str1, .strcpy = str1};
        struct strprop prop2 = {.o_ptr = str2, .strcpy = str2};

        adjust_cpy_pos(&prop1);
        adjust_cpy_pos(&prop2);

        if (prop1.cpylen == 0 || prop2.cpylen == 0)
                return prop1.cpylen - prop2.cpylen;

        uint8_t have_num_only = 0;
        char pos = 0;

        while ((str1 + prop1.cpylen - prop1.strcpy - pos) > 0 && /* must not deref. foreign memory */
               (str2 + prop2.cpylen - prop2.strcpy - pos) > 0) {

                if (*(prop1.strcpy+pos) != '\0' &&
                    *(prop2.strcpy+pos) != '\0' &&
                    !isdigit(*(prop1.strcpy+pos)) && !isdigit(*(prop2.strcpy+pos))) {
                        have_num_only = 0;
                        pos++;
                        continue;
                } else if (pos == 0 &&
                           (str1 + prop1.cpylen - prop1.strcpy - pos) > 0 &&
                           (str2 + prop2.cpylen - prop2.strcpy - pos) > 0) {
                        have_num_only = 1;
                }
                break;
        }

        if (pos != 0 || have_num_only == 1) {
                const int cmp = strncasecmp(prop1.strcpy, prop2.strcpy, pos);
                if (cmp == 0) {
                        const int num1 = find_num_at_pos(&prop1, &pos);
                        const int num2 = find_num_at_pos(&prop2, &pos);

                        if (num1 != num2)
                                result = num1 - num2;
                        else
                                result = strnatcmp(prop1.last_digit, prop2.last_digit);
                } else {
                        result = cmp;
                }
        } else {
                result = strcoll(prop1.strcpy, prop2.strcpy);
        }

        return result;
}
