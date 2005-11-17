/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <irman_config.h>
#include <irman.h>
#include <sconf.h>
#include <misc.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>

static char *button_names[] = {
	"btn_play",
	"btn_stop",
	"btn_pause",
	"btn_prev",
	"btn_next",
	"btn_seek_bwd",
	"btn_seek_fwd",
	"btn_vol_up",
	"btn_vol_down",
	"btn_mute",
	"btn_shuffle",
	"btn_repeat",
	"btn_continue",
	NULL
};

int irman_config(void)
{
	struct irman *irman;
	char device[1024];
	int i;

	do {
		printf("device: ");
		fflush(stdout);
		if (fgets(device, sizeof(device), stdin) != NULL)
			break;
	} while (1);
	i = strlen(device);
	if (i > 0 && device[i - 1] == '\n')
		device[i - 1] = 0;

	irman = irman_open(device);
	if (irman == NULL) {
		fprintf(stderr, "error initialising irman: `%s'\n",
			strerror(errno));
		return 1;
	}
	for (i = 0; button_names[i]; i++) {
		char code[IRMAN_CODE_LEN];
		char text[IRMAN_TEXT_SIZE];

		printf("%s: ", button_names[i]);
		fflush(stdout);
		if (irman_get_code(irman, code)) {
			if (errno == ENXIO) {
				fprintf(stderr, "irman not initialised (bug)\n");
			} else {
				fprintf(stderr, "error reading code: `%s'\n",
					strerror(errno));
			}
			irman_close(irman);
			return 1;
		}
		irman_code_to_text(text, code);
		sconf_set_str_option(&sconf_head, button_names[i], text);
		printf("OK\n");
	}
	irman_close(irman);
	sconf_set_str_option(&sconf_head, "irman_device", device);
	return 0;
}
