/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <output.h>
#include <op.h>
#include <mixer.h>
#include <sf.h>
#include <prog.h>
#include <utils.h>
#include <xmalloc.h>
#include <list.h>
#include <debug.h>
#include "config/libdir.h"

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>

struct output_plugin {
	struct list_head node;
	char *name;
	void *handle;

	const struct output_plugin_ops *pcm_ops;
	const struct mixer_plugin_ops *mixer_ops;
	const char * const *pcm_options;
	const char * const *mixer_options;
	int priority;

	unsigned int pcm_initialized : 1;
	unsigned int mixer_initialized : 1;
	unsigned int mixer_open : 1;
};

static const char * const plugin_dir = LIBDIR "/cmus/op";
static LIST_HEAD(op_head);
static struct output_plugin *op = NULL;
static sample_format_t current_sf = 0;

/* volume is between 0 and volume_max */
int volume_max = 0;

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

	dir = opendir(plugin_dir);
	if (dir == NULL) {
		warn("couldn't open directory `%s': %s\n", plugin_dir, strerror(errno));
		return;
	}
	while ((d = readdir(dir)) != NULL) {
		char filename[256];
		struct output_plugin *plug;
		void *so, *symptr;
		char *ext;
		const char *sym;

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
			warn("%s\n", dlerror());
			continue;
		}

		plug = xnew(struct output_plugin, 1);

		sym = "op_pcm_ops";
		if (!(plug->pcm_ops = dlsym(so, sym)))
			goto sym_err;

		sym = "op_pcm_options";
		if (!(plug->pcm_options = dlsym(so, sym)))
			goto sym_err;

		sym = "op_priority";
		symptr = dlsym(so, sym);
		if (symptr == NULL)
			goto sym_err;
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
		continue;
sym_err:
		warn("%s: symbol %s not found\n", filename, sym);
		free(plug);
		dlclose(so);
	}
	closedir(dir);
}

static void init_plugin(struct output_plugin *o)
{
	if (!o->mixer_initialized && o->mixer_ops) {
		if (o->mixer_ops->init() == 0) {
			o->mixer_initialized = 1;
		} else {
			d_print("could not initialize mixer `%s'\n", o->name);
		}
	}
	if (!o->pcm_initialized) {
		if (o->pcm_ops->init() == 0) {
			o->pcm_initialized = 1;
		} else {
			d_print("could not initialize pcm `%s'\n", o->name);
		}
	}
}

void op_init_plugins(void)
{
	struct output_plugin *o;

	list_for_each_entry(o, &op_head, node)
		init_plugin(o);
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

static void close_mixer(void)
{
	volume_max = 0;
	if (op && op->mixer_open) {
		BUG_ON(op->mixer_ops == NULL);
		op->mixer_ops->close();
		op->mixer_open = 0;
	}
}

static void open_mixer(void)
{
	if (op == NULL)
		return;

	BUG_ON(op->mixer_open);
	if (op->mixer_ops && op->mixer_initialized) {
		int rc;

		rc = op->mixer_ops->open(&volume_max);
		if (rc == 0) {
			op->mixer_open = 1;
		} else {
			volume_max = 0;
		}
	}
}

int op_select(const char *name)
{
	struct output_plugin *o;

	list_for_each_entry(o, &op_head, node) {
		if (strcasecmp(name, o->name) == 0) {
			/* try to initialize if not initialized yet */
			init_plugin(o);

			if (!o->pcm_initialized)
				return -OP_ERROR_NOT_INITIALIZED;

			close_mixer();
			op = o;
			open_mixer();
			return 0;
		}
	}
	return -OP_ERROR_NO_PLUGIN;
}

int op_select_any(void)
{
	struct output_plugin *o;
	int rc = -OP_ERROR_NO_PLUGIN;

	list_for_each_entry(o, &op_head, node) {
		if (!o->pcm_initialized)
			continue;

		rc = op_select(o->name);
		if (rc == 0)
			break;
	}
	return rc;
}

int op_open(sample_format_t sf)
{
	int rc;

	if (op == NULL)
		return -OP_ERROR_NOT_INITIALIZED;
	current_sf = sf;
	rc = op->pcm_ops->open(sf);
	if (rc)
		current_sf = 0;
	return rc;
}

int op_set_sf(sample_format_t sf)
{
	int rc = 0;

	if (current_sf != sf) {
		rc = op_close();
		if (rc) {
			d_print("op_close returned %d\n", rc);
			return rc;
		}
		rc = op_open(sf);
		if (rc) {
			return rc;
		} else {
			current_sf = sf;
			return 1;
		}
	}
	return 0;
}

int op_second_size(void)
{
	return sf_get_rate(current_sf) * sf_get_frame_size(current_sf);
}

int op_drop(void)
{
	if (op->pcm_ops->drop == NULL)
		return -OP_ERROR_NOT_SUPPORTED;
	return op->pcm_ops->drop();
}

int op_close(void)
{
	current_sf = 0;
	return op->pcm_ops->close();
}

int op_write(const char *buffer, int count)
{
	int rc;

	rc = op->pcm_ops->write(buffer, count);
	return rc;
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
	int rc;
	
	rc = op->pcm_ops->buffer_space();
	return rc;
}

int op_set_volume(int left, int right)
{
	if (op == NULL)
		return -OP_ERROR_NOT_INITIALIZED;
	if (!op->mixer_open)
		return -OP_ERROR_NOT_SUPPORTED;
	return op->mixer_ops->set_volume(left, right);
}

int op_get_volume(int *left, int *right)
{
	if (op == NULL)
		return -OP_ERROR_NOT_INITIALIZED;
	if (!op->mixer_open)
		return -OP_ERROR_NOT_SUPPORTED;
	return op->mixer_ops->get_volume(left, right);
}

#define OP_OPT_ID(plugin_idx, is_mixer, option_idx) \
	(((plugin_idx) << 16) | ((is_mixer) << 15) | (option_idx))

static struct output_plugin *find_plugin(int idx)
{
	struct output_plugin *o;

	list_for_each_entry(o, &op_head, node) {
		if (idx == 0)
			return o;
		idx--;
	}
	return NULL;
}

int op_set_option(unsigned int id, const char *val)
{
	unsigned int pid = id >> 16;
	unsigned int mix = id & 0x8000;
	unsigned int oid = id & 0x7fff;
	const struct output_plugin *o = find_plugin(pid);

	if (o == NULL)
		return -OP_ERROR_NOT_OPTION;

	if (mix) {
		const struct mixer_plugin_ops *mo = o->mixer_ops;
		int rc = mo->set_option(oid, val);

		if (rc == 0 && op && op->mixer_ops == mo) {
			/* option of the current op was set
			 * try to reopen the mixer */
			close_mixer();
			open_mixer();
		}
		return rc;
	}
	return o->pcm_ops->set_option(oid, val);
}

int op_get_option(unsigned int id, char **val)
{
	unsigned int pid = id >> 16;
	unsigned int mix = id & 0x8000;
	unsigned int oid = id & 0x7fff;
	const struct output_plugin *o = find_plugin(pid);

	if (o == NULL)
		return -OP_ERROR_NOT_OPTION;

	if (mix)
		return o->mixer_ops->get_option(oid, val);
	return o->pcm_ops->get_option(oid, val);
}

int op_for_each_option(void (*cb)(unsigned int id, const char *key))
{
	struct output_plugin *o;
	unsigned int pid, oid;
	char key[64];

	pid = -1;
	list_for_each_entry(o, &op_head, node) {
		pid++;

		for (oid = 0; o->pcm_options[oid]; oid++) {
			snprintf(key, sizeof(key), "dsp.%s.%s",
					o->name,
					o->pcm_options[oid]);
			cb(OP_OPT_ID(pid, 0, oid), key);
		}
		if (o->mixer_ops == NULL)
			continue;
		for (oid = 0; o->mixer_options[oid]; oid++) {
			snprintf(key, sizeof(key), "mixer.%s.%s",
					o->name,
					o->mixer_options[oid]);
			cb(OP_OPT_ID(pid, 1, oid), key);
		}
	}
	return 0;
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

char *op_get_current(void)
{
	if (op)
		return xstrdup(op->name);
	return NULL;
}
