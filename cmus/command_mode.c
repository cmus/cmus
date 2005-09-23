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

#include <command_mode.h>
#include <cmdline.h>
#include <ui_curses.h>
#include <history.h>
#include <tabexp.h>
#include <tabexp_file.h>
#include <browser.h>
#include <player.h>
#include <pl.h>
#include <cmus.h>
#include <xmalloc.h>
#include <xstrjoin.h>
#include <misc.h>
#include <sconf.h>
#include <path.h>
#include <format_print.h>
#include <utils.h>
#include <list.h>
#include <debug.h>

#include <stdlib.h>
#include <curses.h>
#include <ctype.h>
#include <sys/types.h>
#include <pwd.h>

static struct history cmd_history;
static char *cmd_history_filename;
static char *history_search_text = NULL;

static char *get_home_dir(const char *username)
{
	struct passwd *passwd;

	if (username == NULL)
		return xstrdup(home_dir);
	passwd = getpwnam(username);
	if (passwd == NULL)
		return NULL;
	/* don't free passwd */
	return xstrdup(passwd->pw_dir);
}

static char *expand_filename(const char *name)
{
	if (name[0] == '~') {
		char *slash;

		slash = strchr(name, '/');
		if (slash) {
			char *username, *home;

			if (slash - name - 1 > 0) {
				/* ~user/... */
				username = xstrndup(name + 1, slash - name - 1);
			} else {
				/* ~/... */
				username = NULL;
			}
			home = get_home_dir(username);
			free(username);
			if (home) {
				char *expanded;

				expanded = xstrjoin(home, slash);
				free(home);
				return expanded;
			} else {
				return xstrdup(name);
			}
		} else {
			if (name[1] == 0) {
				return xstrdup(home_dir);
			} else {
				char *home;

				home = get_home_dir(name + 1);
				if (home)
					return home;
				return xstrdup(name);
			}
		}
	} else {
		return xstrdup(name);
	}
}

typedef void option_func(void *data, const char *key, const char *value);

static LIST_HEAD(options_head);
static int nr_options = 0;

typedef void cmd_func(char *arg);

static void cmd_set(char *arg)
{
	struct list_head *item;
	char *name, *value = NULL;
	int i;

	name = arg;
	for (i = 0; arg[i]; i++) {
		if (arg[i] == '=') {
			arg[i] = 0;
			value = &arg[i + 1];
			break;
		}
	}
	if (value == NULL) {
		ui_curses_display_error_msg("'=' expected (:set option=value)");
		return;
	}
	list_for_each(item, &options_head) {
		struct command_mode_option *opt;
		
		opt = list_entry(item, struct command_mode_option, node);
		if (strcmp(name, opt->name) == 0) {
			opt->set(opt, value);
			return;
		}
	}
	ui_curses_display_error_msg("unknown option '%s'", name);
}

static void cmd_seek(char *arg)
{
	int seek, seek_mode;
	char *endptr;

	/* Absolute or relative search */
	seek_mode = SEEK_SET;
	if (arg[0] == '+' || arg[0] == '-') {
		seek_mode = SEEK_CUR;
	}

	seek = (int) strtol(arg, &endptr, 10);
	if (!seek && arg == endptr) {
		ui_curses_display_error_msg("invalid seek value");
		return;
	}

	/* Expand M, H to seconds */
	if (endptr && endptr[0] != '\0') {
		endptr[0] = toupper (endptr[0]);
		if (endptr[0] == 'M') {
			seek *= 60;
		} else if (endptr[0] == 'H') {
			seek *= 3600;
		} else {
			ui_curses_display_error_msg("invalid seek modifier");
			return;
		}
	}

	player_seek(seek, seek_mode);
}

static void cmd_add(char *arg)
{
	char *name;
	
	name = expand_filename(arg);
	if (cmus_add(name) == -1)
		ui_curses_display_error_msg("adding '%s' to playlist: %s", name, strerror(errno));
	free(name);
}

static void cmd_enqueue(char *arg)
{
	char *name;
	
	name = expand_filename(arg);
	if (cmus_enqueue(name, 0) == -1)
		ui_curses_display_error_msg("adding '%s' to playqueue: %s", name, strerror(errno));
	free(name);
}

static void cmd_cd(char *arg)
{
	if (arg) {
		char *dir, *absolute;
		
		dir = expand_filename(arg);
		absolute = path_absolute(dir);
		if (chdir(dir) == -1) {
			ui_curses_display_error_msg("could not cd to '%s': %s", dir, strerror(errno));
		} else {
			browser_chdir(absolute);
		}
		free(absolute);
		free(dir);
	} else {
		if (chdir(home_dir) == -1) {
			ui_curses_display_error_msg("could not cd to '%s': %s", home_dir, strerror(errno));
		} else {
			browser_chdir(home_dir);
		}
	}
}

static void cmd_clear(char *arg)
{
	cmus_clear_playlist();
}

static void cmd_save(char *arg)
{
	if (arg) {
		char *tmp;

		free(playlist_filename);
		tmp = expand_filename(arg);
		playlist_filename = path_absolute(tmp);
		free(tmp);
	}
	if (playlist_filename == NULL) {
		playlist_filename = xstrdup(playlist_autosave_filename);
	}
	if (cmus_save_playlist(playlist_filename) == -1)
		ui_curses_display_error_msg("saving playlist '%s': %s", playlist_filename, strerror(errno));
}

static void cmd_load(char *arg)
{
	char *name;
	
	name = expand_filename(arg);
	if (cmus_load_playlist(name) == -1) {
		ui_curses_display_error_msg("loading playlist '%s': %s", name, strerror(errno));
		free(name);
	} else {
		free(playlist_filename);
		playlist_filename = name;
	}
}

static void cmd_reshuffle(char *arg)
{
	pl_reshuffle();
}

struct command {
	const char *name;
	cmd_func *func;

	/* min/max number of arguments */
	int min_args;
	int max_args;

	/* type of tab expansion*/
	enum { TE_NONE, TE_FILEDIR, TE_DIR, TE_OPTION } expand;
};

/* sort by name */
static struct command commands[] = {
	{ "add",        cmd_add,        1, 1, TE_FILEDIR },
	{ "cd",         cmd_cd,         0, 1, TE_DIR },
	{ "clear",      cmd_clear,      0, 0, TE_NONE },
	{ "enqueue",    cmd_enqueue,    1, 1, TE_FILEDIR },
	{ "load",       cmd_load,       1, 1, TE_FILEDIR },
	{ "save",       cmd_save,       0, 1, TE_FILEDIR },
	{ "seek",       cmd_seek,       1, 1, TE_NONE },
	{ "set",        cmd_set,        1, 1, TE_OPTION },
	{ "shuffle",    cmd_reshuffle,  0, 0, TE_NONE },
	{ NULL,         NULL,           0, 0, 0 },
};

static int arg_expand_cmd = -1;
static struct tabexp *filedir_tabexp;
static struct tabexp *dir_tabexp;
static struct tabexp *cmd_tabexp;
static struct tabexp *option_tabexp;

static void load_matching_commands(struct tabexp *tabexp, const char *str)
{
	int i, len, pos;
	char **tails;

	/* tabexp is resetted */
	tails = xnew(char *, sizeof(commands) / sizeof(struct command));
	len = strlen(str);
	pos = 0;
	for (i = 0; commands[i].name; i++) {
		if (strncmp(str, commands[i].name, len) == 0)
			tails[pos++] = xstrdup(commands[i].name + len);
	}
	if (pos > 0) {
		tails[pos] = NULL;
		tabexp->head = xstrdup(str);
		tabexp->tails = tails;
		tabexp->nr_tails = pos;
		tabexp->index = 0;
	} else {
		free(tails);
	}
}

static void load_matching_cm_options(struct tabexp *tabexp, const char *str)
{
	struct command_mode_option *opt;
	int len;
	char **tails;

	/* tabexp is resetted */
	len = strlen(str);
	if (len > 1 && str[len - 1] == '=') {
		/* expand value */
		char *var = xstrndup(str, len - 1);

		list_for_each_entry(opt, &options_head, node) {
			if (strcmp(var, opt->name) == 0) {
				tails = xnew(char *, 1);
				opt->get(opt, &tails[0]);
				tails[1] = NULL;
				tabexp->head = xstrdup(str);
				tabexp->tails = tails;
				tabexp->nr_tails = 1;
				tabexp->index = 0;
				free(var);
				return;
			}
		}
		free(var);
	} else {
		/* expand variable */
		int pos;

		tails = xnew(char *, nr_options + 1);
		pos = 0;
		list_for_each_entry(opt, &options_head, node) {
			if (strncmp(str, opt->name, len) == 0)
				tails[pos++] = xstrdup(opt->name + len);
		}
		if (pos > 0) {
			tails[pos] = NULL;
			tabexp->head = xstrdup(str);
			tabexp->tails = tails;
			tabexp->nr_tails = pos;
			tabexp->index = 0;
		} else {
			free(tails);
		}
	}
}

/* '<command> *[argument]' */
static char *expand(const char *str)
{
	int cmd_len, arg_start, i;

	cmd_len = 0;
	while (str[cmd_len] != ' ' && str[cmd_len])
		cmd_len++;
	arg_start = cmd_len;
	while (str[arg_start] == ' ')
		arg_start++;
	if (str[cmd_len] == 0) {
		/* expand command */
		char *cmd_head = xstrndup(str, cmd_len);
		char *expanded = NULL;

		expanded = tabexp_expand(cmd_tabexp, cmd_head);
		if (expanded) {
			char *s;
			
			s = xstrjoin(expanded, str + cmd_len);
			free(cmd_head);
			free(expanded);
			return s;
		} else {
			free(cmd_head);
			return NULL;
		}
	} else {
		/* expand argument */
		char *expanded = NULL;

		if (arg_expand_cmd == -1) {
			/* not expanded before */

			/* get the command */
			for (i = 0; commands[i].name; i++) {
				if (strncmp(str, commands[i].name, cmd_len) == 0) {
					break;
				}
			}
			if (commands[i].name) {
				if (commands[i + 1].name && strncmp(str, commands[i + 1].name, cmd_len) == 0)
					return NULL;
				arg_expand_cmd = i;
			}
		}
		if (arg_expand_cmd == -1)
			return NULL;
		switch (commands[arg_expand_cmd].expand) {
		case TE_NONE:
			break;
		case TE_FILEDIR:
			expanded = tabexp_expand(filedir_tabexp, str + arg_start);
			break;
		case TE_DIR:
			expanded = tabexp_expand(dir_tabexp, str + arg_start);
			break;
		case TE_OPTION:
			expanded = tabexp_expand(option_tabexp, str + arg_start);
			break;
		}
		if (expanded) {
			char *s;
			int expanded_len;

			expanded_len = strlen(expanded);
			s = xnew(char, arg_start + expanded_len + 1);
			memcpy(s, str, arg_start);
			memcpy(s + arg_start, expanded, expanded_len + 1);
			free(expanded);
			return s;
		} else {
			return NULL;
		}
	}
}

static void tab_expand(void)
{
	char *s1, *s2, *s3, *tmp;
	int pos;

	pos = 0;
	while (cmdline.line[pos] == ' ' && pos < cmdline.bpos)
		pos++;
	/* white space */
	s1 = xstrndup(cmdline.line, pos);
	/* string to expand */
	s2 = xstrndup(cmdline.line + pos, cmdline.bpos - pos);
	/* tail */
	s3 = xstrdup(cmdline.line + cmdline.bpos);
	tmp = expand(s2);
	if (tmp) {
		/* s1.tmp.s3 */
		int l1, l2, l3;

		l1 = strlen(s1);
		l2 = strlen(tmp);
		l3 = strlen(s3);
		cmdline.blen = l1 + l2 + l3;
		if (cmdline.blen >= cmdline.size) {
			while (cmdline.blen >= cmdline.size)
				cmdline.size *= 2;
			cmdline.line = xrenew(char, cmdline.line, cmdline.size);
		}
		sprintf(cmdline.line, "%s%s%s", s1, tmp, s3);
		cmdline.bpos = l1 + l2;
		cmdline.cpos = u_strlen(s1) + u_strlen(tmp);
		cmdline.clen = u_strlen(cmdline.line);
		free(tmp);
	}
	free(s1);
	free(s2);
	free(s3);
}

static void reset_tab_expansion(void)
{
	tabexp_reset(filedir_tabexp);
	tabexp_reset(dir_tabexp);
	tabexp_reset(option_tabexp);
	tabexp_reset(cmd_tabexp);
	arg_expand_cmd = -1;
}

/* FIXME: parse all arguments */
static void run_command(const char *buf)
{
	char *cmd, *arg;
	int cmd_start, cmd_end;
	int arg_start, arg_end;
	int i;

	i = 0;
	while (buf[i] && buf[i] == ' ')
		i++;
	cmd_start = i;
	while (buf[i] && buf[i] != ' ')
		i++;
	cmd_end = i;
	while (buf[i] && buf[i] == ' ')
		i++;
	arg_start = i;
	while (buf[i])
		i++;
	arg_end = i;

	if (cmd_start == cmd_end)
		return;
	cmd = xstrndup(buf + cmd_start, cmd_end - cmd_start);
	if (arg_start == arg_end) {
		arg = NULL;
	} else {
		arg = xstrndup(buf + arg_start, arg_end - arg_start);
	}
	d_print("command: '%s' '%s'\n", cmd, arg);
	i = 0;
	while (1) {
		if (commands[i].name == NULL) {
			ui_curses_display_error_msg("unknown command\n");
			break;
		}
		if (strncmp(cmd, commands[i].name, cmd_end - cmd_start) == 0) {
			if (commands[i + 1].name && strncmp(cmd, commands[i + 1].name, cmd_end - cmd_start) == 0) {
				ui_curses_display_error_msg("ambiguous command\n");
				break;
			}
			d_print("full command name: %s\n", commands[i].name);
			if (commands[i].min_args > 0 && arg == NULL) {
				ui_curses_display_error_msg("not enough arguments\n");
				break;
			}
			if (commands[i].max_args == 0 && arg) {
				ui_curses_display_error_msg("too many arguments\n");
				break;
			}
			commands[i].func(arg);
			break;
		}
		i++;
	}
	free(arg);
	free(cmd);
}

static void reset_history_search(void)
{
	history_reset_search(&cmd_history);
	free(history_search_text);
	history_search_text = NULL;
}

static void backspace(void)
{
	if (cmdline.clen > 0) {
		cmdline_backspace();
	} else {
		ui_curses_input_mode = NORMAL_MODE;
	}
}

void command_mode_ch(uchar ch)
{
	switch (ch) {
	case 0x1B:
		if (cmdline.blen) {
			history_add_line(&cmd_history, cmdline.line);
			cmdline_clear();
		}
		ui_curses_input_mode = NORMAL_MODE;
		break;
	case 0x0A:
		if (cmdline.blen) {
			run_command(cmdline.line);
			history_add_line(&cmd_history, cmdline.line);
			cmdline_clear();
		}
		ui_curses_input_mode = NORMAL_MODE;
		break;
	case 0x09:
		tab_expand();
		break;
	case 127:
		backspace();
		break;
	default:
		cmdline_insert_ch(ch);
	}
	reset_history_search();
	if (ch != 0x09)
		reset_tab_expansion();
}

void command_mode_key(int key)
{
	reset_tab_expansion();
	switch (key) {
	case KEY_DC:
		cmdline_delete_ch();
		break;
	case KEY_BACKSPACE:
		backspace();
		break;
	case KEY_LEFT:
		cmdline_move_left();
		return;
	case KEY_RIGHT:
		cmdline_move_right();
		return;
	case KEY_HOME:
		cmdline_move_home();
		return;
	case KEY_END:
		cmdline_move_end();
		return;
	case KEY_UP:
		{
			const char *s;

			if (history_search_text == NULL)
				history_search_text = xstrdup(cmdline.line);
			s = history_search_forward(&cmd_history, history_search_text);
			if (s)
				cmdline_set_text(s);
		}
		return;
	case KEY_DOWN:
		if (history_search_text) {
			const char *s;
			
			s = history_search_backward(&cmd_history, history_search_text);
			if (s) {
				cmdline_set_text(s);
			} else {
				cmdline_set_text(history_search_text);
			}
		}
		return;
	default:
		d_print("key = %c (%d)\n", key, key);
	}
	reset_history_search();
}

void option_add(const char *name, option_get_func get, option_set_func set, void *data)
{
	struct command_mode_option *opt;
	struct list_head *item;

	opt = xnew(struct command_mode_option, 1);
	opt->name = xstrdup(name);
	opt->get = get;
	opt->set = set;
	opt->data = data;

	item = options_head.next;
	while (item != &options_head) {
		struct command_mode_option *o = container_of(item, struct command_mode_option, node);

		if (strcmp(name, o->name) < 0)
			break;
		item = item->next;
	}
	/* add before item */
	list_add_tail(&opt->node, item);
	nr_options++;
}

void commands_init(void)
{
	cmd_history_filename = xstrjoin(cmus_cache_dir, "/ui_curses_cmd_history");
	history_load(&cmd_history, cmd_history_filename, 2000);

	filedir_tabexp = tabexp_file_new(TABEXP_FILE_FLAG_FILES, NULL);
	dir_tabexp = tabexp_file_new(0, NULL);
	option_tabexp = tabexp_new(load_matching_cm_options, NULL);
	cmd_tabexp = tabexp_new(load_matching_commands, NULL);
}

void commands_exit(void)
{
	history_save(&cmd_history);
	free(cmd_history_filename);

	tabexp_file_free(filedir_tabexp);
	tabexp_file_free(dir_tabexp);
	tabexp_free(option_tabexp);
	tabexp_free(cmd_tabexp);
}
