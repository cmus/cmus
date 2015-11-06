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
        char *last_digit;
        size_t cpylen;
};

static void skip_ws(struct strprop *prop)
{
        while (*prop->strcpy != '\0' && (isspace(*prop->strcpy) || iscntrl(*prop->strcpy) || !isprint(*prop->strcpy)))
                prop->strcpy++;
        prop->cpylen = strlen(prop->strcpy);
}

static intmax_t grp_num(struct strprop *prop, const char pos)
{
        prop->last_digit = (char*)prop->strcpy+pos;
        while (*prop->last_digit != '\0' && isdigit(*prop->last_digit))
                prop->last_digit++;
        return strtoimax(prop->strcpy+pos, &prop->last_digit, 10);
}

static uint8_t mem_ok(const char *str, const struct strprop *prop, const char pos)
{
        return (str - pos + prop->cpylen - prop->strcpy) > 0;
}

int strnatcmp(const char *__restrict str1, const char *__restrict str2)
{
        if (!str1)
                return str2 ? -1 : 0;
        if (!str2)
                return 1;
        register char pos = 0;
        struct strprop prop1 = {.o_ptr = str1, .strcpy = str1}, prop2 = {.o_ptr = str2, .strcpy = str2};
        skip_ws(&prop1);
        skip_ws(&prop2);
        if (!prop1.cpylen || !prop2.cpylen)
                return prop1.cpylen - prop2.cpylen;
        uint8_t num_only = 0;
        while (mem_ok(str1, &prop1, pos) && mem_ok(str2, &prop2, pos)) {
                if (!isdigit(*(prop1.strcpy+pos)) && !isdigit(*(prop2.strcpy+pos))) {
                        num_only = 0;
                        pos++;
                        continue;
                } else if (!pos) {
                        num_only = 1;
                }
                break;
        }
        if (!pos && !num_only)
                return strcoll(prop1.strcpy, prop2.strcpy);
        const int cmp = strncasecmp(prop1.strcpy, prop2.strcpy, pos);
        if (cmp)
                return cmp;
        const int num1 = grp_num(&prop1, pos), num2 = grp_num(&prop2, pos);
        return (num1 != num2) ? num1 - num2 : strnatcmp(prop1.last_digit, prop2.last_digit);
}
