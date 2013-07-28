/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004-2006 Timo Hirvonen
 *
 * keys.[ch] by Frank Terbeck <ft@bewatermyfriend.org>
 * heavily modified by Timo Hirvonen
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

#include "keys.h"
#include "help.h"
#include "ui_curses.h"
#include "command_mode.h"
#include "xmalloc.h"

#if defined(__sun__)
#include <ncurses.h>
#else
#include <curses.h>
#endif

const char * const key_context_names[NR_CTXS + 1] = {
	"browser",
	"common",
	"filters",
	"library",
	"playlist",
	"queue",
	"settings",
	NULL
};

struct binding *key_bindings[NR_CTXS] = { NULL, };

static const enum key_context view_to_context[] = {
	CTX_LIBRARY,
	CTX_LIBRARY,
	CTX_PLAYLIST,
	CTX_QUEUE,
	CTX_BROWSER,
	CTX_FILTERS,
	CTX_SETTINGS,
};

#define KEY_IS_CHAR -255

/* key_table {{{
 *
 * key: KEY_IS_CHAR, not a key
 * ch:  0, not a char
 */
const struct key key_table[] = {
	{ "!",			KEY_IS_CHAR,		33	},
	{ "\"",			KEY_IS_CHAR,		34	},
	{ "#",			KEY_IS_CHAR,		35	},
	{ "$",			KEY_IS_CHAR,		36	},
	{ "%",			KEY_IS_CHAR,		37	},
	{ "&",			KEY_IS_CHAR,		38	},
	{ "'",			KEY_IS_CHAR,		39	},
	{ "(",			KEY_IS_CHAR,		40	},
	{ ")",			KEY_IS_CHAR,		41	},
	{ "*",			KEY_IS_CHAR,		42	},
	{ "+",			KEY_IS_CHAR,		43	},
	{ ",",			KEY_IS_CHAR,		44	},
	{ "-",			KEY_IS_CHAR,		45	},
	{ ".",			KEY_IS_CHAR,		46	},
	{ "/",			KEY_IS_CHAR,		47	},
	{ "0",			KEY_IS_CHAR,		48	},
	{ "1",			KEY_IS_CHAR,		49	},
	{ "2",			KEY_IS_CHAR,		50	},
	{ "3",			KEY_IS_CHAR,		51	},
	{ "4",			KEY_IS_CHAR,		52	},
	{ "5",			KEY_IS_CHAR,		53	},
	{ "6",			KEY_IS_CHAR,		54	},
	{ "7",			KEY_IS_CHAR,		55	},
	{ "8",			KEY_IS_CHAR,		56	},
	{ "9",			KEY_IS_CHAR,		57	},
	{ ":",			KEY_IS_CHAR,		58	},
	{ ";",			KEY_IS_CHAR,		59	},
	{ "<",			KEY_IS_CHAR,		60	},
	{ "=",			KEY_IS_CHAR,		61	},
	{ ">",			KEY_IS_CHAR,		62	},
	{ "?",			KEY_IS_CHAR,		63	},
	{ "@",			KEY_IS_CHAR,		64	},
	{ "A",			KEY_IS_CHAR,		65	},
	{ "B",			KEY_IS_CHAR,		66	},
	{ "C",			KEY_IS_CHAR,		67	},
	{ "D",			KEY_IS_CHAR,		68	},
	{ "E",			KEY_IS_CHAR,		69	},
	{ "F",			KEY_IS_CHAR,		70	},
	{ "F1",			KEY_F(1),		0	},
	{ "F10",		KEY_F(10),		0	},
	{ "F11",		KEY_F(11),		0	},
	{ "F12",		KEY_F(12),		0	},
	{ "F2",			KEY_F(2),		0	},
	{ "F3",			KEY_F(3),		0	},
	{ "F4",			KEY_F(4),		0	},
	{ "F5",			KEY_F(5),		0	},
	{ "F6",			KEY_F(6),		0	},
	{ "F7",			KEY_F(7),		0	},
	{ "F8",			KEY_F(8),		0	},
	{ "F9",			KEY_F(9),		0	},
	{ "G",			KEY_IS_CHAR,		71	},
	{ "H",			KEY_IS_CHAR,		72	},
	{ "I",			KEY_IS_CHAR,		73	},
	{ "J",			KEY_IS_CHAR,		74	},
	{ "K",			KEY_IS_CHAR,		75	},
	{ "KP_center",		KEY_B2,			0	},
	{ "KP_lower_left",	KEY_C1,			0	},
	{ "KP_lower_right",	KEY_C3,			0	},
	{ "KP_upper_left",	KEY_A1,			0	},
	{ "KP_upper_right",	KEY_A3,			0	},
	{ "L",			KEY_IS_CHAR,		76	},
	{ "M",			KEY_IS_CHAR,		77	},
	{ "M-!",		KEY_IS_CHAR,		161	},
	{ "M-\"",		KEY_IS_CHAR,		162	},
	{ "M-#",		KEY_IS_CHAR,		163	},
	{ "M-$",		KEY_IS_CHAR,		164	},
	{ "M-%",		KEY_IS_CHAR,		165	},
	{ "M-&",		KEY_IS_CHAR,		166	},
	{ "M-'",		KEY_IS_CHAR,		167	},
	{ "M-(",		KEY_IS_CHAR,		168	},
	{ "M-)",		KEY_IS_CHAR,		169	},
	{ "M-*",		KEY_IS_CHAR,		170	},
	{ "M-+",		KEY_IS_CHAR,		171	},
	{ "M-,",		KEY_IS_CHAR,		172	},
	{ "M--",		KEY_IS_CHAR,		173	},
	{ "M-.",		KEY_IS_CHAR,		174	},
	{ "M-/",		KEY_IS_CHAR,		175	},
	{ "M-0",		KEY_IS_CHAR,		176	},
	{ "M-1",		KEY_IS_CHAR,		177	},
	{ "M-2",		KEY_IS_CHAR,		178	},
	{ "M-3",		KEY_IS_CHAR,		179	},
	{ "M-4",		KEY_IS_CHAR,		180	},
	{ "M-5",		KEY_IS_CHAR,		181	},
	{ "M-6",		KEY_IS_CHAR,		182	},
	{ "M-7",		KEY_IS_CHAR,		183	},
	{ "M-8",		KEY_IS_CHAR,		184	},
	{ "M-9",		KEY_IS_CHAR,		185	},
	{ "M-:",		KEY_IS_CHAR,		186	},
	{ "M-;",		KEY_IS_CHAR,		187	},
	{ "M-<",		KEY_IS_CHAR,		188	},
	{ "M-=",		KEY_IS_CHAR,		189	},
	{ "M->",		KEY_IS_CHAR,		190	},
	{ "M-?",		KEY_IS_CHAR,		191	},
	{ "M-@",		KEY_IS_CHAR,		192	},
	{ "M-A",		KEY_IS_CHAR,		193	},
	{ "M-B",		KEY_IS_CHAR,		194	},
	{ "M-C",		KEY_IS_CHAR,		195	},
	{ "M-D",		KEY_IS_CHAR,		196	},
	{ "M-E",		KEY_IS_CHAR,		197	},
	{ "M-F",		KEY_IS_CHAR,		198	},
	{ "M-G",		KEY_IS_CHAR,		199	},
	{ "M-H",		KEY_IS_CHAR,		200	},
	{ "M-I",		KEY_IS_CHAR,		201	},
	{ "M-J",		KEY_IS_CHAR,		202	},
	{ "M-K",		KEY_IS_CHAR,		203	},
	{ "M-L",		KEY_IS_CHAR,		204	},
	{ "M-M",		KEY_IS_CHAR,		205	},
	{ "M-N",		KEY_IS_CHAR,		206	},
	{ "M-O",		KEY_IS_CHAR,		207	},
	{ "M-P",		KEY_IS_CHAR,		208	},
	{ "M-Q",		KEY_IS_CHAR,		209	},
	{ "M-R",		KEY_IS_CHAR,		210	},
	{ "M-S",		KEY_IS_CHAR,		211	},
	{ "M-T",		KEY_IS_CHAR,		212	},
	{ "M-U",		KEY_IS_CHAR,		213	},
	{ "M-V",		KEY_IS_CHAR,		214	},
	{ "M-W",		KEY_IS_CHAR,		215	},
	{ "M-X",		KEY_IS_CHAR,		216	},
	{ "M-Y",		KEY_IS_CHAR,		217	},
	{ "M-Z",		KEY_IS_CHAR,		218	},
	{ "M-[",		KEY_IS_CHAR,		219	},
	{ "M-\\",		KEY_IS_CHAR,		220	},
	{ "M-]",		KEY_IS_CHAR,		221	},
	{ "M-^",		KEY_IS_CHAR,		222	},
	{ "M-^?",		KEY_IS_CHAR,		255	},
	{ "M-^@",		KEY_IS_CHAR,		128	},
	{ "M-^A",		KEY_IS_CHAR,		129	},
	{ "M-^B",		KEY_IS_CHAR,		130	},
	{ "M-^C",		KEY_IS_CHAR,		131	},
	{ "M-^D",		KEY_IS_CHAR,		132	},
	{ "M-^E",		KEY_IS_CHAR,		133	},
	{ "M-^F",		KEY_IS_CHAR,		134	},
	{ "M-^G",		KEY_IS_CHAR,		135	},
	{ "M-^H",		KEY_IS_CHAR,		136	},
	{ "M-^I",		KEY_IS_CHAR,		137	},
	{ "M-^J",		KEY_IS_CHAR,		138	},
	{ "M-^K",		KEY_IS_CHAR,		139	},
	{ "M-^L",		KEY_IS_CHAR,		140	},
	{ "M-^M",		KEY_IS_CHAR,		141	},
	{ "M-^N",		KEY_IS_CHAR,		142	},
	{ "M-^O",		KEY_IS_CHAR,		143	},
	{ "M-^P",		KEY_IS_CHAR,		144	},
	{ "M-^Q",		KEY_IS_CHAR,		145	},
	{ "M-^R",		KEY_IS_CHAR,		146	},
	{ "M-^S",		KEY_IS_CHAR,		147	},
	{ "M-^T",		KEY_IS_CHAR,		148	},
	{ "M-^U",		KEY_IS_CHAR,		149	},
	{ "M-^V",		KEY_IS_CHAR,		150	},
	{ "M-^W",		KEY_IS_CHAR,		151	},
	{ "M-^X",		KEY_IS_CHAR,		152	},
	{ "M-^Y",		KEY_IS_CHAR,		153	},
	{ "M-^Z",		KEY_IS_CHAR,		154	},
	{ "M-^[",		KEY_IS_CHAR,		155	},
	{ "M-^\\",		KEY_IS_CHAR,		156	},
	{ "M-^]",		KEY_IS_CHAR,		157	},
	{ "M-^^",		KEY_IS_CHAR,		158	},
	{ "M-^_",		KEY_IS_CHAR,		159	},
	{ "M-_",		KEY_IS_CHAR,		223	},
	{ "M-`",		KEY_IS_CHAR,		224	},
	{ "M-a",		KEY_IS_CHAR,		225	},
	{ "M-b",		KEY_IS_CHAR,		226	},
	{ "M-c",		KEY_IS_CHAR,		227	},
	{ "M-d",		KEY_IS_CHAR,		228	},
	{ "M-e",		KEY_IS_CHAR,		229	},
	{ "M-f",		KEY_IS_CHAR,		230	},
	{ "M-g",		KEY_IS_CHAR,		231	},
	{ "M-h",		KEY_IS_CHAR,		232	},
	{ "M-i",		KEY_IS_CHAR,		233	},
	{ "M-j",		KEY_IS_CHAR,		234	},
	{ "M-k",		KEY_IS_CHAR,		235	},
	{ "M-l",		KEY_IS_CHAR,		236	},
	{ "M-m",		KEY_IS_CHAR,		237	},
	{ "M-n",		KEY_IS_CHAR,		238	},
	{ "M-o",		KEY_IS_CHAR,		239	},
	{ "M-p",		KEY_IS_CHAR,		240	},
	{ "M-q",		KEY_IS_CHAR,		241	},
	{ "M-r",		KEY_IS_CHAR,		242	},
	{ "M-s",		KEY_IS_CHAR,		243	},
	{ "M-space",		KEY_IS_CHAR,		160	},
	{ "M-t",		KEY_IS_CHAR,		244	},
	{ "M-u",		KEY_IS_CHAR,		245	},
	{ "M-v",		KEY_IS_CHAR,		246	},
	{ "M-w",		KEY_IS_CHAR,		247	},
	{ "M-x",		KEY_IS_CHAR,		248	},
	{ "M-y",		KEY_IS_CHAR,		249	},
	{ "M-z",		KEY_IS_CHAR,		250	},
	{ "M-{",		KEY_IS_CHAR,		251	},
	{ "M-|",		KEY_IS_CHAR,		252	},
	{ "M-}",		KEY_IS_CHAR,		253	},
	{ "M-~",		KEY_IS_CHAR,		254	},
	{ "N",			KEY_IS_CHAR,		78	},
	{ "O",			KEY_IS_CHAR,		79	},
	{ "P",			KEY_IS_CHAR,		80	},
	{ "Q",			KEY_IS_CHAR,		81	},
	{ "R",			KEY_IS_CHAR,		82	},
	{ "S",			KEY_IS_CHAR,		83	},
	{ "S-begin",		KEY_SBEG,		0	},
	{ "S-cancel",		KEY_SCANCEL,		0	},
	{ "S-command",		KEY_SCOMMAND,		0	},
	{ "S-copy",		KEY_SCOPY,		0	},
	{ "S-create",		KEY_SCREATE,		0	},
	{ "S-del_line",		KEY_SDL,		0	},
	{ "S-delete",		KEY_SDC,		0	},
	{ "S-eol",		KEY_SEOL,		0	},
	{ "S-exit",		KEY_SEXIT,		0	},
	{ "S-find",		KEY_SFIND,		0	},
	{ "S-help",		KEY_SHELP,		0	},
	{ "S-home",		KEY_SHOME,		0	},
	{ "S-insert",		KEY_SIC,		0	},
	{ "S-left",		KEY_SLEFT,		0	},
	{ "S-message",		KEY_SMESSAGE,		0	},
	{ "S-move",		KEY_SMOVE,		0	},
	{ "S-next",		KEY_SNEXT,		0	},
	{ "S-options",		KEY_SOPTIONS,		0	},
	{ "S-previous",		KEY_SPREVIOUS,		0	},
	{ "S-print",		KEY_SPRINT,		0	},
	{ "S-redo",		KEY_SREDO,		0	},
	{ "S-replace",		KEY_SREPLACE,		0	},
	{ "S-resume",		KEY_SRSUME,		0	},
	{ "S-right",		KEY_SRIGHT,		0	},
	{ "S-save",		KEY_SSAVE,		0	},
	{ "S-suspend",		KEY_SSUSPEND,		0	},
	{ "S-undo",		KEY_SUNDO,		0	},
	{ "T",			KEY_IS_CHAR,		84	},
	{ "U",			KEY_IS_CHAR,		85	},
	{ "V",			KEY_IS_CHAR,		86	},
	{ "W",			KEY_IS_CHAR,		87	},
	{ "X",			KEY_IS_CHAR,		88	},
	{ "Y",			KEY_IS_CHAR,		89	},
	{ "Z",			KEY_IS_CHAR,		90	},
	{ "[",			KEY_IS_CHAR,		91	},
	{ "\\",			KEY_IS_CHAR,		92	},
	{ "]",			KEY_IS_CHAR,		93	},
	{ "^",			KEY_IS_CHAR,		94	},
	{ "^A",			KEY_IS_CHAR,		1	},
	{ "^B",			KEY_IS_CHAR,		2	},
	{ "^C",			KEY_IS_CHAR,		3	},
	{ "^D",			KEY_IS_CHAR,		4	},
	{ "^E",			KEY_IS_CHAR,		5	},
	{ "^F",			KEY_IS_CHAR,		6	},
	{ "^G",			KEY_IS_CHAR,		7	},
	{ "^H",			KEY_IS_CHAR,		8	},
	{ "^K",			KEY_IS_CHAR,		11	},
	{ "^L",			KEY_IS_CHAR,		12	},
	{ "^M",			KEY_IS_CHAR,		13	},
	{ "^N",			KEY_IS_CHAR,		14	},
	{ "^O",			KEY_IS_CHAR,		15	},
	{ "^P",			KEY_IS_CHAR,		16	},
	{ "^Q",			KEY_IS_CHAR,		17	},
	{ "^R",			KEY_IS_CHAR,		18	},
	{ "^S",			KEY_IS_CHAR,		19	},
	{ "^T",			KEY_IS_CHAR,		20	},
	{ "^U",			KEY_IS_CHAR,		21	},
	{ "^V",			KEY_IS_CHAR,		22	},
	{ "^W",			KEY_IS_CHAR,		23	},
	{ "^X",			KEY_IS_CHAR,		24	},
	{ "^Y",			KEY_IS_CHAR,		25	},
	{ "^Z",			KEY_IS_CHAR,		26	},
	{ "^\\",		KEY_IS_CHAR,		28	},
	{ "^]",			KEY_IS_CHAR,		29	},
	{ "^^",			KEY_IS_CHAR,		30	},
	{ "^_",			KEY_IS_CHAR,		31	},
	{ "_",			KEY_IS_CHAR,		95	},
	{ "`",			KEY_IS_CHAR,		96	},
	{ "a",			KEY_IS_CHAR,		97	},
	{ "b",			KEY_IS_CHAR,		98	},
	{ "back_tab",		KEY_BTAB,		0	},
	{ "backspace",		KEY_BACKSPACE,		127	}, /* NOTE: both key and ch */
	{ "begin",		KEY_BEG,		0	},
	{ "c",			KEY_IS_CHAR,		99	},
	{ "cancel",		KEY_CANCEL,		0	},
	{ "clear",		KEY_CLEAR,		0	},
	{ "clear_all_tabs",	KEY_CATAB,		0	},
	{ "clear_tab",		KEY_CTAB,		0	},
	{ "close",		KEY_CLOSE,		0	},
	{ "command",		KEY_COMMAND,		0	},
	{ "copy",		KEY_COPY,		0	},
	{ "create",		KEY_CREATE,		0	},
	{ "d",			KEY_IS_CHAR,		100	},
	{ "del_line",		KEY_DL,			0	},
	{ "delete",		KEY_DC,			0	},
	{ "down",		KEY_DOWN,		0	},
	{ "e",			KEY_IS_CHAR,		101	},
	{ "eic",		KEY_EIC,		0	},
	{ "end",		KEY_END,		0	},
	{ "enter",		KEY_IS_CHAR,		10	},
	{ "eol",		KEY_EOL,		0	},
	{ "eos",		KEY_EOS,		0	},
	{ "exit",		KEY_EXIT,		0	},
	{ "f",			KEY_IS_CHAR,		102	},
	{ "find",		KEY_FIND,		0	},
	{ "g",			KEY_IS_CHAR,		103	},
	{ "h",			KEY_IS_CHAR,		104	},
	{ "help",		KEY_HELP,		0	},
	{ "home",		KEY_HOME,		0	},
	{ "i",			KEY_IS_CHAR,		105	},
	{ "ins_line",		KEY_IL,			0	},
	{ "insert",		KEY_IC,			0	},
	{ "j",			KEY_IS_CHAR,		106	},
	{ "k",			KEY_IS_CHAR,		107	},
	{ "l",			KEY_IS_CHAR,		108	},
	{ "left",		KEY_LEFT,		0	},
	{ "lower_left",		KEY_LL,			0	},
	{ "m",			KEY_IS_CHAR,		109	},
	{ "mark",		KEY_MARK,		0	},
	{ "message",		KEY_MESSAGE,		0	},
	{ "move",		KEY_MOVE,		0	},
	{ "n",			KEY_IS_CHAR,		110	},
	{ "next",		KEY_NEXT,		0	},
	{ "o",			KEY_IS_CHAR,		111	},
	{ "open",		KEY_OPEN,		0	},
	{ "options",		KEY_OPTIONS,		0	},
	{ "p",			KEY_IS_CHAR,		112	},
	{ "page_down",		KEY_NPAGE,		0	},
	{ "page_up",		KEY_PPAGE,		0	},
	{ "previous",		KEY_PREVIOUS,		0	},
	{ "print",		KEY_PRINT,		0	},
	{ "q",			KEY_IS_CHAR,		113	},
	{ "r",			KEY_IS_CHAR,		114	},
	{ "redo",		KEY_REDO,		0	},
	{ "reference",		KEY_REFERENCE,		0	},
	{ "refresh",		KEY_REFRESH,		0	},
	{ "replace",		KEY_REPLACE,		0	},
	{ "restart",		KEY_RESTART,		0	},
	{ "resume",		KEY_RESUME,		0	},
	{ "right",		KEY_RIGHT,		0	},
	{ "s",			KEY_IS_CHAR,		115	},
	{ "save",		KEY_SAVE,		0	},
	{ "scroll_b",		KEY_SR,			0	},
	{ "scroll_f",		KEY_SF,			0	},
	{ "select",		KEY_SELECT,		0	},
	{ "send",		KEY_SEND,		0	},
	{ "set_tab",		KEY_STAB,		0	},
	{ "space",		KEY_IS_CHAR,		32	},
	{ "suspend",		KEY_SUSPEND,		0	},
	{ "t",			KEY_IS_CHAR,		116	},
	{ "tab",		KEY_IS_CHAR,		9	},
	{ "u",			KEY_IS_CHAR,		117	},
	{ "undo",		KEY_UNDO,		0	},
	{ "up",			KEY_UP,			0	},
	{ "v",			KEY_IS_CHAR,		118	},
	{ "w",			KEY_IS_CHAR,		119	},
	{ "x",			KEY_IS_CHAR,		120	},
	{ "y",			KEY_IS_CHAR,		121	},
	{ "z",			KEY_IS_CHAR,		122	},
	{ "{",			KEY_IS_CHAR,		123	},
	{ "|",			KEY_IS_CHAR,		124	},
	{ "}",			KEY_IS_CHAR,		125	},
	{ "~",			KEY_IS_CHAR,		126	},
	{ NULL,			0,			0	}
};
/* }}} */

static int find_context(const char *name)
{
	int i;

	for (i = 0; i < NR_CTXS; i++) {
		if (strcmp(name, key_context_names[i]) == 0)
			return i;
	}
	error_msg("invalid context '%s'", name);
	return -1;
}

static const struct key *find_key(const char *name)
{
	int i;

	for (i = 0; key_table[i].name; i++) {
		if (strcmp(name, key_table[i].name) == 0)
			return &key_table[i];
	}
	error_msg("invalid key '%s'", name);
	return NULL;
}

static struct binding *find_binding(enum key_context c, const struct key *k)
{
	struct binding *b = key_bindings[c];

	while (b) {
		if (b->key == k)
			break;
		b = b->next;
	}
	return b;
}

void show_binding(const char *context, const char *key)
{
	const struct key *k;
	const struct binding *b;
	int c;

	c = find_context(context);
	if (c < 0)
		return;

	k = find_key(key);
	if (k == NULL)
		return;

	b = find_binding(c, k);
	if (b == NULL) {
		info_msg("No such binding");
	} else {
		info_msg("bind %s %s %s", context, key, b->cmd);
	}
}

int key_bind(const char *context, const char *key, const char *cmd, int force)
{
	const struct key *k;
	struct binding *b, *ptr, *prev;
	struct command *command;
	int c, size;

	c = find_context(context);
	if (c < 0)
		return -1;

	k = find_key(key);
	if (k == NULL)
		return -1;

	/* check if already bound */
	b = find_binding(c, k);
	if (b) {
		if (!force)
			goto bound;
		key_unbind(context, key, 0);
	}

	if (*cmd == ':')
		cmd++;
	size = strlen(cmd) + 1;

	b = xmalloc(sizeof(struct binding) + size);
	b->key = k;
	b->ctx = c;
	memcpy(b->cmd, cmd, size);

	/* insert keeping sorted by key */
	prev = NULL;
	ptr = key_bindings[c];
	while (ptr) {
		if (strcmp(b->key->name, ptr->key->name) < 0)
			break;
		prev = ptr;
		ptr = ptr->next;
	}
	b->next = ptr;
	if (prev) {
		prev->next = b;
	} else {
		key_bindings[c] = b;
	}
	command = get_command(cmd);
	if (command && !command->bc++)
		help_remove_unbound(command);
	help_add_bound(b);
	return 0;
bound:
	error_msg("key %s already bound in context %s", key, key_context_names[c]);
	return -1;
}

int key_unbind(const char *context, const char *key, int force)
{
	int c;
	const struct key *k;
	struct binding *b, *prev;
	struct command *command;

	c = find_context(context);
	if (c < 0)
		return -1;

	k = find_key(key);
	if (k == NULL)
		return -1;

	prev = NULL;
	b = key_bindings[c];
	while (b) {
		if (b->key == k) {
			if (prev) {
				prev->next = b->next;
			} else {
				key_bindings[c] = b->next;
			}
			command = get_command(b->cmd);
			if (command && !--command->bc)
				help_add_unbound(command);
			help_remove_bound(b);
			free(b);
			return 0;
		}
		prev = b;
		b = b->next;
	}
	if (!force) {
		error_msg("key %s not bound in context %s", key, context);
		return -1;
	}
	return 0;
}

static int handle_key(const struct binding *b, const struct key *k)
{
	while (b) {
		if (b->key == k) {
			run_command(b->cmd);
			return 1;
		}
		b = b->next;
	}
	return 0;
}

static const struct key *ch_to_key(uchar ch)
{
	int i;

	for (i = 0; key_table[i].name; i++) {
		if (key_table[i].ch == ch)
			return &key_table[i];
	}
	return NULL;
}

static const struct key *keycode_to_key(int key)
{
	int i;

	for (i = 0; key_table[i].name; i++) {
		if (key_table[i].key != KEY_IS_CHAR && key_table[i].key == key)
			return &key_table[i];
	}
	return NULL;
}

void normal_mode_ch(uchar ch)
{
	enum key_context c;
	const struct key *k;

	c = view_to_context[cur_view];
	k = ch_to_key(ch);

	if (k == NULL) {
		return;
	}

	/* view-specific ch */
	if (handle_key(key_bindings[c], k))
		return;

	/* common ch */
	if (handle_key(key_bindings[CTX_COMMON], k))
		return;

	/* these can be overridden but have default magic */
	switch (ch) {
	case ':':
		enter_command_mode();
		return;
	case '/':
		enter_search_mode();
		return;
	case '?':
		enter_search_backward_mode();
		return;
	}
}

void normal_mode_key(int key)
{
	enum key_context c = view_to_context[cur_view];
	const struct key *k = keycode_to_key(key);

	if (k == NULL) {
		return;
	}

	/* view-specific key */
	if (handle_key(key_bindings[c], k))
		return;

	/* common key */
	handle_key(key_bindings[CTX_COMMON], k);
}
