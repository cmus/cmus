/*
 * Copyright 2008-2013 Various Authors
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

#include "command_mode.h"
#include "search_mode.h"
#include "cmdline.h"
#include "options.h"
#include "ui_curses.h"
#include "history.h"
#include "tabexp.h"
#include "tabexp_file.h"
#include "browser.h"
#include "filters.h"
#include "player.h"
#include "output.h"
#include "editable.h"
#include "lib.h"
#include "pl.h"
#include "play_queue.h"
#include "cmus.h"
#include "worker.h"
#include "keys.h"
#include "xmalloc.h"
#include "xstrjoin.h"
#include "misc.h"
#include "path.h"
#include "spawn.h"
#include "utils.h"
#include "list.h"
#include "debug.h"
#include "load_dir.h"
#ifdef HAVE_CONFIG
#include "config/datadir.h"
#endif
#include "help.h"
#include "op.h"

#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>

#if defined(__sun__)
#include <ncurses.h>
#else
#include <curses.h>
#endif

static struct history cmd_history;
static char *cmd_history_filename;
static char *history_search_text = NULL;
static int arg_expand_cmd = -1;
static int prev_view = -1;

/* view {{{ */

void view_clear(int view)
{
	switch (view) {
	case TREE_VIEW:
	case SORTED_VIEW:
		worker_remove_jobs(JOB_TYPE_LIB);
		editable_lock();
		editable_clear(&lib_editable);

		/* FIXME: make this optional? */
		lib_clear_store();

		editable_unlock();
		break;
	case PLAYLIST_VIEW:
		worker_remove_jobs(JOB_TYPE_PL);
		editable_lock();
		editable_clear(&pl_editable);
		editable_unlock();
		break;
	case QUEUE_VIEW:
		worker_remove_jobs(JOB_TYPE_QUEUE);
		editable_lock();
		editable_clear(&pq_editable);
		editable_unlock();
		break;
	default:
		info_msg(":clear only works in views 1-4");
	}
}

void view_add(int view, char *arg, int prepend)
{
	char *tmp, *name;
	enum file_type ft;

	tmp = expand_filename(arg);
	ft = cmus_detect_ft(tmp, &name);
	if (ft == FILE_TYPE_INVALID) {
		error_msg("adding '%s': %s", tmp, strerror(errno));
		free(tmp);
		return;
	}
	free(tmp);

	switch (view) {
	case TREE_VIEW:
	case SORTED_VIEW:
		cmus_add(lib_add_track, name, ft, JOB_TYPE_LIB, 0);
		break;
	case PLAYLIST_VIEW:
		cmus_add(pl_add_track, name, ft, JOB_TYPE_PL, 0);
		break;
	case QUEUE_VIEW:
		if (prepend) {
			cmus_add(play_queue_prepend, name, ft, JOB_TYPE_QUEUE, 0);
		} else {
			cmus_add(play_queue_append, name, ft, JOB_TYPE_QUEUE, 0);
		}
		break;
	default:
		info_msg(":add only works in views 1-4");
	}
	free(name);
}

void view_load(int view, char *arg)
{
	char *tmp, *name;
	enum file_type ft;

	tmp = expand_filename(arg);
	ft = cmus_detect_ft(tmp, &name);
	if (ft == FILE_TYPE_INVALID) {
		error_msg("loading '%s': %s", tmp, strerror(errno));
		free(tmp);
		return;
	}
	free(tmp);

	if (ft == FILE_TYPE_FILE)
		ft = FILE_TYPE_PL;
	if (ft != FILE_TYPE_PL) {
		error_msg("loading '%s': not a playlist file", name);
		free(name);
		return;
	}

	switch (view) {
	case TREE_VIEW:
	case SORTED_VIEW:
		worker_remove_jobs(JOB_TYPE_LIB);
		editable_lock();
		editable_clear(&lib_editable);
		editable_unlock();
		cmus_add(lib_add_track, name, FILE_TYPE_PL, JOB_TYPE_LIB, 0);
		free(lib_filename);
		lib_filename = name;
		break;
	case PLAYLIST_VIEW:
		worker_remove_jobs(JOB_TYPE_PL);
		editable_lock();
		editable_clear(&pl_editable);
		editable_unlock();
		cmus_add(pl_add_track, name, FILE_TYPE_PL, JOB_TYPE_PL, 0);
		free(pl_filename);
		pl_filename = name;
		break;
	default:
		info_msg(":load only works in views 1-3");
		free(name);
	}
}

static void do_save(for_each_ti_cb for_each_ti, const char *arg, char **filenamep, save_ti_cb save_ti)
{
	char *filename = *filenamep;

	if (arg) {
		if (strcmp(arg, "-") == 0) {
			filename = (char *) arg;
		} else {
			free(filename);
			filename = xstrdup(arg);
			*filenamep = filename;
		}
	} else if (!filename) {
		error_msg("need a file as argument, no default stored yet");
		return;
	}

	editable_lock();
	if (save_ti(for_each_ti, filename) == -1)
		error_msg("saving '%s': %s", filename, strerror(errno));
	editable_unlock();
}

void view_save(int view, char *arg, int to_stdout, int filtered, int extended)
{
	char **dest;
	save_ti_cb     save_ti         = extended ? cmus_save_ext         : cmus_save;
	for_each_ti_cb lib_for_each_ti = filtered ? lib_for_each_filtered : lib_for_each;

	if (arg) {
		if (to_stdout) {
			arg = xstrdup(arg);
		} else {
			char *tmp = expand_filename(arg);
			arg = path_absolute(tmp);
			free(tmp);
		}
	}

	switch (view) {
	case TREE_VIEW:
	case SORTED_VIEW:
		if (worker_has_job(JOB_TYPE_LIB))
			goto worker_running;
		dest = extended ? &lib_ext_filename : &lib_filename;
		do_save(lib_for_each_ti, arg, dest, save_ti);
		break;
	case PLAYLIST_VIEW:
		if (worker_has_job(JOB_TYPE_PL))
			goto worker_running;
		dest = extended ? &pl_ext_filename : &pl_filename;
		do_save(pl_for_each, arg, dest, save_ti);
		break;
	case QUEUE_VIEW:
		if (worker_has_job(JOB_TYPE_QUEUE))
			goto worker_running;
		dest = extended ? &play_queue_ext_filename : &play_queue_filename;
		do_save(play_queue_for_each, arg, dest, save_ti);
		break;
	default:
		info_msg(":save only works in views 1 & 2 (library) and 3 (playlist)");
	}
	free(arg);
	return;
worker_running:
	error_msg("can't save when tracks are being added");
	free(arg);
}

/* }}} */

/* if only_last != 0, only return the last flag */
static int do_parse_flags(const char **strp, const char *flags, int only_last)
{
	const char *str = *strp;
	int flag = 0;

	if (str == NULL)
		return flag;

	while (*str && (only_last || !flag)) {
		if (*str != '-')
			break;

		// "-"
		if (str[1] == 0)
			break;

		// "--" or "-- "
		if (str[1] == '-' && (str[2] == 0 || str[2] == ' ')) {
			str += 2;
			break;
		}

		// not "-?" or "-? "
		if (str[2] && str[2] != ' ')
			break;

		flag = str[1];
		if (!strchr(flags, flag)) {
			error_msg("invalid option -%c", flag);
			return -1;
		}

		str += 2;

		while (*str == ' ')
			str++;
	}
	while (*str == ' ')
		str++;
	if (*str == 0)
		str = NULL;
	*strp = str;
	return flag;
}

static int parse_flags(const char **strp, const char *flags)
{
	return do_parse_flags(strp, flags, 1);
}

static int parse_one_flag(const char **strp, const char *flags)
{
	return do_parse_flags(strp, flags, 0);
}

/* is str == "...-", but not "...-- -" ? copied from do_parse_flags() */
static int is_stdout_filename(const char *str)
{
	if (!str)
		return 0;

	while (*str) {
		if (*str != '-')
			return 0;
		// "-"
		if (str[1] == 0)
			return 1;
		// "--" or "-- "
		if (str[1] == '-' && (str[2] == 0 || str[2] == ' '))
			return 0;
		// not "-?" or "-? "
		if (str[2] && str[2] != ' ')
			return 0;
		str += 2;
		while (*str == ' ')
			str++;
	}

	return 0;
}

static int flag_to_view(int flag)
{
	switch (flag) {
	case 'l':
	case 'L':
		return TREE_VIEW;
	case 'p':
		return PLAYLIST_VIEW;
	case 'q':
	case 'Q':
		return QUEUE_VIEW;
	default:
		return cur_view;
	}
}

static void cmd_add(char *arg)
{
	int flag = parse_flags((const char **)&arg, "lpqQ");

	if (flag == -1)
		return;
	if (arg == NULL) {
		error_msg("not enough arguments\n");
		return;
	}
	view_add(flag_to_view(flag), arg, flag == 'Q');
}

static void cmd_clear(char *arg)
{
	int flag = parse_flags((const char **)&arg, "lpq");

	if (flag == -1)
		return;
	if (arg) {
		error_msg("too many arguments\n");
		return;
	}
	view_clear(flag_to_view(flag));
}

static void cmd_load(char *arg)
{
	int flag = parse_flags((const char **)&arg, "lp");

	if (flag == -1)
		return;
	if (arg == NULL) {
		error_msg("not enough arguments\n");
		return;
	}
	view_load(flag_to_view(flag), arg);
}

static void cmd_save(char *arg)
{
	int to_stdout = is_stdout_filename(arg);
	int flag = 0, f, extended = 0;

	do {
		f = parse_one_flag((const char **)&arg, "eLlpq");
		if (f == 'e')
			extended = 1;
		else if (f)
			flag = f;
	} while (f > 0);

	if (flag == -1)
		return;
	view_save(flag_to_view(flag), arg, to_stdout, flag == 'L', extended);
}

static void cmd_set(char *arg)
{
	char *value = NULL;
	int i;

	for (i = 0; arg[i]; i++) {
		if (arg[i] == '=') {
			arg[i] = 0;
			value = &arg[i + 1];
			break;
		}
	}
	if (value) {
		option_set(arg, value);
		help_win->changed = 1;
	} else {
		struct cmus_opt *opt;
		char buf[OPTION_MAX_SIZE];

		/* support "set <option>?" */
		i--;
		if (arg[i] == '?')
			arg[i] = 0;

		opt = option_find(arg);
		if (opt) {
			opt->get(opt->id, buf);
			info_msg("setting: '%s=%s'", arg, buf);
		}
	}
}

static void cmd_toggle(char *arg)
{
	struct cmus_opt *opt = option_find(arg);

	if (opt == NULL)
		return;

	if (opt->toggle == NULL) {
		error_msg("%s is not toggle option", opt->name);
		return;
	}
	opt->toggle(opt->id);
	help_win->changed = 1;
}

static int get_number(char *str, char **end)
{
	int val = 0;

	while (*str >= '0' && *str <= '9') {
		val *= 10;
		val += *str++ - '0';
	}
	*end = str;
	return val;
}

static void cmd_seek(char *arg)
{
	int relative = 0;
	int seek = 0, sign = 1, count;

	switch (*arg) {
	case '-':
		sign = -1;
		/* fallthrough */
	case '+':
		relative = 1;
		arg++;
		break;
	}

	count = 0;
	goto inside;

	do {
		int num;
		char *end;

		if (*arg != ':')
			break;
		arg++;
inside:
		num = get_number(arg, &end);
		if (arg == end)
			break;
		arg = end;
		seek = seek * 60 + num;
	} while (++count < 3);

	seek *= sign;
	if (!count)
		goto err;

	if (count == 1) {
		switch (tolower((unsigned char)*arg)) {
		case 'h':
			seek *= 60;
			/* fallthrough */
		case 'm':
			seek *= 60;
			/* fallthrough */
		case 's':
			arg++;
			break;
		}
	}

	if (!*arg) {
		player_seek(seek, relative, 1);
		return;
	}
err:
	error_msg("expecting one argument: [+-]INTEGER[mh] or [+-]H:MM:SS");
}

static void cmd_factivate(char *arg)
{
	editable_lock();
	filters_activate_names(arg);
	editable_unlock();
}

static void cmd_live_filter(char *arg)
{
	editable_lock();
	filters_set_live(arg);
	editable_unlock();
}

static void cmd_filter(char *arg)
{
	editable_lock();
	filters_set_anonymous(arg);
	editable_unlock();
}

static void cmd_fset(char *arg)
{
	filters_set_filter(arg);
}

static void cmd_help(char *arg)
{
	info_msg("To get started with cmus, read cmus-tutorial(7) and cmus(1) man pages");
}

static void cmd_invert(char *arg)
{
	editable_lock();
	switch (cur_view) {
	case SORTED_VIEW:
		editable_invert_marks(&lib_editable);
		break;
	case PLAYLIST_VIEW:
		editable_invert_marks(&pl_editable);
		break;
	case QUEUE_VIEW:
		editable_invert_marks(&pq_editable);
		break;
	default:
		info_msg(":invert only works in views 2-4");
	}
	editable_unlock();
}

static void cmd_mark(char *arg)
{
	editable_lock();
	switch (cur_view) {
	case SORTED_VIEW:
		editable_mark(&lib_editable, arg);
		break;
	case PLAYLIST_VIEW:
		editable_mark(&pl_editable, arg);
		break;
	case QUEUE_VIEW:
		editable_mark(&pq_editable, arg);
		break;
	default:
		info_msg(":mark only works in views 2-4");
	}
	editable_unlock();
}

static void cmd_unmark(char *arg)
{
	editable_lock();
	switch (cur_view) {
	case SORTED_VIEW:
		editable_unmark(&lib_editable);
		break;
	case PLAYLIST_VIEW:
		editable_unmark(&pl_editable);
		break;
	case QUEUE_VIEW:
		editable_unmark(&pq_editable);
		break;
	default:
		info_msg(":unmark only works in views 2-4");
	}
	editable_unlock();
}

static void cmd_update_cache(char *arg)
{
	int flag = parse_flags((const char **)&arg, "f");
	cmus_update_cache(flag == 'f');
}

static void cmd_cd(char *arg)
{
	if (arg) {
		char *dir, *absolute;

		dir = expand_filename(arg);
		absolute = path_absolute(dir);
		if (chdir(dir) == -1) {
			error_msg("could not cd to '%s': %s", dir, strerror(errno));
		} else {
			browser_chdir(absolute);
		}
		free(absolute);
		free(dir);
	} else {
		if (chdir(home_dir) == -1) {
			error_msg("could not cd to '%s': %s", home_dir, strerror(errno));
		} else {
			browser_chdir(home_dir);
		}
	}
}

static void cmd_bind(char *arg)
{
	int flag = parse_flags((const char **)&arg, "f");
	char *key, *func;

	if (flag == -1)
		return;

	if (arg == NULL)
		goto err;

	key = strchr(arg, ' ');
	if (key == NULL)
		goto err;
	*key++ = 0;
	while (*key == ' ')
		key++;

	func = strchr(key, ' ');
	if (func == NULL)
		goto err;
	*func++ = 0;
	while (*func == ' ')
		func++;
	if (*func == 0)
		goto err;

	key_bind(arg, key, func, flag == 'f');
	return;
err:
	error_msg("expecting 3 arguments (context, key and function)\n");
}

static void cmd_unbind(char *arg)
{
	int flag = parse_flags((const char **)&arg, "f");
	char *key;

	if (flag == -1)
		return;

	if (arg == NULL)
		goto err;

	key = strchr(arg, ' ');
	if (key == NULL)
		goto err;
	*key++ = 0;
	while (*key == ' ')
		key++;
	if (*key == 0)
		goto err;

	/* FIXME: remove spaces at end */

	key_unbind(arg, key, flag == 'f');
	return;
err:
	error_msg("expecting 2 arguments (context and key)\n");
}

static void cmd_showbind(char *arg)
{
	char *key;

	key = strchr(arg, ' ');
	if (key == NULL)
		goto err;
	*key++ = 0;
	while (*key == ' ')
		key++;
	if (*key == 0)
		goto err;

	/* FIXME: remove spaces at end */

	show_binding(arg, key);
	return;
err:
	error_msg("expecting 2 arguments (context and key)\n");
}

static void cmd_quit(char *arg)
{
	int flag = parse_flags((const char **)&arg, "i");
	if (!worker_has_job(JOB_TYPE_ANY)) {
		if (flag != 'i' || yes_no_query("Quit cmus? [y/N]"))
			cmus_running = 0;
	} else {
		if (yes_no_query("Tracks are being added. Quit and truncate playlist(s)? [y/N]"))
			cmus_running = 0;
	}
}

static void cmd_reshuffle(char *arg)
{
	editable_lock();
	lib_reshuffle();
	pl_reshuffle();
	editable_unlock();
}

static void cmd_source(char *arg)
{
	char *filename = expand_filename(arg);

	if (source_file(filename) == -1)
		error_msg("sourcing %s: %s", filename, strerror(errno));
	free(filename);
}

static void cmd_colorscheme(char *arg)
{
	char filename[512];

	snprintf(filename, sizeof(filename), "%s/%s.theme", cmus_config_dir, arg);
	if (source_file(filename) == -1) {
		snprintf(filename, sizeof(filename), DATADIR "/cmus/%s.theme", arg);
		if (source_file(filename) == -1)
			error_msg("sourcing %s: %s", filename, strerror(errno));
	}
}

/*
 * \" inside double-quotes becomes "
 * \\ inside double-quotes becomes \
 */
static char *parse_quoted(const char **strp)
{
	const char *str = *strp;
	const char *start;
	char *ret, *dst;

	str++;
	start = str;
	while (1) {
		int c = *str++;

		if (c == 0)
			goto error;
		if (c == '"')
			break;
		if (c == '\\') {
			if (*str++ == 0)
				goto error;
		}
	}
	*strp = str;
	ret = xnew(char, str - start);
	str = start;
	dst = ret;
	while (1) {
		int c = *str++;

		if (c == '"')
			break;
		if (c == '\\') {
			c = *str++;
			if (c != '"' && c != '\\')
				*dst++ = '\\';
		}
		*dst++ = c;
	}
	*dst = 0;
	return ret;
error:
	error_msg("`\"' expected");
	return NULL;
}

static char *parse_escaped(const char **strp)
{
	const char *str = *strp;
	const char *start;
	char *ret, *dst;

	start = str;
	while (1) {
		int c = *str;

		if (c == 0 || c == ' ' || c == '\'' || c == '"')
			break;

		str++;
		if (c == '\\') {
			c = *str;
			if (c == 0)
				break;
			str++;
		}
	}
	*strp = str;
	ret = xnew(char, str - start + 1);
	str = start;
	dst = ret;
	while (1) {
		int c = *str;

		if (c == 0 || c == ' ' || c == '\'' || c == '"')
			break;

		str++;
		if (c == '\\') {
			c = *str;
			if (c == 0) {
				*dst++ = '\\';
				break;
			}
			str++;
		}
		*dst++ = c;
	}
	*dst = 0;
	return ret;
}

static char *parse_one(const char **strp)
{
	const char *str = *strp;
	char *ret = NULL;

	while (1) {
		char *part = NULL;
		int c = *str;

		if (!c || c == ' ')
			break;
		if (c == '"') {
			part = parse_quoted(&str);
			if (part == NULL)
				goto error;
		} else if (c == '\'') {
			/* backslashes are normal chars inside single-quotes */
			const char *end;

			str++;
			end = strchr(str, '\'');
			if (end == NULL)
				goto sq_missing;
			part = xstrndup(str, end - str);
			str = end + 1;
		} else {
			part = parse_escaped(&str);
		}

		if (ret == NULL) {
			ret = part;
		} else {
			char *tmp = xstrjoin(ret, part);
			free(ret);
			ret = tmp;
		}
		free(part);
	}
	*strp = str;
	return ret;
sq_missing:
	error_msg("`'' expected");
error:
	free(ret);
	return NULL;
}

static char **parse_cmd(const char *cmd, int *args_idx, int *ac)
{
	char **av = NULL;
	int nr = 0;
	int alloc = 0;

	while (*cmd) {
		char *arg;

		/* there can't be spaces at start of command
		 * and there is at least one argument */
		if (cmd[0] == '{' && cmd[1] == '}' && (cmd[2] == ' ' || cmd[2] == 0)) {
			/* {} is replaced with file arguments */
			if (*args_idx != -1)
				goto only_once_please;
			*args_idx = nr;
			cmd += 2;
			goto skip_spaces;
		} else {
			arg = parse_one(&cmd);
			if (arg == NULL)
				goto error;
		}

		if (nr == alloc) {
			alloc = alloc ? alloc * 2 : 4;
			av = xrenew(char *, av, alloc + 1);
		}
		av[nr++] = arg;
skip_spaces:
		while (*cmd == ' ')
			cmd++;
	}
	av[nr] = NULL;
	*ac = nr;
	return av;
only_once_please:
	error_msg("{} can be used only once");
error:
	while (nr > 0)
		free(av[--nr]);
	free(av);
	return NULL;
}

struct track_info_selection {
	struct track_info **tis;
	int tis_alloc;
	int tis_nr;
};

static int add_ti(void *data, struct track_info *ti)
{
	struct track_info_selection *sel = data;
	if (sel->tis_nr == sel->tis_alloc) {
		sel->tis_alloc = sel->tis_alloc ? sel->tis_alloc * 2 : 8;
		sel->tis = xrenew(struct track_info *, sel->tis, sel->tis_alloc);
	}
	track_info_ref(ti);
	sel->tis[sel->tis_nr++] = ti;
	return 0;
}

static void cmd_run(char *arg)
{
	char **av, **argv;
	int ac, argc, i, run, files_idx = -1;
	struct track_info_selection sel = { .tis = NULL };

	if (cur_view > QUEUE_VIEW) {
		info_msg("Command execution is supported only in views 1-4");
		return;
	}

	av = parse_cmd(arg, &files_idx, &ac);
	if (av == NULL) {
		return;
	}

	/* collect selected files (struct track_info) */
	editable_lock();
	switch (cur_view) {
	case TREE_VIEW:
		__tree_for_each_sel(add_ti, &sel, 0);
		break;
	case SORTED_VIEW:
		__editable_for_each_sel(&lib_editable, add_ti, &sel, 0);
		break;
	case PLAYLIST_VIEW:
		__editable_for_each_sel(&pl_editable, add_ti, &sel, 0);
		break;
	case QUEUE_VIEW:
		__editable_for_each_sel(&pq_editable, add_ti, &sel, 0);
		break;
	}
	editable_unlock();

	if (sel.tis_nr == 0) {
		/* no files selected, do nothing */
		free_str_array(av);
		return;
	}
	sel.tis[sel.tis_nr] = NULL;

	/* build argv */
	argv = xnew(char *, ac + sel.tis_nr + 1);
	argc = 0;
	if (files_idx == -1) {
		/* add selected files after rest of the args */
		for (i = 0; i < ac; i++)
			argv[argc++] = av[i];
		for (i = 0; i < sel.tis_nr; i++)
			argv[argc++] = sel.tis[i]->filename;
	} else {
		for (i = 0; i < files_idx; i++)
			argv[argc++] = av[i];
		for (i = 0; i < sel.tis_nr; i++)
			argv[argc++] = sel.tis[i]->filename;
		for (i = files_idx; i < ac; i++)
			argv[argc++] = av[i];
	}
	argv[argc] = NULL;

	for (i = 0; argv[i]; i++)
		d_print("ARG: '%s'\n", argv[i]);

	run = 1;
	if (confirm_run && (sel.tis_nr > 1 || strcmp(argv[0], "rm") == 0)) {
		if (!yes_no_query("Execute %s for the %d selected files? [y/N]", arg, sel.tis_nr)) {
			info_msg("Aborted");
			run = 0;
		}
	}
	if (run) {
		int status;

		if (spawn(argv, &status, 1)) {
			error_msg("executing %s: %s", argv[0], strerror(errno));
		} else {
			if (WIFEXITED(status)) {
				int rc = WEXITSTATUS(status);

				if (rc)
					error_msg("%s returned %d", argv[0], rc);
			}
			if (WIFSIGNALED(status))
				error_msg("%s received signal %d", argv[0], WTERMSIG(status));

			switch (cur_view) {
			case TREE_VIEW:
			case SORTED_VIEW:
				/* this must be done before sel.tis are unreffed */
				free_str_array(av);
				free(argv);

				/* remove non-existed files, update tags for changed files */
				cmus_update_tis(sel.tis, sel.tis_nr, 0);

				/* we don't own sel.tis anymore! */
				return;
			}
		}
	}
	free_str_array(av);
	free(argv);
	for (i = 0; sel.tis[i]; i++)
		track_info_unref(sel.tis[i]);
	free(sel.tis);
}

static void cmd_shell(char *arg)
{
	const char * const argv[] = { "sh", "-c", arg, NULL };

	if (spawn((char **) argv, NULL, 0))
		error_msg("executing '%s': %s", arg, strerror(errno));
}

static int get_one_ti(void *data, struct track_info *ti)
{
	struct track_info **sel_ti = data;

	track_info_ref(ti);
	*sel_ti = ti;
	/* stop the for each loop, we need only the first selected track */
	return 1;
}

static void cmd_echo(char *arg)
{
	struct track_info *sel_ti;
	char *ptr = arg;

	while (1) {
		ptr = strchr(ptr, '{');
		if (ptr == NULL)
			break;
		if (ptr[1] == '}')
			break;
		ptr++;
	}

	if (ptr == NULL) {
		info_msg("%s", arg);
		return;
	}

	if (cur_view > QUEUE_VIEW) {
		info_msg("echo with {} in its arguments is supported only in views 1-4");
		return;
	}

	*ptr = 0;
	ptr += 2;

	/* get only the first selected track */
	sel_ti = NULL;

	editable_lock();
	switch (cur_view) {
	case TREE_VIEW:
		__tree_for_each_sel(get_one_ti, &sel_ti, 0);
		break;
	case SORTED_VIEW:
		__editable_for_each_sel(&lib_editable, get_one_ti, &sel_ti, 0);
		break;
	case PLAYLIST_VIEW:
		__editable_for_each_sel(&pl_editable, get_one_ti, &sel_ti, 0);
		break;
	case QUEUE_VIEW:
		__editable_for_each_sel(&pq_editable, get_one_ti, &sel_ti, 0);
		break;
	}
	editable_unlock();

	if (sel_ti == NULL)
		return;

	info_msg("%s%s%s", arg, sel_ti->filename, ptr);
	track_info_unref(sel_ti);
}

#define VF_RELATIVE	0x01
#define VF_PERCENTAGE	0x02

static int parse_vol_arg(const char *arg, int *value, unsigned int *flags)
{
	unsigned int f = 0;
	int ch, val = 0, digits = 0, sign = 1;

	if (*arg == '-') {
		arg++;
		f |= VF_RELATIVE;
		sign = -1;
	} else if (*arg == '+') {
		arg++;
		f |= VF_RELATIVE;
	}

	while (1) {
		ch = *arg++;
		if (ch < '0' || ch > '9')
			break;
		val *= 10;
		val += ch - '0';
		digits++;
	}
	if (digits == 0)
		goto err;

	if (ch == '%') {
		f |= VF_PERCENTAGE;
		ch = *arg;
	}
	if (ch)
		goto err;

	*value = sign * val;
	*flags = f;
	return 0;
err:
	return -1;
}

static int calc_vol(int val, int old, int max_vol, unsigned int flags)
{
	if (flags & VF_RELATIVE) {
		if (flags & VF_PERCENTAGE)
			val = scale_from_percentage(val, max_vol);
		val += old;
	} else if (flags & VF_PERCENTAGE) {
		val = scale_from_percentage(val, max_vol);
	}
	return clamp(val, 0, max_vol);
}

/*
 * :vol value [value]
 *
 * where value is [-+]?[0-9]+%?
 */
static void cmd_vol(char *arg)
{
	char **values = get_words(arg);
	unsigned int lf, rf;
	int l, r;

	if (values[1] && values[2])
		goto err;

	if (parse_vol_arg(values[0], &l, &lf))
		goto err;

	r = l;
	rf = lf;
	if (values[1] && parse_vol_arg(values[1], &r, &rf))
		goto err;

	free_str_array(values);

	if (soft_vol) {
		l = calc_vol(l, soft_vol_l, 100, lf);
		r = calc_vol(r, soft_vol_r, 100, rf);
		player_set_soft_volume(l, r);
	} else {
		int rc;
		mixer_read_volume();
		l = calc_vol(l, volume_l, volume_max, lf);
		r = calc_vol(r, volume_r, volume_max, rf);
		rc = mixer_set_volume(l, r);
		if (rc != OP_ERROR_SUCCESS) {
			char *msg = op_get_error_msg(rc, "can't change volume");
			error_msg("%s", msg);
			free(msg);
		}
		mixer_read_volume();
	}
	update_statusline();
	return;
err:
	free_str_array(values);
	error_msg("expecting 1 or 2 arguments (total or L and R volumes [+-]INTEGER[%%])\n");
}

static void cmd_prev_view(char *arg)
{
	int tmp;
	if (prev_view >= 0) {
		tmp = cur_view;
		set_view(prev_view);
		prev_view = tmp;
	}
}

static void cmd_view(char *arg)
{
	int view;

	if (parse_enum(arg, 1, NR_VIEWS, view_names, &view) && (view - 1) != cur_view) {
		prev_view = cur_view;
		set_view(view - 1);
	}
}

static void cmd_push(char *arg)
{
	cmdline_set_text(arg);
	enter_command_mode();
}

static void cmd_p_next(char *arg)
{
	cmus_next();
}

static void cmd_p_pause(char *arg)
{
	player_pause();
}

static void cmd_p_play(char *arg)
{
	if (arg) {
		cmus_play_file(arg);
	} else {
		player_play();
	}
}

static void cmd_p_prev(char *arg)
{
	if (rewind_offset < 0 || player_info.pos < rewind_offset) {
		cmus_prev();
	} else {
		player_play();
	}
}

static void cmd_p_stop(char *arg)
{
	player_stop();
}

static void cmd_search_next(char *arg)
{
	if (search_str) {
		if (!search_next(searchable, search_str, search_direction))
			search_not_found();
	}
}

static void cmd_search_prev(char *arg)
{
	if (search_str) {
		if (!search_next(searchable, search_str, !search_direction))
			search_not_found();
	}
}

static int sorted_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse)
{
	return editable_for_each_sel(&lib_editable, cb, data, reverse);
}

static int pl_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse)
{
	return editable_for_each_sel(&pl_editable, cb, data, reverse);
}

static int pq_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse)
{
	return editable_for_each_sel(&pq_editable, cb, data, reverse);
}

static for_each_sel_ti_cb view_for_each_sel[4] = {
	tree_for_each_sel,
	sorted_for_each_sel,
	pl_for_each_sel,
	pq_for_each_sel
};

/* wrapper for add_ti_cb, (void *) can't store function pointers */
struct wrapper_cb_data {
	add_ti_cb cb;
};

/* wrapper for void lib_add_track(struct track_info *) etc. */
static int wrapper_cb(void *data, struct track_info *ti)
{
	struct wrapper_cb_data *add = data;

	add->cb(ti);
	return 0;
}

static void add_from_browser(add_ti_cb add, int job_type)
{
	char *sel = browser_get_sel();

	if (sel) {
		enum file_type ft;
		char *ret;

		if (ends_with(sel, "/../") || ends_with(sel, "/..")) {
			info_msg("For convenience, you can not add \"..\" directory from the browser view");
			free(sel);
			return;
		}

		ft = cmus_detect_ft(sel, &ret);
		if (ft != FILE_TYPE_INVALID) {
			cmus_add(add, ret, ft, job_type, 0);
			window_down(browser_win, 1);
		}
		free(ret);
		free(sel);
	}
}

static void cmd_win_add_l(char *arg)
{
	if (cur_view == TREE_VIEW || cur_view == SORTED_VIEW)
		return;

	if (cur_view <= QUEUE_VIEW) {
		struct wrapper_cb_data add = { lib_add_track };
		editable_lock();
		view_for_each_sel[cur_view](wrapper_cb, &add, 0);
		editable_unlock();
	} else if (cur_view == BROWSER_VIEW) {
		add_from_browser(lib_add_track, JOB_TYPE_LIB);
	}
}

static void cmd_win_add_p(char *arg)
{
	/* could allow adding dups? */
	if (cur_view == PLAYLIST_VIEW)
		return;

	if (cur_view <= QUEUE_VIEW) {
		struct wrapper_cb_data add = { pl_add_track };
		editable_lock();
		view_for_each_sel[cur_view](wrapper_cb, &add, 0);
		editable_unlock();
	} else if (cur_view == BROWSER_VIEW) {
		add_from_browser(pl_add_track, JOB_TYPE_PL);
	}
}

static void cmd_win_add_Q(char *arg)
{
	if (cur_view == QUEUE_VIEW)
		return;

	if (cur_view <= QUEUE_VIEW) {
		struct wrapper_cb_data add = { play_queue_prepend };
		editable_lock();
		view_for_each_sel[cur_view](wrapper_cb, &add, 1);
		editable_unlock();
	} else if (cur_view == BROWSER_VIEW) {
		add_from_browser(play_queue_prepend, JOB_TYPE_QUEUE);
	}
}

static void cmd_win_add_q(char *arg)
{
	if (cur_view == QUEUE_VIEW)
		return;

	if (cur_view <= QUEUE_VIEW) {
		struct wrapper_cb_data add = { play_queue_append };
		editable_lock();
		view_for_each_sel[cur_view](wrapper_cb, &add, 0);
		editable_unlock();
	} else if (cur_view == BROWSER_VIEW) {
		add_from_browser(play_queue_append, JOB_TYPE_QUEUE);
	}
}

static void cmd_win_activate(char *arg)
{
	struct track_info *info = NULL;

	editable_lock();
	switch (cur_view) {
	case TREE_VIEW:
		info = tree_set_selected();
		break;
	case SORTED_VIEW:
		info = sorted_set_selected();
		break;
	case PLAYLIST_VIEW:
		info = pl_set_selected();
		break;
	case QUEUE_VIEW:
		break;
	case BROWSER_VIEW:
		browser_enter();
		break;
	case FILTERS_VIEW:
		filters_activate();
		break;
	case HELP_VIEW:
		help_select();
		break;
	}
	editable_unlock();

	if (info) {
		/* update lib/pl mode */
		if (cur_view < 2)
			play_library = 1;
		if (cur_view == 2)
			play_library = 0;

		player_play_file(info);
	}
}

static void cmd_win_mv_after(char *arg)
{
	editable_lock();
	switch (cur_view) {
	case TREE_VIEW:
		break;
	case SORTED_VIEW:
		editable_move_after(&lib_editable);
		break;
	case PLAYLIST_VIEW:
		editable_move_after(&pl_editable);
		break;
	case QUEUE_VIEW:
		editable_move_after(&pq_editable);
		break;
	case BROWSER_VIEW:
		break;
	case FILTERS_VIEW:
		break;
	case HELP_VIEW:
		break;
	}
	editable_unlock();
}

static void cmd_win_mv_before(char *arg)
{
	editable_lock();
	switch (cur_view) {
	case TREE_VIEW:
		break;
	case SORTED_VIEW:
		editable_move_before(&lib_editable);
		break;
	case PLAYLIST_VIEW:
		editable_move_before(&pl_editable);
		break;
	case QUEUE_VIEW:
		editable_move_before(&pq_editable);
		break;
	case BROWSER_VIEW:
		break;
	case FILTERS_VIEW:
		break;
	case HELP_VIEW:
		break;
	}
	editable_unlock();
}

static void cmd_win_remove(char *arg)
{
	editable_lock();
	switch (cur_view) {
	case TREE_VIEW:
		tree_remove_sel();
		break;
	case SORTED_VIEW:
		editable_remove_sel(&lib_editable);
		break;
	case PLAYLIST_VIEW:
		editable_remove_sel(&pl_editable);
		break;
	case QUEUE_VIEW:
		editable_remove_sel(&pq_editable);
		break;
	case BROWSER_VIEW:
		browser_delete();
		break;
	case FILTERS_VIEW:
		filters_delete_filter();
		break;
	case HELP_VIEW:
		help_remove();
		break;
	}
	editable_unlock();
}

static void cmd_win_sel_cur(char *arg)
{
	editable_lock();
	switch (cur_view) {
	case TREE_VIEW:
		tree_sel_current();
		break;
	case SORTED_VIEW:
		sorted_sel_current();
		break;
	case PLAYLIST_VIEW:
		pl_sel_current();
		break;
	case QUEUE_VIEW:
		break;
	case BROWSER_VIEW:
		break;
	case FILTERS_VIEW:
		break;
	case HELP_VIEW:
		break;
	}
	editable_unlock();
}

static void cmd_win_toggle(char *arg)
{
	switch (cur_view) {
	case TREE_VIEW:
		editable_lock();
		tree_toggle_expand_artist();
		editable_unlock();
		break;
	case SORTED_VIEW:
		editable_lock();
		editable_toggle_mark(&lib_editable);
		editable_unlock();
		break;
	case PLAYLIST_VIEW:
		editable_lock();
		editable_toggle_mark(&pl_editable);
		editable_unlock();
		break;
	case QUEUE_VIEW:
		editable_lock();
		editable_toggle_mark(&pq_editable);
		editable_unlock();
		break;
	case BROWSER_VIEW:
		break;
	case FILTERS_VIEW:
		filters_toggle_filter();
		break;
	case HELP_VIEW:
		help_toggle();
		break;
	}
}

static struct window *current_win(void)
{
	switch (cur_view) {
	case TREE_VIEW:
		return lib_cur_win;
	case SORTED_VIEW:
		return lib_editable.win;
	case PLAYLIST_VIEW:
		return pl_editable.win;
	case QUEUE_VIEW:
		return pq_editable.win;
	case BROWSER_VIEW:
		return browser_win;
	case HELP_VIEW:
		return help_win;
	case FILTERS_VIEW:
	default:
		return filters_win;
	}
}

static void cmd_win_bottom(char *arg)
{
	editable_lock();
	window_goto_bottom(current_win());
	editable_unlock();
}

static void cmd_win_down(char *arg)
{
	editable_lock();
	window_down(current_win(), 1);
	editable_unlock();
}

static void cmd_win_next(char *arg)
{
	if (cur_view == TREE_VIEW) {
		editable_lock();
		tree_toggle_active_window();
		editable_unlock();
	}
}

static void cmd_win_pg_down(char *arg)
{
	editable_lock();
	window_page_down(current_win());
	editable_unlock();
}

static void cmd_win_pg_up(char *arg)
{
	editable_lock();
	window_page_up(current_win());
	editable_unlock();
}

static void cmd_win_pg_top(char *arg)
{
	editable_lock();
	window_page_top(current_win());
	editable_unlock();
}

static void cmd_win_pg_bottom(char *arg)
{
	editable_lock();
	window_page_bottom(current_win());
	editable_unlock();
}

static void cmd_win_pg_middle(char *arg)
{
	editable_lock();
	window_page_middle(current_win());
	editable_unlock();
}

static void cmd_win_update_cache(char *arg)
{
	struct track_info_selection sel = { .tis = NULL };
	int flag = parse_flags((const char **)&arg, "f");

	if (cur_view != TREE_VIEW && cur_view != SORTED_VIEW)
		return;

	editable_lock();
	view_for_each_sel[cur_view](add_ti, &sel, 0);
	editable_unlock();
	if (sel.tis_nr == 0)
		return;
	sel.tis[sel.tis_nr] = NULL;
	cmus_update_tis(sel.tis, sel.tis_nr, flag == 'f');
}

static void cmd_win_top(char *arg)
{
	editable_lock();
	window_goto_top(current_win());
	editable_unlock();
}

static void cmd_win_up(char *arg)
{
	editable_lock();
	window_up(current_win(), 1);
	editable_unlock();
}

static void cmd_win_update(char *arg)
{
	switch (cur_view) {
	case TREE_VIEW:
	case SORTED_VIEW:
		cmus_update_lib();
		break;
	case BROWSER_VIEW:
		browser_reload();
		break;
	}
}

static void cmd_browser_up(char *arg)
{
	browser_up();
}

static void cmd_refresh(char *arg)
{
	clearok(curscr, TRUE);
	refresh();
}

static int cmp_intp(const void *ap, const void *bp)
{
	int a = *(int *)ap;
	int b = *(int *)bp;
	return a - b;
}

static int *rand_array(int size, int nmax)
{
	int *r = xnew(int, size + 1);
	int i, offset = 0;
	int count = size;

	if (count > nmax / 2) {
		/*
		 * Imagine that there are 1000 tracks in library and we want to
		 * add 998 random tracks to queue.  After we have added 997
		 * random numbers to the array it would be quite hard to find a
		 * random number that isn't already in the array (3/1000
		 * probability).
		 *
		 * So we invert the logic:
		 *
		 * Find two (1000 - 998) random numbers in 0..999 range and put
		 * them at end of the array.  Sort the numbers and then fill
		 * the array starting at index 0 with incrementing values that
		 * are not in the set of random numbers.
		 */
		count = nmax - count;
		offset = size - count;
	}

	for (i = 0; i < count; ) {
		int v, j;
found:
		v = rand() % nmax;
		for (j = 0; j < i; j++) {
			if (r[offset + j] == v)
				goto found;
		}
		r[offset + i++] = v;
	}
	qsort(r + offset, count, sizeof(*r), cmp_intp);

	if (offset) {
		int j, n;

		/* simplifies next loop */
		r[size] = nmax;

		/* convert the indexes we don't want to those we want */
		i = 0;
		j = offset;
		n = 0;
		do {
			while (n < r[j])
				r[i++] = n++;
			j++;
			n++;
		} while (i < size);
	}
	return r;
}

static int count_albums(void)
{
	struct artist *artist;
	struct rb_node *tmp1, *tmp2;
	int count = 0;

	rb_for_each_entry(artist, tmp1, &lib_artist_root, tree_node) {
		rb_for_each(tmp2, &artist->album_root)
			count++;
	}
	return count;
}

struct album_list {
	struct list_head node;
	const struct album *album;
};

static void cmd_lqueue(char *arg)
{
	LIST_HEAD(head);
	const struct list_head *item;
	const struct album *album;
	int count = 1, nmax, i, pos;
	int *r;

	if (arg) {
		long int val;

		if (str_to_int(arg, &val) || val <= 0) {
			error_msg("argument must be positive integer");
			return;
		}
		count = val;
	}
	editable_lock();
	nmax = count_albums();
	if (count > nmax)
		count = nmax;
	if (!count)
		goto unlock;

	r = rand_array(count, nmax);
	album = to_album(rb_first(&to_artist(rb_first(&lib_artist_root))->album_root));
	pos = 0;
	for (i = 0; i < count; i++) {
		struct album_list *a;

		while (pos < r[i]) {
			struct artist *artist = album->artist;
			if (!rb_next(&album->tree_node)) {
				artist = to_artist(rb_next(&artist->tree_node));
				album = to_album(rb_first(&artist->album_root));
			} else {
				album = to_album(rb_next(&album->tree_node));
			}
			pos++;
		}
		a = xnew(struct album_list, 1);
		a->album = album;
		list_add_rand(&head, &a->node, i);
	}
	free(r);

	item = head.next;
	do {
		struct list_head *next = item->next;
		struct album_list *a = container_of(item, struct album_list, node);
		struct tree_track *t;
		struct rb_node *tmp;

		rb_for_each_entry(t, tmp, &a->album->track_root, tree_node)
			play_queue_append(tree_track_info(t));
		free(a);
		item = next;
	} while (item != &head);
unlock:
	editable_unlock();
}

struct track_list {
	struct list_head node;
	const struct simple_track *track;
};

static void cmd_tqueue(char *arg)
{
	LIST_HEAD(head);
	struct list_head *item;
	int count = 1, i, pos;
	int *r;

	if (arg) {
		long int val;

		if (str_to_int(arg, &val) || val <= 0) {
			error_msg("argument must be positive integer");
			return;
		}
		count = val;
	}
	editable_lock();
	if (count > lib_editable.nr_tracks)
		count = lib_editable.nr_tracks;
	if (!count)
		goto unlock;

	r = rand_array(count, lib_editable.nr_tracks);
	item = lib_editable.head.next;
	pos = 0;
	for (i = 0; i < count; i++) {
		struct track_list *t;

		while (pos < r[i]) {
			item = item->next;
			pos++;
		}
		t = xnew(struct track_list, 1);
		t->track = to_simple_track(item);
		list_add_rand(&head, &t->node, i);
	}
	free(r);

	item = head.next;
	do {
		struct list_head *next = item->next;
		struct track_list *t = container_of(item, struct track_list, node);
		play_queue_append(t->track->info);
		free(t);
		item = next;
	} while (item != &head);
unlock:
	editable_unlock();
}

/* tab exp {{{
 *
 * these functions fill tabexp struct, which is resetted beforehand
 */

/* buffer used for tab expansion */
static char expbuf[512];

static int filter_directories(const char *name, const struct stat *s)
{
	return S_ISDIR(s->st_mode);
}

static int filter_executable_files(const char *name, const struct stat *s)
{
	return S_ISREG(s->st_mode) && (s->st_mode & 0111);
}

static int filter_any(const char *name, const struct stat *s)
{
	return 1;
}

static int filter_playable(const char *name, const struct stat *s)
{
	return S_ISDIR(s->st_mode) || cmus_is_playable(name);
}

static int filter_playlist(const char *name, const struct stat *s)
{
	return S_ISDIR(s->st_mode) || cmus_is_playlist(name);
}

static int filter_supported(const char *name, const struct stat *s)
{
	return S_ISDIR(s->st_mode) || cmus_is_supported(name);
}

static void expand_files(const char *str)
{
	expand_files_and_dirs(str, filter_any);
}

static void expand_directories(const char *str)
{
	expand_files_and_dirs(str, filter_directories);
}

static void expand_playable(const char *str)
{
	expand_files_and_dirs(str, filter_playable);
}

static void expand_playlist(const char *str)
{
	expand_files_and_dirs(str, filter_playlist);
}

static void expand_supported(const char *str)
{
	expand_files_and_dirs(str, filter_supported);
}

static void expand_add(const char *str)
{
	int flag = parse_flags(&str, "lpqQ");

	if (flag == -1)
		return;
	if (str == NULL)
		str = "";
	expand_supported(str);

	if (tabexp.head && flag) {
		snprintf(expbuf, sizeof(expbuf), "-%c %s", flag, tabexp.head);
		free(tabexp.head);
		tabexp.head = xstrdup(expbuf);
	}
}

static void expand_program_paths(const char *str)
{
	if (str == NULL)
		str = "";
	if (str[0] == '~' || strchr(str, '/'))
		expand_files(str);
	else
		expand_env_path(str, filter_executable_files);
}

static void expand_program_paths_option(const char *str, const char *opt)
{
	expand_program_paths(str);

	if (tabexp.head && opt) {
		snprintf(expbuf, sizeof(expbuf), "%s=%s", opt, tabexp.head);
		free(tabexp.head);
		tabexp.head = xstrdup(expbuf);
	}
}

static void expand_load_save(const char *str)
{
	int flag = parse_flags(&str, "lp");

	if (flag == -1)
		return;
	if (str == NULL)
		str = "";
	expand_playlist(str);

	if (tabexp.head && flag) {
		snprintf(expbuf, sizeof(expbuf), "-%c %s", flag, tabexp.head);
		free(tabexp.head);
		tabexp.head = xstrdup(expbuf);
	}
}

static void expand_key_context(const char *str, const char *force)
{
	int pos, i, len = strlen(str);
	char **tails;

	tails = xnew(char *, NR_CTXS);
	pos = 0;
	for (i = 0; key_context_names[i]; i++) {
		int cmp = strncmp(str, key_context_names[i], len);
		if (cmp > 0)
			continue;
		if (cmp < 0)
			break;
		tails[pos++] = xstrdup(key_context_names[i] + len);
	}

	if (pos == 0) {
		free(tails);
		return;
	}
	if (pos == 1) {
		char *tmp = xstrjoin(tails[0], " ");
		free(tails[0]);
		tails[0] = tmp;
	}
	snprintf(expbuf, sizeof(expbuf), "%s%s", force, str);
	tabexp.head = xstrdup(expbuf);
	tabexp.tails = tails;
	tabexp.count = pos;
}

static int get_context(const char *str, int len)
{
	int i, c = -1, count = 0;

	for (i = 0; key_context_names[i]; i++) {
		if (strncmp(str, key_context_names[i], len) == 0) {
			if (key_context_names[i][len] == 0) {
				/* exact */
				return i;
			}
			c = i;
			count++;
		}
	}
	if (count == 1)
		return c;
	return -1;
}

static void expand_command_line(const char *str);

static void expand_bind_args(const char *str)
{
	/* :bind context key function
	 *
	 * possible values for str:
	 *   c
	 *   context k
	 *   context key f
	 *
	 * you need to know context before you can expand function
	 */
	/* start and end pointers for context, key and function */
	const char *cs, *ce, *ks, *ke, *fs;
	int i, c, k, count;
	int flag = parse_flags((const char **)&str, "f");
	const char *force = "";

	if (flag == -1)
		return;
	if (str == NULL)
		str = "";

	if (flag == 'f')
		force = "-f ";

	cs = str;
	ce = strchr(cs, ' ');
	if (ce == NULL) {
		expand_key_context(cs, force);
		return;
	}

	/* context must be expandable */
	c = get_context(cs, ce - cs);
	if (c == -1) {
		/* context is ambiguous or invalid */
		return;
	}

	ks = ce;
	while (*ks == ' ')
		ks++;
	ke = strchr(ks, ' ');
	if (ke == NULL) {
		/* expand key */
		int len = strlen(ks);
		PTR_ARRAY(array);

		for (i = 0; key_table[i].name; i++) {
			int cmp = strncmp(ks, key_table[i].name, len);
			if (cmp > 0)
				continue;
			if (cmp < 0)
				break;
			ptr_array_add(&array, xstrdup(key_table[i].name + len));
		}

		if (!array.count)
			return;

		if (array.count == 1) {
			char **ptrs = array.ptrs;
			char *tmp = xstrjoin(ptrs[0], " ");
			free(ptrs[0]);
			ptrs[0] = tmp;
		}

		snprintf(expbuf, sizeof(expbuf), "%s%s %s", force, key_context_names[c], ks);

		tabexp.head = xstrdup(expbuf);
		tabexp.tails = array.ptrs;
		tabexp.count = array.count;
		return;
	}

	/* key must be expandable */
	k = -1;
	count = 0;
	for (i = 0; key_table[i].name; i++) {
		if (strncmp(ks, key_table[i].name, ke - ks) == 0) {
			if (key_table[i].name[ke - ks] == 0) {
				/* exact */
				k = i;
				count = 1;
				break;
			}
			k = i;
			count++;
		}
	}
	if (count != 1) {
		/* key is ambiguous or invalid */
		return;
	}

	fs = ke;
	while (*fs == ' ')
		fs++;

	if (*fs == ':')
		fs++;

	/* expand com [arg...] */
	expand_command_line(fs);
	if (tabexp.head == NULL) {
		/* command expand failed */
		return;
	}

	/*
	 * tabexp.head is now "com"
	 * tabexp.tails is [ mand1 mand2 ... ]
	 *
	 * need to change tabexp.head to "context key com"
	 */

	snprintf(expbuf, sizeof(expbuf), "%s%s %s %s", force, key_context_names[c],
			key_table[k].name, tabexp.head);
	free(tabexp.head);
	tabexp.head = xstrdup(expbuf);
}

static void expand_unbind_args(const char *str)
{
	/* :unbind context key */
	/* start and end pointers for context and key */
	const char *cs, *ce, *ks;
	const struct binding *b;
	PTR_ARRAY(array);
	int c, len;

	cs = str;
	ce = strchr(cs, ' ');
	if (ce == NULL) {
		expand_key_context(cs, "");
		return;
	}

	/* context must be expandable */
	c = get_context(cs, ce - cs);
	if (c == -1) {
		/* context is ambiguous or invalid */
		return;
	}

	ks = ce;
	while (*ks == ' ')
		ks++;

	/* expand key */
	len = strlen(ks);
	b = key_bindings[c];
	while (b) {
		if (!strncmp(ks, b->key->name, len))
			ptr_array_add(&array, xstrdup(b->key->name + len));
		b = b->next;
	}
	if (!array.count)
		return;

	snprintf(expbuf, sizeof(expbuf), "%s %s", key_context_names[c], ks);

	tabexp.head = xstrdup(expbuf);
	tabexp.tails = array.ptrs;
	tabexp.count = array.count;
}

static void expand_factivate(const char *str)
{
	/* "name1 name2 name3", expand only name3 */
	struct filter_entry *e;
	const char *name;
	PTR_ARRAY(array);
	int str_len, len, i;

	str_len = strlen(str);
	i = str_len;
	while (i > 0) {
		if (str[i - 1] == ' ')
			break;
		i--;
	}
	len = str_len - i;
	name = str + i;

	list_for_each_entry(e, &filters_head, node) {
		if (!strncmp(name, e->name, len))
			ptr_array_add(&array, xstrdup(e->name + len));
	}
	if (!array.count)
		return;

	tabexp.head = xstrdup(str);
	tabexp.tails = array.ptrs;
	tabexp.count = array.count;
}

static void expand_fset(const char *str)
{
	struct filter_entry *e;
	PTR_ARRAY(array);

	list_for_each_entry(e, &filters_head, node) {
		char *line = xnew(char, strlen(e->name) + strlen(e->filter) + 2);
		sprintf(line, "%s=%s", e->name, e->filter);
		if (!strncmp(str, line, strlen(str)))
			ptr_array_add(&array, xstrdup(line + strlen(str)));
		free(line);
	}
	if (!array.count)
		return;

	tabexp.head = xstrdup(str);
	tabexp.tails = array.ptrs;
	tabexp.count = array.count;
}

static void expand_options(const char *str)
{
	struct cmus_opt *opt;
	int len;
	char **tails, *sep;

	/* tabexp is resetted */
	len = strlen(str);
	sep = strchr(str, '=');
	if (len > 1 && sep) {
		/* expand value */
		char *var = xstrndup(str, sep - str);

		list_for_each_entry(opt, &option_head, node) {
			if (strcmp(var, opt->name) == 0) {
				if (str[len - 1] == '=') {
					char buf[OPTION_MAX_SIZE];

					tails = xnew(char *, 1);

					buf[0] = 0;
					opt->get(opt->id, buf);
					tails[0] = xstrdup(buf);

					tabexp.head = xstrdup(str);
					tabexp.tails = tails;
					tabexp.count = 1;
				} else if (opt->flags & OPT_PROGRAM_PATH) {
					expand_program_paths_option(sep + 1, var);
				}
				break;
			}
		}
		free(var);
	} else {
		/* expand variable */
		int pos;

		tails = xnew(char *, nr_options);
		pos = 0;
		list_for_each_entry(opt, &option_head, node) {
			if (strncmp(str, opt->name, len) == 0)
				tails[pos++] = xstrdup(opt->name + len);
		}
		if (pos > 0) {
			if (pos == 1) {
				/* only one variable matches, add '=' */
				char *tmp = xstrjoin(tails[0], "=");

				free(tails[0]);
				tails[0] = tmp;
			}

			tabexp.head = xstrdup(str);
			tabexp.tails = tails;
			tabexp.count = pos;
		} else {
			free(tails);
		}
	}
}

static void expand_toptions(const char *str)
{
	struct cmus_opt *opt;
	int len, pos;
	char **tails;

	tails = xnew(char *, nr_options);
	len = strlen(str);
	pos = 0;
	list_for_each_entry(opt, &option_head, node) {
		if (opt->toggle == NULL)
			continue;
		if (strncmp(str, opt->name, len) == 0)
			tails[pos++] = xstrdup(opt->name + len);
	}
	if (pos > 0) {
		tabexp.head = xstrdup(str);
		tabexp.tails = tails;
		tabexp.count = pos;
	} else {
		free(tails);
	}
}

static void load_themes(const char *dirname, const char *str, struct ptr_array *array)
{
	struct directory dir;
	const char *name, *dot;
	int len = strlen(str);

	if (dir_open(&dir, dirname))
		return;

	while ((name = dir_read(&dir))) {
		if (!S_ISREG(dir.st.st_mode))
			continue;
		if (strncmp(name, str, len))
			continue;
		dot = strrchr(name, '.');
		if (dot == NULL || strcmp(dot, ".theme"))
			continue;
		if (dot - name < len)
			/* str is  "foo.th"
			 * matches "foo.theme"
			 * which also ends with ".theme"
			 */
			continue;
		ptr_array_add(array, xstrndup(name + len, dot - name - len));
	}
	dir_close(&dir);
}

static void expand_colorscheme(const char *str)
{
	PTR_ARRAY(array);

	load_themes(cmus_config_dir, str, &array);
	load_themes(DATADIR "/cmus", str, &array);

	if (array.count) {
		ptr_array_sort(&array, strptrcmp);

		tabexp.head = xstrdup(str);
		tabexp.tails = array.ptrs;
		tabexp.count = array.count;
	}
}

static void expand_commands(const char *str);

/* tab exp }}} */

/* sort by name */
struct command commands[] = {
	{ "add",		cmd_add,	1, 1, expand_add,	  0, 0 },
	{ "bind",		cmd_bind,	1, 1, expand_bind_args,	  0, CMD_UNSAFE },
	{ "browser-up",		cmd_browser_up,	0, 0, NULL,		  0, 0 },
	{ "cd",			cmd_cd,		0, 1, expand_directories, 0, 0 },
	{ "clear",		cmd_clear,	0, 1, NULL,		  0, 0 },
	{ "colorscheme",	cmd_colorscheme,1, 1, expand_colorscheme, 0, 0 },
	{ "echo",		cmd_echo,	1,-1, NULL,		  0, 0 },
	{ "factivate",		cmd_factivate,	0, 1, expand_factivate,	  0, 0 },
	{ "filter",		cmd_filter,	0, 1, NULL,		  0, 0 },
	{ "fset",		cmd_fset,	1, 1, expand_fset,	  0, 0 },
	{ "help",		cmd_help,	0, 0, NULL,		  0, 0 },
	{ "invert",		cmd_invert,	0, 0, NULL,		  0, 0 },
	{ "live-filter",	cmd_live_filter,0, 1, NULL,		  0, CMD_LIVE },
	{ "load",		cmd_load,	1, 1, expand_load_save,	  0, 0 },
	{ "lqueue",		cmd_lqueue,	0, 1, NULL,		  0, 0 },
	{ "mark",		cmd_mark,	0, 1, NULL,		  0, 0 },
	{ "player-next",	cmd_p_next,	0, 0, NULL,		  0, 0 },
	{ "player-pause",	cmd_p_pause,	0, 0, NULL,		  0, 0 },
	{ "player-play",	cmd_p_play,	0, 1, expand_playable,	  0, 0 },
	{ "player-prev",	cmd_p_prev,	0, 0, NULL,		  0, 0 },
	{ "player-stop",	cmd_p_stop,	0, 0, NULL,		  0, 0 },
	{ "prev-view",		cmd_prev_view,	0, 0, NULL,		  0, 0 },
	{ "push",		cmd_push,	1,-1, expand_commands,	  0, 0 },
	{ "quit",		cmd_quit,	0, 1, NULL,		  0, 0 },
	{ "refresh",		cmd_refresh,	0, 0, NULL,		  0, 0 },
	{ "run",		cmd_run,	1,-1, expand_program_paths, 0, CMD_UNSAFE },
	{ "save",		cmd_save,	0, 1, expand_load_save,	  0, CMD_UNSAFE },
	{ "search-next",	cmd_search_next,0, 0, NULL,		  0, 0 },
	{ "search-prev",	cmd_search_prev,0, 0, NULL,		  0, 0 },
	{ "seek",		cmd_seek,	1, 1, NULL,		  0, 0 },
	{ "set",		cmd_set,	1, 1, expand_options,	  0, 0 },
	{ "shell",		cmd_shell,	1,-1, expand_program_paths, 0, CMD_UNSAFE },
	{ "showbind",		cmd_showbind,	1, 1, expand_unbind_args, 0, 0 },
	{ "shuffle",		cmd_reshuffle,	0, 0, NULL,		  0, 0 },
	{ "source",		cmd_source,	1, 1, expand_files,	  0, CMD_UNSAFE },
	{ "toggle",		cmd_toggle,	1, 1, expand_toptions,	  0, 0 },
	{ "tqueue",		cmd_tqueue,	0, 1, NULL,		  0, 0 },
	{ "unbind",		cmd_unbind,	1, 1, expand_unbind_args, 0, 0 },
	{ "unmark",		cmd_unmark,	0, 0, NULL,		  0, 0 },
	{ "update-cache",	cmd_update_cache,0, 1, NULL,		  0, 0 },
	{ "view",		cmd_view,	1, 1, NULL,		  0, 0 },
	{ "vol",		cmd_vol,	1, 2, NULL,		  0, 0 },
	{ "w",			cmd_save,	0, 1, expand_load_save,	  0, CMD_UNSAFE },
	{ "win-activate",	cmd_win_activate,0, 0, NULL,		  0, 0 },
	{ "win-add-l",		cmd_win_add_l,	0, 0, NULL,		  0, 0 },
	{ "win-add-p",		cmd_win_add_p,	0, 0, NULL,		  0, 0 },
	{ "win-add-Q",		cmd_win_add_Q,	0, 0, NULL,		  0, 0 },
	{ "win-add-q",		cmd_win_add_q,	0, 0, NULL,		  0, 0 },
	{ "win-bottom",		cmd_win_bottom,	0, 0, NULL,		  0, 0 },
	{ "win-down",		cmd_win_down,	0, 0, NULL,		  0, 0 },
	{ "win-mv-after",	cmd_win_mv_after,0, 0, NULL,		  0, 0 },
	{ "win-mv-before",	cmd_win_mv_before,0, 0, NULL,		  0, 0 },
	{ "win-next",		cmd_win_next,	0, 0, NULL,		  0, 0 },
	{ "win-page-bottom",	cmd_win_pg_bottom,0, 0, NULL,		  0, 0 },
	{ "win-page-down",	cmd_win_pg_down,0, 0, NULL,		  0, 0 },
	{ "win-page-middle",	cmd_win_pg_middle,0, 0, NULL,		  0, 0 },
	{ "win-page-top",	cmd_win_pg_top,	0, 0, NULL,		  0, 0 },
	{ "win-page-up",	cmd_win_pg_up,	0, 0, NULL,		  0, 0 },
	{ "win-remove",		cmd_win_remove,	0, 0, NULL,		  0, CMD_UNSAFE },
	{ "win-sel-cur",	cmd_win_sel_cur,0, 0, NULL,		  0, 0 },
	{ "win-toggle",		cmd_win_toggle,	0, 0, NULL,		  0, 0 },
	{ "win-top",		cmd_win_top,	0, 0, NULL,		  0, 0 },
	{ "win-up",		cmd_win_up,	0, 0, NULL,		  0, 0 },
	{ "win-update",		cmd_win_update,	0, 0, NULL,		  0, 0 },
	{ "win-update-cache",	cmd_win_update_cache,0, 1, NULL,	  0, 0 },
	{ "wq",			cmd_quit,	0, 1, NULL,		  0, 0 },
	{ NULL,			NULL,		0, 0, 0,		  0, 0 }
};

/* fills tabexp struct */
static void expand_commands(const char *str)
{
	int i, len, pos;
	char **tails;

	/* tabexp is resetted */
	tails = xnew(char *, N_ELEMENTS(commands) - 1);
	len = strlen(str);
	pos = 0;
	for (i = 0; commands[i].name; i++) {
		if (strncmp(str, commands[i].name, len) == 0)
			tails[pos++] = xstrdup(commands[i].name + len);
	}
	if (pos > 0) {
		if (pos == 1) {
			/* only one command matches, add ' ' */
			char *tmp = xstrjoin(tails[0], " ");

			free(tails[0]);
			tails[0] = tmp;
		}
		tabexp.head = xstrdup(str);
		tabexp.tails = tails;
		tabexp.count = pos;
	} else {
		free(tails);
	}
}

struct command *get_command(const char *str)
{
	int i, len;

	while (*str == ' ')
		str++;
	for (len = 0; str[len] && str[len] != ' '; len++)
		;

	for (i = 0; commands[i].name; i++) {
		if (strncmp(str, commands[i].name, len))
			continue;

		if (commands[i].name[len] == 0) {
			/* exact */
			return &commands[i];
		}

		if (commands[i + 1].name && strncmp(str, commands[i + 1].name, len) == 0) {
			/* ambiguous */
			return NULL;
		}
		return &commands[i];
	}
	return NULL;
}

/* fills tabexp struct */
static void expand_command_line(const char *str)
{
	/* :command [arg]...
	 *
	 * examples:
	 *
	 * str      expanded value (tabexp.head)
	 * -------------------------------------
	 *   fs     fset
	 *   b c    bind common
	 *   se     se          (tabexp.tails = [ ek t ])
	 */
	/* command start/end, argument start */
	const char *cs, *ce, *as;
	const struct command *cmd;

	cs = str;
	ce = strchr(cs, ' ');
	if (ce == NULL) {
		/* expand command */
		expand_commands(cs);
		return;
	}

	/* command must be expandable */
	cmd = get_command(cs);
	if (cmd == NULL) {
		/* command ambiguous or invalid */
		return;
	}

	if (cmd->expand == NULL) {
		/* can't expand argument */
		return;
	}

	as = ce;
	while (*as == ' ')
		as++;

	/* expand argument */
	cmd->expand(as);
	if (tabexp.head == NULL) {
		/* argument expansion failed */
		return;
	}

	/* tabexp.head is now start of the argument string */
	snprintf(expbuf, sizeof(expbuf), "%s %s", cmd->name, tabexp.head);
	free(tabexp.head);
	tabexp.head = xstrdup(expbuf);
}

static void tab_expand(int direction)
{
	char *s1, *s2, *tmp;
	int pos;

	/* strip white space */
	pos = 0;
	while (cmdline.line[pos] == ' ' && pos < cmdline.bpos)
		pos++;

	/* string to expand */
	s1 = xstrndup(cmdline.line + pos, cmdline.bpos - pos);

	/* tail */
	s2 = xstrdup(cmdline.line + cmdline.bpos);

	tmp = tabexp_expand(s1, expand_command_line, direction);
	if (tmp) {
		/* tmp.s2 */
		int l1, l2;

		l1 = strlen(tmp);
		l2 = strlen(s2);
		cmdline.blen = l1 + l2;
		if (cmdline.blen >= cmdline.size) {
			while (cmdline.blen >= cmdline.size)
				cmdline.size *= 2;
			cmdline.line = xrenew(char, cmdline.line, cmdline.size);
		}
		sprintf(cmdline.line, "%s%s", tmp, s2);
		cmdline.bpos = l1;
		cmdline.cpos = u_strlen_safe(tmp);
		cmdline.clen = u_strlen_safe(cmdline.line);
		free(tmp);
	}
	free(s1);
	free(s2);
}

static void reset_tab_expansion(void)
{
	tabexp_reset();
	arg_expand_cmd = -1;
}

static void cmdline_modified(void)
{
	char *cmd, *arg;
	struct command *c;

	if (!parse_command(cmdline.line, &cmd, &arg))
		return;

	c = get_command(cmd);
	if (!c)
		goto end;

	if (c->flags & CMD_LIVE)
		run_parsed_command(cmd, arg);

end:
	free(cmd);
	free(arg);
}

int parse_command(const char *buf, char **cmdp, char **argp)
{
	int cmd_start, cmd_end, cmd_len;
	int arg_start, arg_end;
	int i;

	i = 0;
	while (buf[i] && buf[i] == ' ')
		i++;

	if (buf[i] == '#')
		return 0;

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

	cmd_len = cmd_end - cmd_start;
	if (cmd_len == 0)
		return 0;

	*cmdp = xstrndup(buf + cmd_start, cmd_len);
	if (arg_start == arg_end) {
		*argp = NULL;
	} else {
		*argp = xstrndup(buf + arg_start, arg_end - arg_start);
	}
	return 1;
}

int run_only_safe_commands;

void run_parsed_command(char *cmd, char *arg)
{
	int cmd_len = strlen(cmd);
	int i = 0;

	while (1) {
		const struct command *c = &commands[i];

		if (c->name == NULL) {
			error_msg("unknown command\n");
			break;
		}
		if (strncmp(cmd, c->name, cmd_len) == 0) {
			const char *next = commands[i + 1].name;
			int exact = c->name[cmd_len] == 0;

			if (!exact && next && strncmp(cmd, next, cmd_len) == 0) {
				error_msg("ambiguous command\n");
				break;
			}
			if (c->min_args > 0 && arg == NULL) {
				error_msg("not enough arguments\n");
				break;
			}
			if (c->max_args == 0 && arg) {
				error_msg("too many arguments\n");
				break;
			}
			if (run_only_safe_commands && (c->flags & CMD_UNSAFE)) {
				if (c->func != cmd_save || !is_stdout_filename(arg)) {
					d_print("trying to execute unsafe command over net\n");
					break;
				}
			}
			c->func(arg);
			break;
		}
		i++;
	}
}

void run_command(const char *buf)
{
	char *cmd, *arg;

	if (!parse_command(buf, &cmd, &arg))
		return;

	run_parsed_command(cmd, arg);
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
		input_mode = NORMAL_MODE;
	}
}

void command_mode_ch(uchar ch)
{
	switch (ch) {
	case 0x01: // ^A
		cmdline_move_home();
		break;
	case 0x02: // ^B
		cmdline_move_left();
		break;
	case 0x04: // ^D
		cmdline_delete_ch();
		cmdline_modified();
		break;
	case 0x05: // ^E
		cmdline_move_end();
		break;
	case 0x06: // ^F
		cmdline_move_right();
		break;
	case 0x03: // ^C
	case 0x07: // ^G
	case 0x1B: // ESC
		if (cmdline.blen) {
			history_add_line(&cmd_history, cmdline.line);
			cmdline_clear();
		}
		input_mode = NORMAL_MODE;
		break;
	case 0x0A:
		if (cmdline.blen) {
			run_command(cmdline.line);
			history_add_line(&cmd_history, cmdline.line);
			cmdline_clear();
		}
		input_mode = NORMAL_MODE;
		break;
	case 0x0B:
		cmdline_clear_end();
		cmdline_modified();
		break;
	case 0x09:
		tab_expand(1);
		break;
	case 0x15:
		cmdline_backspace_to_bol();
		cmdline_modified();
		break;
	case 0x17: // ^W
		cmdline_backward_delete_word(cmdline_word_delimiters);
		cmdline_modified();
		break;
	case 0x08: // ^H
	case 127:
		backspace();
		cmdline_modified();
		break;
	default:
		cmdline_insert_ch(ch);
		cmdline_modified();
	}
	reset_history_search();
	if (ch != 0x09)
		reset_tab_expansion();
}

void command_mode_escape(int c)
{
	switch (c) {
	case 98:
		cmdline_backward_word(cmdline_filename_delimiters);
		break;
	case 100:
		cmdline_delete_word(cmdline_filename_delimiters);
		cmdline_modified();
		break;
	case 102:
		cmdline_forward_word(cmdline_filename_delimiters);
		break;
	case 127:
	case KEY_BACKSPACE:
		cmdline_backward_delete_word(cmdline_filename_delimiters);
		cmdline_modified();
		break;
	}
	reset_history_search();
}

void command_mode_key(int key)
{
	if (key != KEY_BTAB)
		reset_tab_expansion();
	switch (key) {
	case KEY_DC:
		cmdline_delete_ch();
		cmdline_modified();
		break;
	case KEY_BACKSPACE:
		backspace();
		cmdline_modified();
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
	case KEY_BTAB:
		tab_expand(-1);
		break;
	default:
		d_print("key = %c (%d)\n", key, key);
	}
	reset_history_search();
}

void commands_init(void)
{
	cmd_history_filename = xstrjoin(cmus_config_dir, "/command-history");
	history_load(&cmd_history, cmd_history_filename, 2000);
}

void commands_exit(void)
{
	view_clear(TREE_VIEW);
	view_clear(SORTED_VIEW);
	view_clear(PLAYLIST_VIEW);
	view_clear(QUEUE_VIEW);
	history_save(&cmd_history);
	history_free(&cmd_history);
	free(cmd_history_filename);
	tabexp_reset();
}
