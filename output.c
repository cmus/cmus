/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004-2006 Timo Hirvonen
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

#include "output.h"
#include "op.h"
#include "mixer.h"
#include "sf.h"
#include "utils.h"
#include "xmalloc.h"
#include "list.h"
#include "debug.h"
#include "ui_curses.h"
#include "options.h"
#include "xstrjoin.h"
#include "misc.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>

struct output_plugin {
	struct list_head node;
	char *name;
	void *handle;

	const struct output_plugin_ops *pcm_ops;
	const struct mixer_plugin_ops *mixer_ops;
	const struct output_plugin_opt *pcm_options;
	const struct mixer_plugin_opt *mixer_options;
	int priority;

	unsigned int pcm_initialized : 1;
	unsigned int mixer_initialized : 1;
	unsigned int mixer_open : 1;
};

static const char *plugin_dir;
static LIST_HEAD(op_head);
static struct output_plugin *op = NULL;

/* volume is between 0 and volume_max */
int volume_max = 0;
int volume_l = -1;
int volume_r = -1;

static void add_plugin(struct output_plugin *plugin)
{
	struct list_head *item = op_head.next;

	while (item != &op_head) {
		struct output_plugin *o = container_of(item, struct output_plugin, node);

		if (plugin->priority < o->priority)
			break;
		item = item->next;
	}

	/* add before item */
	list_add_tail(&plugin->node, item);
}

void op_load_plugins(void)
{
	DIR *dir;
	struct dirent *d;

	plugin_dir = xstrjoin(cmus_lib_dir, "/op");
	dir = opendir(plugin_dir);
	if (dir == NULL) {
		error_msg("couldn't open directory `%s': %s", plugin_dir, strerror(errno));
		return;
	}
	while ((d = (struct dirent *) readdir(dir)) != NULL) {
		char filename[512];
		struct output_plugin *plug;
		void *so, *symptr;
		char *ext;
		const unsigned *abi_version_ptr;
		bool err = false;

		if (d->d_name[0] == '.')
			continue;
		ext = strrchr(d->d_name, '.');
		if (ext == NULL)
			continue;
		if (strcmp(ext, ".so"))
			continue;

		snprintf(filename, sizeof(filename), "%s/%s", plugin_dir, d->d_name);

		so = dlopen(filename, RTLD_NOW);
		if (so == NULL) {
			d_print("%s: %s\n", filename, dlerror());
			continue;
		}

		plug = xnew(struct output_plugin, 1);

		plug->pcm_ops = dlsym(so, "op_pcm_ops");
		plug->pcm_options = dlsym(so, "op_pcm_options");
		symptr = dlsym(so, "op_priority");
		abi_version_ptr = dlsym(so, "op_abi_version");
		if (!plug->pcm_ops || !plug->pcm_options || !symptr) {
			error_msg("%s: missing symbol", filename);
			err = true;
		}
		if (!abi_version_ptr || *abi_version_ptr != OP_ABI_VERSION) {
			error_msg("%s: incompatible plugin version", filename);
			err = true;
		}
		if (err) {
			free(plug);
			dlclose(so);
			continue;
		}
		plug->priority = *(int *)symptr;

		plug->mixer_ops = dlsym(so, "op_mixer_ops");
		plug->mixer_options = dlsym(so, "op_mixer_options");
		if (plug->mixer_ops == NULL || plug->mixer_options == NULL) {
			plug->mixer_ops = NULL;
			plug->mixer_options = NULL;
		}

		plug->name = xstrndup(d->d_name, ext - d->d_name);
		plug->handle = so;
		plug->pcm_initialized = 0;
		plug->mixer_initialized = 0;
		plug->mixer_open = 0;

		add_plugin(plug);
	}
	closedir(dir);
}

static void init_plugin(struct output_plugin *o)
{
	if (!o->mixer_initialized && o->mixer_ops) {
		if (o->mixer_ops->init() == 0) {
			d_print("initialized mixer for %s\n", o->name);
			o->mixer_initialized = 1;
		} else {
			d_print("could not initialize mixer `%s'\n", o->name);
		}
	}
	if (!o->pcm_initialized) {
		if (o->pcm_ops->init() == 0) {
			d_print("initialized pcm for %s\n", o->name);
			o->pcm_initialized = 1;
		} else {
			d_print("could not initialize pcm `%s'\n", o->name);
		}
	}
}

void op_exit_plugins(void)
{
	struct output_plugin *o;

	list_for_each_entry(o, &op_head, node) {
		if (o->mixer_initialized && o->mixer_ops)
			o->mixer_ops->exit();
		if (o->pcm_initialized)
			o->pcm_ops->exit();
	}
}

void mixer_close(void)
{
	volume_max = 0;
	if (op && op->mixer_open) {
		BUG_ON(op->mixer_ops == NULL);
		op->mixer_ops->close();
		op->mixer_open = 0;
	}
}

void mixer_open(void)
{
	if (op == NULL)
		return;

	BUG_ON(op->mixer_open);
	if (op->mixer_ops && op->mixer_initialized) {
		int rc;

		rc = op->mixer_ops->open(&volume_max);
		if (rc == 0) {
			op->mixer_open = 1;
			mixer_read_volume();
		} else {
			volume_max = 0;
		}
	}
}

static int select_plugin(struct output_plugin *o)
{
	/* try to initialize if not initialized yet */
	init_plugin(o);

	if (!o->pcm_initialized)
		return -OP_ERROR_NOT_INITIALIZED;
	op = o;
	return 0;
}

int op_select(const char *name)
{
	struct output_plugin *o;

	list_for_each_entry(o, &op_head, node) {
		if (strcasecmp(name, o->name) == 0)
			return select_plugin(o);
	}
	return -OP_ERROR_NO_PLUGIN;
}

int op_select_any(void)
{
	struct output_plugin *o;
	int rc = -OP_ERROR_NO_PLUGIN;
	sample_format_t sf = sf_channels(2) | sf_rate(44100) | sf_bits(16) | sf_signed(1);

	list_for_each_entry(o, &op_head, node) {
		rc = select_plugin(o);
		if (rc != 0)
			continue;
		rc = o->pcm_ops->open(sf, NULL);
		if (rc == 0) {
			o->pcm_ops->close();
			break;
		}
	}
	return rc;
}

int op_open(sample_format_t sf, const channel_position_t *channel_map)
{
	if (op == NULL)
		return -OP_ERROR_NOT_INITIALIZED;
	return op->pcm_ops->open(sf, channel_map);
}

int op_drop(void)
{
	if (op->pcm_ops->drop == NULL)
		return -OP_ERROR_NOT_SUPPORTED;
	return op->pcm_ops->drop();
}

int op_close(void)
{
	return op->pcm_ops->close();
}

int op_write(const char *buffer, int count)
{
	return op->pcm_ops->write(buffer, count);
}

int op_pause(void)
{
	if (op->pcm_ops->pause == NULL)
		return 0;
	return op->pcm_ops->pause();
}

int op_unpause(void)
{
	if (op->pcm_ops->unpause == NULL)
		return 0;
	return op->pcm_ops->unpause();
}

int op_buffer_space(void)
{
	return op->pcm_ops->buffer_space();
}

int mixer_set_volume(int left, int right)
{
	if (op == NULL)
		return -OP_ERROR_NOT_INITIALIZED;
	if (!op->mixer_open)
		return -OP_ERROR_NOT_OPEN;
	return op->mixer_ops->set_volume(left, right);
}

int mixer_read_volume(void)
{
	if (op == NULL)
		return -OP_ERROR_NOT_INITIALIZED;
	if (!op->mixer_open)
		return -OP_ERROR_NOT_OPEN;
	return op->mixer_ops->get_volume(&volume_l, &volume_r);
}

int mixer_get_fds(int *fds)
{
	if (op == NULL)
		return -OP_ERROR_NOT_INITIALIZED;
	if (!op->mixer_open)
		return -OP_ERROR_NOT_OPEN;
	if (!op->mixer_ops->get_fds)
		return -OP_ERROR_NOT_SUPPORTED;
	return op->mixer_ops->get_fds(fds);
}

extern int soft_vol;

static void option_error(int rc)
{
	char *msg = op_get_error_msg(rc, "setting option");
	error_msg("%s", msg);
	free(msg);
}

static void set_dsp_option(void *data, const char *val)
{
	const struct output_plugin_opt *o = data;
	int rc;

	rc = o->set(val);
	if (rc)
		option_error(rc);
}

static bool option_of_current_mixer(const struct mixer_plugin_opt *opt)
{
	const struct mixer_plugin_opt *mpo;

	if (!op)
		return false;
	for (mpo = op->mixer_options; mpo && mpo->name; mpo++) {
		if (mpo == opt)
			return true;
	}
	return false;
}

static void set_mixer_option(void *data, const char *val)
{
	const struct mixer_plugin_opt *o = data;
	int rc;

	rc = o->set(val);
	if (rc) {
		option_error(rc);
	} else if (option_of_current_mixer(o)) {
		/* option of the current op was set
		 * try to reopen the mixer */
		mixer_close();
		if (!soft_vol)
			mixer_open();
	}
}

static void get_dsp_option(void *data, char *buf, size_t size)
{
	const struct output_plugin_opt *o = data;
	char *val = NULL;

	o->get(&val);
	if (val) {
		strscpy(buf, val, size);
		free(val);
	}
}

static void get_mixer_option(void *data, char *buf, size_t size)
{
	const struct mixer_plugin_opt *o = data;
	char *val = NULL;

	o->get(&val);
	if (val) {
		strscpy(buf, val, size);
		free(val);
	}
}

void op_add_options(void)
{
	struct output_plugin *o;
	const struct output_plugin_opt *opo;
	const struct mixer_plugin_opt *mpo;
	char key[64];

	list_for_each_entry(o, &op_head, node) {
		for (opo = o->pcm_options; opo->name; opo++) {
			snprintf(key, sizeof(key), "dsp.%s.%s", o->name,
					opo->name);
			option_add(xstrdup(key), opo, get_dsp_option,
					set_dsp_option, NULL, 0);
		}
		for (mpo = o->mixer_options; mpo && mpo->name; mpo++) {
			snprintf(key, sizeof(key), "mixer.%s.%s", o->name,
					mpo->name);
			option_add(xstrdup(key), mpo, get_mixer_option,
					set_mixer_option, NULL, 0);
		}
	}
}

char *op_get_error_msg(int rc, const char *arg)
{
	char buffer[1024];

	switch (-rc) {
	case OP_ERROR_ERRNO:
		snprintf(buffer, sizeof(buffer), "%s: %s", arg, strerror(errno));
		break;
	case OP_ERROR_NO_PLUGIN:
		snprintf(buffer, sizeof(buffer),
				"%s: no such plugin", arg);
		break;
	case OP_ERROR_NOT_INITIALIZED:
		snprintf(buffer, sizeof(buffer),
				"%s: couldn't initialize required output plugin", arg);
		break;
	case OP_ERROR_NOT_SUPPORTED:
		snprintf(buffer, sizeof(buffer),
				"%s: function not supported", arg);
		break;
	case OP_ERROR_NOT_OPEN:
		snprintf(buffer, sizeof(buffer),
				"%s: mixer is not open", arg);
		break;
	case OP_ERROR_SAMPLE_FORMAT:
		snprintf(buffer, sizeof(buffer),
				"%s: sample format not supported", arg);
		break;
	case OP_ERROR_NOT_OPTION:
		snprintf(buffer, sizeof(buffer),
				"%s: no such option", arg);
		break;
	case OP_ERROR_INTERNAL:
		snprintf(buffer, sizeof(buffer), "%s: internal error", arg);
		break;
	case OP_ERROR_SUCCESS:
	default:
		snprintf(buffer, sizeof(buffer),
				"%s: this is not an error (%d), this is a bug",
				arg, rc);
		break;
	}
	return xstrdup(buffer);
}

void op_dump_plugins(void)
{
	struct output_plugin *o;

	printf("\nOutput Plugins: %s\n", plugin_dir);
	list_for_each_entry(o, &op_head, node) {
		printf("  %s\n", o->name);
	}
}

const char *op_get_current(void)
{
	if (op)
		return op->name;
	return NULL;
}
