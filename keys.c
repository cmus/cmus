/*
 * Copyright 2004-2005 Timo Hirvonen
 *
 * keys.[ch] by Frank Terbeck <frank.terbeck@rwth-aachen.de>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <keys.h>
#include <player.h>
#include <misc.h>
#include <prog.h>
#include <cmus.h>
#include <filters.h>
#include <search_mode.h>
#include <play_queue.h>
#include <pl.h>
#include <browser.h>
#include <window.h>
#include <ui_curses.h>
#include <file.h>
#include <xmalloc.h>
#include <xstrjoin.h>
#include <config.h>
#include <command_mode.h>

#include <curses.h>
#include <stdio.h>
#include <ctype.h>

const char * const key_context_names[NR_CTXS + 1] = {
	"browser",
	"common",
	"filters",
	"play_queue",
	"playlist",
	NULL
};

struct binding *key_bindings[NR_CTXS] = { NULL, };

static const enum key_context view_to_context[] = {
	CTX_PLAYLIST,
	CTX_PLAYLIST,
	CTX_PLAYLIST,
	CTX_PLAY_QUEUE,
	CTX_BROWSER,
	CTX_FILTERS
};

#define KEY_IS_CHAR -255

static char *filename;

static void view_lock(void)
{
	if (ui_curses_view < 3) {
		pl_lock();
	} else if (ui_curses_view == PLAY_QUEUE_VIEW) {
		play_queue_lock();
	}
}

static void view_unlock(void)
{
	if (ui_curses_view < 3) {
		pl_unlock();
	} else if (ui_curses_view == PLAY_QUEUE_VIEW) {
		play_queue_unlock();
	}
}

static struct window *current_win(void)
{
	switch (ui_curses_view) {
	case TREE_VIEW:
	case SHUFFLE_VIEW:
	case SORTED_VIEW:
		return playlist.cur_win;
	case PLAY_QUEUE_VIEW:
		return play_queue_win;
	case BROWSER_VIEW:
		return browser_win;
	case FILTERS_VIEW:
	default:
		return filters_win;
	}
}

/* bindable functions {{{ */
static void win_activate_next(void)
{
	if (ui_curses_view == TREE_VIEW)
		pl_toggle_active_window();
}

static void win_bottom(void)
{
	view_lock();
	window_goto_bottom(current_win());
	view_unlock();
}

static void win_down(void)
{
	view_lock();
	window_down(current_win(), 1);
	view_unlock();
}

static void win_page_down(void)
{
	view_lock();
	window_page_down(current_win());
	view_unlock();
}

static void win_page_up(void)
{
	view_lock();
	window_page_up(current_win());
	view_unlock();
}

static void win_top(void)
{
	view_lock();
	window_goto_top(current_win());
	view_unlock();
}

static void win_up(void)
{
	view_lock();
	window_up(current_win(), 1);
	view_unlock();
}

static void play_selected(void)
{
	struct track_info *info;

	info = pl_set_selected();
	if (info) {
		player_play_file(info->filename);
		track_info_unref(info);
	}
}

static int queue_append_cb(void *data, struct track_info *ti)
{
	__play_queue_append(ti);
	return 0;
}

static int queue_prepend_cb(void *data, struct track_info *ti)
{
	__play_queue_prepend(ti);
	return 0;
}

static void queue_append(void)
{
	play_queue_lock();
	pl_lock();
	__pl_for_each_selected(queue_append_cb, NULL, 0);
	window_down(current_win(), 1);
	pl_unlock();
	play_queue_unlock();
}

static void queue_prepend(void)
{
	play_queue_lock();
	pl_lock();
	__pl_for_each_selected(queue_prepend_cb, NULL, 1);
	window_down(current_win(), 1);
	pl_unlock();
	play_queue_unlock();
}

static void search_next_forward(void)
{
	if (search_str) {
		if (!search_next(searchable, search_str, search_direction))
			ui_curses_search_not_found();
	}
}

static void search_next_backward(void)
{
	if (search_str) {
		if (!search_next(searchable, search_str, !search_direction))
			ui_curses_search_not_found();
	}
}
/* }}} */

/* functions {{{ */
static const struct key_function common_functions[] = {
	{ "help",			display_help			},
	{ "next",			cmus_next			},
	{ "pause",			player_pause			},
	{ "play",			player_play			},
	{ "prev",			cmus_prev			},
	{ "quit",			ui_curses_quit			},
	{ "search_next",		search_next_forward		},
	{ "search_prev",		search_next_backward		},
	{ "seek_backward",		cmus_seek_bwd			},
	{ "seek_forward",		cmus_seek_fwd			},
	{ "stop",			player_stop			},
	{ "toggle_continue",		player_toggle_cont		},
	{ "toggle_play_mode",		pl_toggle_play_mode		},
	{ "toggle_playlist_mode",	pl_toggle_playlist_mode		},
	{ "toggle_remaining_time",	ui_curses_toggle_remaining_time	},
	{ "toggle_repeat",		pl_toggle_repeat		},
	{ "view_1",			ui_curses_tree_view		},
	{ "view_2",			ui_curses_shuffle_view		},
	{ "view_3",			ui_curses_sorted_view		},
	{ "view_4",			ui_curses_play_queue_view	},
	{ "view_5",			ui_curses_browser_view		},
	{ "view_6",			ui_curses_filters_view		},
	{ "vol_down",			cmus_vol_down			},
	{ "vol_left_down",		cmus_vol_left_down		},
	{ "vol_left_up",		cmus_vol_left_up		},
	{ "vol_right_down",		cmus_vol_right_down		},
	{ "vol_right_up",		cmus_vol_right_up		},
	{ "vol_up",			cmus_vol_up			},
	{ "win_activate_next",		win_activate_next		},
	{ "win_bottom",			win_bottom			},
	{ "win_down",			win_down			},
	{ "win_page_down",		win_page_down			},
	{ "win_page_up",		win_page_up			},
	{ "win_top",			win_top				},
	{ "win_up",			win_up				},
	{ NULL,				NULL				}
};

static struct key_function playlist_functions[] = {
	{ "expand_artist",	pl_toggle_expand_artist	},
	{ "play_selected",	play_selected		},
	{ "queue_append",	queue_append		},
	{ "queue_prepend",	queue_prepend		},
	{ "remove",		pl_remove_sel		},
	{ "select_current",	pl_sel_current		},
	{ "update",		cmus_update_playlist	},
	{ NULL,			NULL			}
};

static const struct key_function play_queue_functions[] = {
	{ "remove",		play_queue_delete	},
	{ NULL,			NULL			}
};

static const struct key_function browser_functions[] = {
	{ "add",		browser_add			},
	{ "cd_parent",		browser_cd_parent		},
	{ "enter",		browser_enter			},
	{ "queue_append",	browser_queue_append		},
	{ "queue_prepend",	browser_queue_prepend		},
	{ "reload",		browser_reload			},
	{ "remove",		browser_delete			},
	{ "toggle_show_hidden",	browser_toggle_show_hidden	},
	{ NULL,			NULL				}
};

static const struct key_function filters_functions[] = {
	{ "activate",		filters_activate	},
	{ "delete_filter",	filters_delete_filter	},
	{ "toggle_filter",	filters_toggle_filter	},
	{ NULL,			NULL			}
};
/* }}} */

const struct key_function *key_functions[NR_CTXS + 1] = {
	browser_functions,
	common_functions,
	filters_functions,
	play_queue_functions,
	playlist_functions,
	NULL
};

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
	{ ";",			KEY_IS_CHAR,		59	},
	{ "<",			KEY_IS_CHAR,		60	},
	{ "=",			KEY_IS_CHAR,		61	},
	{ ">",			KEY_IS_CHAR,		62	},
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

/*
 * TODO: tables are sorted, do binary search
 */

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

static const struct key_function *find_function(const char *name, enum key_context c)
{
	const struct key_function *functions = key_functions[c];
	int i;

	for (i = 0; functions[i].name; i++) {
		if (strcmp(name, functions[i].name) == 0)
			return &functions[i];
	}
	error_msg("function '%s' not in context %s", name, key_context_names[c]);
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

int key_bind(const char *context, const char *key, const char *func)
{
	int c;
	const struct key *k;
	const struct key_function *f = NULL;
	struct binding *b, *ptr, *prev;

	c = find_context(context);
	if (c < 0)
		return -1;

	k = find_key(key);
	if (k == NULL)
		return -1;

	if (func[0] != ':') {
		f = find_function(func, c);
		if (f == NULL)
			return -1;
	}

	/* check if already bound */
	b = find_binding(c, k);
	if (b)
		goto bound;
	if (c != CTX_COMMON) {
		/* always search CTX_COMMON because same key can't be bound
		 * in CTX_COMMON _and_ any other context at the same time
		 */
		b = find_binding(CTX_COMMON, k);
		if (b) {
			c = CTX_COMMON;
			goto bound;
		}
	}

	if (f == NULL) {
		/* ":command", skip the ':' */
		int size = strlen(func + 1) + 1;

		b = xmalloc(sizeof(struct binding) + size);
		memcpy(b->arg, func + 1, size);
	} else {
		b = xnew(struct binding, 1);
	}
	b->key = k;
	b->func = f;

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
	return 0;
bound:
	error_msg("key %s already bound in context %s", key, key_context_names[c]);
	return -1;
}

int key_unbind(const char *context, const char *key)
{
	enum key_context c;
	const struct key *k;
	struct binding *b, *prev;

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
			free(b);
			return 0;
		}
		prev = b;
		b = b->next;
	}
	error_msg("key %s not bound in context %s", key, context);
	return -1;
}

static const char *get_word(const char *line, char *buf)
{
	int i;

	while (isspace(*line))
		line++;

	i = -1;
	do {
		i++;
		if (i == 32 || line[i] == 0)
			return NULL;
		buf[i] = line[i];
	} while (!isspace(line[i]));
	buf[i] = 0;
	return line + i;
}

static int handle_line(void *data, const char *line)
{
	char context[32];
	char key[32];

	line = get_word(line, context);
	if (line == NULL)
		goto error;

	line = get_word(line, key);
	if (line == NULL)
		goto error;

	while (isspace(*line))
		line++;
	if (*line == 0)
		goto error;

	key_bind(context, key, line);
	return 0;
error:
	error_msg("could not parse keybinding '%s'", line);
	return 0;
}

static int load_keys(const char *file)
{
	return file_for_each_line(file, handle_line, NULL);
}

static int bindings_empty(void)
{
	int i;
	for ( i = 0; i < NR_CTXS; i++ )
		if (key_bindings[i] != NULL)
			return 0;
 	return 1;
}

void keys_init(void)
{
	const char *default_bindings = DATADIR "/cmus/keybindings";
	filename = xstrjoin(cmus_config_dir, "/keybindings");
	if (load_keys(filename)) {
		if (errno == ENOENT) {
			if (load_keys(default_bindings))
				die_errno("error: loading keybindings %s", default_bindings);
		} else {
			die_errno("error: loading keybindings %s", filename);
		}
	}
	if (bindings_empty()) {
		d_print("  bindings are empty! loading defaults!\n");
		if (load_keys(default_bindings))
			warn("error: loading keybindings %s\n", default_bindings);
	}
}

void keys_exit(void)
{
	FILE *f;
	int i;

	f = fopen(filename, "w");
	if (f == NULL) {
		warn("error: creating %s: %s\n", filename, strerror(errno));
		return;
	}
	for (i = 0; i < NR_CTXS; i++) {
		struct binding *b = key_bindings[i];
		const char *name = key_context_names[i];

		while (b) {
			if (b->func != NULL)
				fprintf(f, "%-10s %-20s %s\n", name, b->key->name, b->func->name);
			else
				fprintf(f, "%-10s %-20s :%s\n", name, b->key->name, b->arg);
			b = b->next;
		}
	}
	fclose(f);
}

static int handle_key(const struct binding *b, const struct key *k)
{
	while (b) {
		if (b->key == k) {
			if (b->func != NULL)
				b->func->func();
			else
				run_command(b->arg);
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

	/* you can't redefine these keys */
	switch (ch) {
	case ':':
		ui_curses_command_mode();
		return;
	case '/':
		ui_curses_search_mode();
		return;
	case '?':
		ui_curses_search_backward_mode();
		return;
	}

	c = view_to_context[ui_curses_view];
	k = ch_to_key(ch);

	if (k == NULL) {
		return;
	}

	/* view-specific ch */
	if (handle_key(key_bindings[c], k))
		return;

	/* common ch */
	if (!handle_key(key_bindings[CTX_COMMON], k))
		d_print("key %s not bound in context %s or common\n", k->name, key_context_names[c]);
}

void normal_mode_key(int key)
{
	enum key_context c = view_to_context[ui_curses_view];
	const struct key *k = keycode_to_key(key);

	if (k == NULL) {
		return;
	}

	/* view-specific key */
	if (handle_key(key_bindings[c], k))
		return;

	/* common key */
	if (!handle_key(key_bindings[CTX_COMMON], k))
		d_print("key %s not bound in context %s or common\n", k->name, key_context_names[c]);
}
