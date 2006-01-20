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
#include <filters.h>
#include <player.h>
#include <lib.h>
#include <pl.h>
#include <play_queue.h>
#include <cmus.h>
#include <worker.h>
#include <keys.h>
#include <xmalloc.h>
#include <xstrjoin.h>
#include <misc.h>
#include <path.h>
#include <format_print.h>
#include <spawn.h>
#include <utils.h>
#include <list.h>
#include <debug.h>

#include <stdlib.h>
#include <curses.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>

int confirm_run = 1;

static struct history cmd_history;
static char *cmd_history_filename;
static char *history_search_text = NULL;
static LIST_HEAD(options_head);
static int nr_options = 0;
static int arg_expand_cmd = -1;

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
		error_msg("'=' expected (:set option=value)");
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
	error_msg("unknown option '%s'", name);
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
		error_msg("invalid seek value");
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
			error_msg("invalid seek modifier");
			return;
		}
	}

	player_seek(seek, seek_mode);
}

static void cmd_factivate(char *arg)
{
	filters_activate_names(arg);
}

static void cmd_filter(char *arg)
{
	filters_set_anonymous(arg);
}

static void cmd_fset(char *arg)
{
	filters_set_filter(arg);
}

static void cmd_add(char *arg)
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

	switch (cur_view) {
	case TREE_VIEW:
	case SORTED_VIEW:
		cmus_add(lib_add_track, name, ft, JOB_TYPE_LIB);
		break;
	case PLAYLIST_VIEW:
		cmus_add(pl_add_track, name, ft, JOB_TYPE_PL);
		break;
	case QUEUE_VIEW:
		cmus_add(play_queue_append, name, ft, JOB_TYPE_QUEUE);
		break;
	default:
		info_msg(":add only works in views 1-4");
		free(name);
	}
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

static void cmd_clear(char *arg)
{
	switch (cur_view) {
	case TREE_VIEW:
	case SORTED_VIEW:
		worker_remove_jobs(JOB_TYPE_LIB);
		lib_clear();
		break;
	case PLAYLIST_VIEW:
		worker_remove_jobs(JOB_TYPE_PL);
		pl_clear();
		break;
	case QUEUE_VIEW:
		worker_remove_jobs(JOB_TYPE_QUEUE);
		play_queue_clear();
		break;
	default:
		info_msg(":clear only works in views 1-4");
	}
}

static void do_save(for_each_ti_cb for_each_ti, const char *given,
		char **filenamep, const char *autosave)
{
	char *filename = *filenamep;

	if (given == NULL) {
		/* no argument given, use old filename */
		if (filename == NULL) {
			/* filename not yet set, use default filename (autosave) */
			filename = xstrdup(autosave);
		}
	} else {
		/* argument given, set new filename */
		free(filename);
		filename = xstrdup(given);
	}
	*filenamep = filename;
	if (cmus_save(for_each_ti, filename) == -1)
		error_msg("saving '%s': %s", filename, strerror(errno));
}

static void cmd_save(char *arg)
{
	if (arg) {
		char *tmp;

		tmp = expand_filename(arg);
		arg = path_absolute(tmp);
		free(tmp);
	}

	switch (cur_view) {
	case TREE_VIEW:
	case SORTED_VIEW:
		do_save(lib_for_each, arg, &lib_filename, lib_autosave_filename);
		break;
	case PLAYLIST_VIEW:
		do_save(pl_for_each, arg, &pl_filename, pl_autosave_filename);
		break;
	default:
		info_msg(":save only works in views 1 & 2 (library) and 3 (playlist)");
	}
	free(arg);
}

static void cmd_load(char *arg)
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

	switch (cur_view) {
	case TREE_VIEW:
	case SORTED_VIEW:
		worker_remove_jobs(JOB_TYPE_LIB);
		lib_clear();
		cmus_add(lib_add_track, name, FILE_TYPE_PL, JOB_TYPE_LIB);
		free(lib_filename);
		lib_filename = name;
		break;
	case PLAYLIST_VIEW:
		worker_remove_jobs(JOB_TYPE_PL);
		pl_clear();
		cmus_add(pl_add_track, name, FILE_TYPE_PL, JOB_TYPE_PL);
		free(pl_filename);
		pl_filename = name;
		break;
	default:
		info_msg(":load only works in views 1-3");
		free(name);
	}
}

static void cmd_bind(char *arg)
{
	char *key, *func;

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

	key_bind(arg, key, func);
	return;
err:
	error_msg("expecting 3 arguments (context, key and function)\n");
}

static void cmd_unbind(char *arg)
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

	key_unbind(arg, key);
	return;
err:
	error_msg("expecting 2 arguments (context and key)\n");
}

static void cmd_quit(char *arg)
{
	quit();
}
static void cmd_reshuffle(char *arg)
{
	lib_reshuffle();
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
		char *part;
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

static char **sel_files;
static int sel_files_alloc;
static int sel_files_nr;

static int add_file(void *data, struct track_info *ti)
{
	if (sel_files_nr == sel_files_alloc) {
		sel_files_alloc = sel_files_alloc ? sel_files_alloc * 2 : 8;
		sel_files = xrenew(char *, sel_files, sel_files_alloc);
	}
	sel_files[sel_files_nr++] = xstrdup(ti->filename);
	return 0;
}

static void cmd_run(char *arg)
{
	char **av, **argv;
	int ac, argc, i, run, files_idx = -1;

	if (cur_view > SORTED_VIEW) {
		info_msg("Command execution is supported only in views 1 and 2");
		return;
	}

	av = parse_cmd(arg, &files_idx, &ac);
	if (av == NULL) {
		return;
	}

	/* collect selected files */
	sel_files = NULL;
	sel_files_alloc = 0;
	sel_files_nr = 0;
	lib_for_each_sel(add_file, NULL, 0);
	if (sel_files_nr == 0) {
		/* no files selected, do nothing */
		free_str_array(av);
		return;
	}
	sel_files[sel_files_nr] = NULL;

	/* build argv */
	argv = xnew(char *, ac + sel_files_nr + 1);
	argc = 0;
	if (files_idx == -1) {
		/* add selected files after rest of the args */
		for (i = 0; i < ac; i++)
			argv[argc++] = av[i];
		for (i = 0; i < sel_files_nr; i++)
			argv[argc++] = sel_files[i];
	} else {
		for (i = 0; i < files_idx; i++)
			argv[argc++] = av[i];
		for (i = 0; i < sel_files_nr; i++)
			argv[argc++] = sel_files[i];
		for (i = files_idx; i < ac; i++)
			argv[argc++] = av[i];
	}
	argv[argc] = NULL;

	free(av);
	free(sel_files);

	for (i = 0; argv[i]; i++)
		d_print("ARG: '%s'\n", argv[i]);

	run = 1;
	if (confirm_run && (sel_files_nr > 1 || strcmp(argv[0], "rm") == 0)) {
		if (!yes_no_query("Execute %s for the %d selected files? [y/N]", arg, sel_files_nr)) {
			info_msg("Aborted");
			run = 0;
		}
	}
	if (run) {
		int status;

		if (spawn(argv, &status)) {
			error_msg("executing %s: %s", argv[0], strerror(errno));
		} else {
			if (WIFEXITED(status)) {
				int rc = WEXITSTATUS(status);

				if (rc)
					error_msg("%s returned %d", argv[0], rc);
			}
			if (WIFSIGNALED(status))
				error_msg("%s received signal %d", argv[0], WTERMSIG(status));

			/* remove non-existed files, update tags for changed files */
			cmus_update_selected();
		}
	}

	free_str_array(argv);
}

/* fills tabexp struct */
static void expand_files(const char *str)
{
	tabexp_files = 1;
	expand_files_and_dirs(str);
}

/* fills tabexp struct */
static void expand_directories(const char *str)
{
	tabexp_files = 0;
	expand_files_and_dirs(str);
}

/* buffer used for tab expansion */
static char expbuf[512];

static void expand_key_context(const char *str)
{
	int pos, i, len = strlen(str);
	char **tails;

	tails = xnew(char *, NR_CTXS + 1);
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
	tails[pos] = NULL;
	tabexp.head = xstrdup(str);
	tabexp.tails = tails;
	tabexp.nr_tails = pos;
	tabexp.index = 0;
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

/* fills tabexp struct */
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
	char *tmp, **tails;
	int i, c, k, len, pos, alloc, count;

	cs = str;
	ce = strchr(cs, ' ');
	if (ce == NULL) {
		expand_key_context(cs);
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
		len = strlen(ks);
		tails = NULL;
		alloc = 0;
		pos = 0;
		for (i = 0; key_table[i].name; i++) {
			int cmp = strncmp(ks, key_table[i].name, len);
			if (cmp > 0)
				continue;
			if (cmp < 0)
				break;
			tails = str_array_add(tails, &alloc, &pos, xstrdup(key_table[i].name + len));
		}

		if (pos == 0) {
			return;
		}
		if (pos == 1) {
			tmp = xstrjoin(tails[0], " ");
			free(tails[0]);
			tails[0] = tmp;
		}

		snprintf(expbuf, sizeof(expbuf), "%s %s", key_context_names[c], ks);

		tails[pos] = NULL;
		tabexp.head = xstrdup(expbuf);
		tabexp.tails = tails;
		tabexp.nr_tails = pos;
		tabexp.index = 0;
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

	if (*fs == ':') {
		/* expand :com [arg...] */
		expand_command_line(fs + 1);
		if (tabexp.head == NULL) {
			/* command expand failed */
			return;
		}

		/*
		 * tabexp.head is now "com"
		 * tabexp.tails is [ mand1 mand2 ... ]
		 *
		 * need to change tabexp.head to "context key :com"
		 */

		snprintf(expbuf, sizeof(expbuf), "%s %s :%s", key_context_names[c],
				key_table[k].name, tabexp.head);
		free(tabexp.head);
		tabexp.head = xstrdup(expbuf);
		return;
	}

	/* expand function */
	len = strlen(fs);
	tails = NULL;
	alloc = 0;
	pos = 0;
	for (i = 0; key_functions[c][i].name; i++) {
		int cmp = strncmp(fs, key_functions[c][i].name, len);
		if (cmp > 0)
			continue;
		if (cmp < 0)
			break;
		tails = str_array_add(tails, &alloc, &pos, xstrdup(key_functions[c][i].name + len));
	}
	if (pos == 0)
		return;

	snprintf(expbuf, sizeof(expbuf), "%s %s %s", key_context_names[c], key_table[k].name, fs);

	tails[pos] = NULL;
	tabexp.head = xstrdup(expbuf);
	tabexp.tails = tails;
	tabexp.nr_tails = pos;
	tabexp.index = 0;
}

/* fills tabexp struct */
static void expand_unbind_args(const char *str)
{
	/* :unbind context key */
	/* start and end pointers for context and key */
	const char *cs, *ce, *ks;
	char **tails;
	int c, len, pos, alloc;
	const struct binding *b;

	cs = str;
	ce = strchr(cs, ' ');
	if (ce == NULL) {
		expand_key_context(cs);
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
	tails = NULL;
	alloc = 0;
	pos = 0;
	b = key_bindings[c];
	while (b) {
		if (strncmp(ks, b->key->name, len) == 0)
			tails = str_array_add(tails, &alloc, &pos, xstrdup(b->key->name + len));
		b = b->next;
	}
	if (pos == 0)
		return;

	snprintf(expbuf, sizeof(expbuf), "%s %s", key_context_names[c], ks);

	tails[pos] = NULL;
	tabexp.head = xstrdup(expbuf);
	tabexp.tails = tails;
	tabexp.nr_tails = pos;
	tabexp.index = 0;
}

/* fills tabexp struct */
static void expand_factivate(const char *str)
{
	/* "name1 name2 name3", expand only name3 */
	struct filter_entry *e;
	int str_len, len, i, pos, alloc;
	const char *name;
	char **tails;

	str_len = strlen(str);
	i = str_len;
	while (i > 0) {
		if (str[i - 1] == ' ')
			break;
		i--;
	}
	len = str_len - i;
	name = str + i;

	tails = NULL;
	alloc = 0;
	pos = 0;
	list_for_each_entry(e, &filters_head, node) {
		if (strncmp(name, e->name, len) == 0)
			tails = str_array_add(tails, &alloc, &pos, xstrdup(e->name + len));
	}
	if (pos == 0)
		return;

	tails[pos] = NULL;
	tabexp.head = xstrdup(str);
	tabexp.tails = tails;
	tabexp.nr_tails = pos;
	tabexp.index = 0;
}

/* fills tabexp struct */
static void expand_options(const char *str)
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
				tabexp.head = xstrdup(str);
				tabexp.tails = tails;
				tabexp.nr_tails = 1;
				tabexp.index = 0;
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
			if (pos == 1) {
				/* only one variable matches, add '=' */
				char *tmp = xstrjoin(tails[0], "=");

				free(tails[0]);
				tails[0] = tmp;
			}

			tails[pos] = NULL;
			tabexp.head = xstrdup(str);
			tabexp.tails = tails;
			tabexp.nr_tails = pos;
			tabexp.index = 0;
		} else {
			free(tails);
		}
	}
}

struct command {
	const char *name;
	void (*func)(char *arg);

	/* min/max number of arguments */
	int min_args;
	int max_args;

	void (*expand)(const char *str);
};

/* sort by name */
static struct command commands[] = {
	{ "add",	cmd_add,	1, 1, expand_files	},
	{ "bind",	cmd_bind,	1, 1, expand_bind_args	},
	{ "cd",		cmd_cd,		0, 1, expand_directories},
	{ "clear",	cmd_clear,	0, 0, NULL		},
	{ "factivate",	cmd_factivate,	0, 1, expand_factivate	},
	{ "filter",	cmd_filter,	0, 1, NULL		},
	{ "fset",	cmd_fset,	1, 1, NULL		},
	{ "load",	cmd_load,	1, 1, expand_files	},
	{ "quit",	cmd_quit,	0, 0, NULL		},
	{ "run",	cmd_run,	1,-1, NULL		},
	{ "save",	cmd_save,	0, 1, expand_files	},
	{ "seek",	cmd_seek,	1, 1, NULL		},
	{ "set",	cmd_set,	1, 1, expand_options	},
	{ "shuffle",	cmd_reshuffle,	0, 0, NULL		},
	{ "unbind",	cmd_unbind,	1, 1, expand_unbind_args},
	{ NULL,		NULL,		0, 0, 0			}
};

/* fills tabexp struct */
static void expand_commands(const char *str)
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
		if (pos == 1) {
			/* only one command matches, add ' ' */
			char *tmp = xstrjoin(tails[0], " ");

			free(tails[0]);
			tails[0] = tmp;
		}
		tails[pos] = NULL;
		tabexp.head = xstrdup(str);
		tabexp.tails = tails;
		tabexp.nr_tails = pos;
		tabexp.index = 0;
	} else {
		free(tails);
	}
}

static const struct command *get_command(const char *str, int len)
{
	int i;

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
	cmd = get_command(cs, ce - cs);
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

static void tab_expand(void)
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

	tmp = tabexp_expand(s1, expand_command_line);
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
		cmdline.cpos = u_strlen(tmp);
		cmdline.clen = u_strlen(cmdline.line);
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

/* FIXME: parse all arguments */
void run_command(const char *buf)
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
			error_msg("unknown command\n");
			break;
		}
		if (strncmp(cmd, commands[i].name, cmd_end - cmd_start) == 0) {
			const char *next = commands[i + 1].name;

			if (next && strncmp(cmd, next, cmd_end - cmd_start) == 0) {
				error_msg("ambiguous command\n");
				break;
			}
			d_print("full command name: %s\n", commands[i].name);
			if (commands[i].min_args > 0 && arg == NULL) {
				error_msg("not enough arguments\n");
				break;
			}
			if (commands[i].max_args == 0 && arg) {
				error_msg("too many arguments\n");
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
		input_mode = NORMAL_MODE;
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
}

void commands_exit(void)
{
	history_save(&cmd_history);
	free(cmd_history_filename);
	tabexp_reset();
}
