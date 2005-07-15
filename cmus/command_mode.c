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

struct history cmd_history;

static char *cmd_history_filename;

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

struct command_mode_option {
	struct list_head node;
	const char *name;
	option_func *func;
	void *func_data;
};

static LIST_HEAD(options_head);
static int nr_options = 0;

static void add_option(const char *name, option_func *func, void *func_data)
{
	struct command_mode_option *opt;

	opt = xnew(struct command_mode_option, 1);
	opt->name = xstrdup(name);
	opt->func = func;
	opt->func_data = func_data;
	list_add_tail(&opt->node, &options_head);
	nr_options++;
}

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
			opt->func(opt->func_data, opt->name, value);
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
	struct list_head *item;
	int len, pos;
	char **tails;

	/* tabexp is resetted */
	len = strlen(str);
	tails = xnew(char *, nr_options + 1);
	pos = 0;
	list_for_each(item, &options_head) {
		struct command_mode_option *opt;
		
		opt = list_entry(item, struct command_mode_option, node);
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
	while (cmd_history.current[pos] == ' ' && pos < cmd_history.current_bpos)
		pos++;
	/* white space */
	s1 = xstrndup(cmd_history.current, pos);
	/* string to expand */
	s2 = xstrndup(cmd_history.current + pos, cmd_history.current_bpos - pos);
	/* tail */
	s3 = xstrdup(cmd_history.current + cmd_history.current_bpos);
	tmp = expand(s2);
	if (tmp) {
		/* s1.tmp.s3 */
		int l1, l2, l3;

		l1 = strlen(s1);
		l2 = strlen(tmp);
		l3 = strlen(s3);
		cmd_history.current_blen = l1 + l2 + l3;
		if (cmd_history.current_blen >= cmd_history.current_size) {
			while (cmd_history.current_blen >= cmd_history.current_size)
				cmd_history.current_size *= 2;
			cmd_history.current = xrenew(char, cmd_history.current, cmd_history.current_size);
		}
		sprintf(cmd_history.current, "%s%s%s", s1, tmp, s3);
		cmd_history.current_bpos = l1 + l2;
		cmd_history.current_cpos = u_strlen(s1) + u_strlen(tmp);
		cmd_history.current_clen = u_strlen(cmd_history.current);
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

void command_mode_ch(uchar ch)
{
	switch (ch) {
	case 0x1B:
		if (cmd_history.current_blen) {
			history_current_save(&cmd_history);
		}
		ui_curses_input_mode = NORMAL_MODE;
		break;
	case 0x0A:
		if (cmd_history.current_blen) {
			run_command(cmd_history.current);
			history_current_save(&cmd_history);
		}
		ui_curses_input_mode = NORMAL_MODE;
		break;
	case 0x09:
		tab_expand();
		break;
	case 127:
		if (history_backspace(&cmd_history))
			ui_curses_input_mode = NORMAL_MODE;
		break;
	default:
		history_insert_ch(&cmd_history, ch);
	}
	if (ch != 0x09)
		reset_tab_expansion();
}

void command_mode_key(int key)
{
	switch (key) {
	case KEY_DC:
		history_delete_ch(&cmd_history);
		break;
	case KEY_BACKSPACE:
		if (history_backspace(&cmd_history))
			ui_curses_input_mode = NORMAL_MODE;
		break;
	case KEY_LEFT:
		history_move_left(&cmd_history);
		break;
	case KEY_RIGHT:
		history_move_right(&cmd_history);
		break;
	case KEY_UP:
		history_search_forward(&cmd_history);
		break;
	case KEY_DOWN:
		history_search_backward(&cmd_history);
		break;
	case KEY_HOME:
		history_move_home(&cmd_history);
		break;
	case KEY_END:
		history_move_end(&cmd_history);
		break;
	default:
		d_print("key = %c (%d)\n", key, key);
	}
	reset_tab_expansion();
}

/* options */

static void set_format_option(void *data, const char *key, const char *value)
{
	char **var = data;

	d_print("%s=%s (old=%s)\n", key, value, *var);
	if (!format_valid(value)) {
		ui_curses_display_error_msg("invalid format string");
		return;
	}
	free(*var);
	*var = xstrdup(value);
	ui_curses_update_view();
	ui_curses_update_titleline();
}

static void set_op(void *data, const char *key, const char *value)
{
	player_set_op(value);
}

static void set_buffer_seconds(void *data, const char *key, const char *value)
{
	long int seconds;

	if (str_to_int(value, &seconds) == -1) {
		ui_curses_display_error_msg("invalid format string");
		return;
	}
	player_set_buffer_seconds(seconds);
}

static void set_status_display_program(void *data, const char *key, const char *value)
{
	free(status_display_program);
	if (strcmp(value, "") == 0) {
		status_display_program = NULL;
		return;
	}
	status_display_program = xstrdup(value);
}

static void set_sort(void *data, const char *key, const char *value)
{
	ui_curses_set_sort(value, 1);
	ui_curses_update_view();
}

static void add_format_option(const char *key, const char *default_value, char **var)
{
	if (sconf_get_str_option(&sconf_head, key, var))
		*var = xstrdup(default_value);
	add_option(key, set_format_option, var);
}

static void set_op_option(void *data, const char *key, const char *value)
{
	d_print("%s=%s\n", key, value);
	BUG_ON(data != NULL);
	player_set_op_option(key, value);
}

static void player_option_callback(void *data, const char *key)
{
	d_print("adding player option %s\n", key);
	add_option(key, set_op_option, data);
}

void commands_init(void)
{
	cmd_history_filename = xstrjoin(cmus_cache_dir, "/ui_curses_cmd_history");
	history_init(&cmd_history, 100);
	history_load(&cmd_history, cmd_history_filename);

	add_format_option("altformat_current",  " %F%= %d ",                    &current_alt_format);
	add_format_option("altformat_playlist", " %f%= %d ",                    &list_win_alt_format);
	add_format_option("altformat_title",    "%f",                           &window_title_alt_format);
	add_format_option("altformat_trackwin", " %f%= %d ",                    &track_win_alt_format);

	player_for_each_op_option(player_option_callback, NULL);
	
	add_format_option("format_current",     " %a - %l - %02n. %t%= %y %d ", &current_format);
	add_format_option("format_playlist",    " %a - %l - %02n. %t%= %y %d ", &list_win_format);
	add_format_option("format_title",       "%a - %l - %t (%y)",            &window_title_format);
	add_format_option("format_trackwin",    " %02n. %t%= %y %d ",           &track_win_format);

	add_option("output_plugin", set_op, NULL);
	add_option("buffer_seconds", set_buffer_seconds, NULL);
	add_option("status_display_program", set_status_display_program, NULL);
	add_option("sort", set_sort, NULL);

	filedir_tabexp = tabexp_file_new(TABEXP_FILE_FLAG_FILES, NULL);
	dir_tabexp = tabexp_file_new(0, NULL);
	option_tabexp = tabexp_new(load_matching_cm_options, NULL);
	cmd_tabexp = tabexp_new(load_matching_commands, NULL);
}

void commands_exit(void)
{
	sconf_set_str_option(&sconf_head, "altformat_current", current_alt_format);
	sconf_set_str_option(&sconf_head, "altformat_playlist", list_win_alt_format);
	sconf_set_str_option(&sconf_head, "altformat_title", window_title_alt_format);
	sconf_set_str_option(&sconf_head, "altformat_trackwin", track_win_alt_format);

	sconf_set_str_option(&sconf_head, "format_current", current_format);
	sconf_set_str_option(&sconf_head, "format_playlist", list_win_format);
	sconf_set_str_option(&sconf_head, "format_title", window_title_format);
	sconf_set_str_option(&sconf_head, "format_trackwin", track_win_format);

	history_save(&cmd_history, cmd_history_filename);
	history_free(&cmd_history);
	free(cmd_history_filename);

	tabexp_file_free(filedir_tabexp);
	tabexp_file_free(dir_tabexp);
	tabexp_free(option_tabexp);
	tabexp_free(cmd_tabexp);
}
