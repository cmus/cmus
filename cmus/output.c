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

#include <output.h>
#include <op.h>
#include <mixer.h>
#include <sf.h>
#include <sconf.h>
#include <misc.h>
#include <utils.h>
#include <symbol.h>
#include <xmalloc.h>
#include <debug.h>
#include <config.h>

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

	unsigned int pcm_initialized : 1;
	unsigned int mixer_initialized : 1;
	unsigned int mixer_open : 1;
};

static const char * const plugin_dir = LIBDIR "/" PACKAGE "/op";
static LIST_HEAD(op_head);
static struct output_plugin *op = NULL;
static sample_format_t current_sf = 0;

/* volume is between 0 and volume_max */
static int volume_max;

static void dump_option(void *data, const char *key)
{
	d_print("%s\n", key);
}

static void load_plugins(void)
{
	DIR *dir;
	struct dirent *d;

	dir = opendir(plugin_dir);
	if (dir == NULL) {
		fprintf(stderr, "couldn't open directory `%s': %s\n", plugin_dir, strerror(errno));
		return;
	}
	while ((d = readdir(dir)) != NULL) {
		char filename[256];
		struct output_plugin *plug;
		void *so;
		char *ext;

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
			fprintf(stderr, "%s\n", dlerror());
			continue;
		}

		plug = xnew(struct output_plugin, 1);

		if (!get_symbol(so, "op_pcm_ops", filename, (void **)&plug->pcm_ops, 0)) {
			free(plug);
			dlclose(so);
			continue;
		}

		if (!get_symbol(so, "op_pcm_options", filename, (void **)&plug->pcm_options, 0)) {
			free(plug);
			dlclose(so);
			continue;
		}

		if (!get_symbol(so, "op_mixer_ops", filename, (void **)&plug->mixer_ops, 1)) {
			free(plug);
			dlclose(so);
			continue;
		}

		if (!get_symbol(so, "op_mixer_options", filename, (void **)&plug->mixer_options, 1)) {
			free(plug);
			dlclose(so);
			continue;
		}

		if (plug->mixer_ops == NULL || plug->mixer_options == NULL) {
			plug->mixer_ops = NULL;
			plug->mixer_options = NULL;
		}

		plug->name = xstrndup(d->d_name, ext - d->d_name);
		plug->handle = so;
		plug->pcm_initialized = 0;
		plug->mixer_initialized = 0;
		plug->mixer_open = 0;

		list_add_tail(&plug->node, &op_head);
	}
	closedir(dir);
}

static void init_plugins(void)
{
	struct output_plugin *o;
	int rc;

	list_for_each_entry(o, &op_head, node) {
		if (o->mixer_ops) {
			rc = o->mixer_ops->init();
			if (rc == 0) {
				o->mixer_initialized = 1;
			} else {
				d_print("could not initialize mixer `%s'\n", o->name);
			}
		}
		rc = o->pcm_ops->init();
		if (rc == 0) {
			o->pcm_initialized = 1;
		} else {
			d_print("could not initialize op `%s'\n", o->name);
		}
	}
}

static void exit_plugins(void)
{
	struct output_plugin *o;

	list_for_each_entry(o, &op_head, node) {
		if (o->mixer_initialized && o->mixer_ops)
			o->mixer_ops->exit();
		if (o->pcm_initialized)
			o->pcm_ops->exit();
	}
}

static void load_plugin_options(void)
{
	struct output_plugin *o;

	list_for_each_entry(o, &op_head, node) {
		char key[64];
		int j;

		for (j = 0; o->pcm_options[j]; j++) {
			char *val = NULL;

			snprintf(key, sizeof(key), "dsp.%s.%s",
					o->name,
					o->pcm_options[j]);
			sconf_get_str_option(&sconf_head, key, &val);
			if (val) {
				d_print("loaded: '%s=%s'\n", key, val);
				o->pcm_ops->set_option(j, val);
				free(val);
			}
		}

		/* arts has no mixer */
		if (o->mixer_ops == NULL)
			continue;

		for (j = 0; o->mixer_options[j]; j++) {
			char *val = NULL;

			snprintf(key, sizeof(key), "mixer.%s.%s",
					o->name,
					o->mixer_options[j]);
			sconf_get_str_option(&sconf_head, key, &val);
			if (val) {
				d_print("loaded: '%s=%s'\n", key, val);
				o->mixer_ops->set_option(j, val);
				free(val);
			}
		}
	}
}

static void save_plugin_options(void)
{
	struct output_plugin *o;

	list_for_each_entry(o, &op_head, node) {
		char key[64];
		int j;

		/* FIXME: */
		if (!o->pcm_initialized)
			continue;

		for (j = 0; o->pcm_options[j]; j++) {
			char *val = NULL;

			o->pcm_ops->get_option(j, &val);
			if (val) {
				snprintf(key, sizeof(key), "dsp.%s.%s",
						o->name,
						o->pcm_options[j]);
				d_print("saving: '%s=%s'\n", key, val);
				sconf_set_str_option(&sconf_head, key, val);
				free(val);
			}
		}

		/* FIXME: */
		if (!o->mixer_initialized)
			continue;

		for (j = 0; o->mixer_options[j]; j++) {
			char *val = NULL;

			o->mixer_ops->get_option(j, &val);
			if (val) {
				snprintf(key, sizeof(key), "mixer.%s.%s",
						o->name,
						o->mixer_options[j]);
				d_print("saving: '%s=%s'\n", key, val);
				sconf_set_str_option(&sconf_head, key, val);
				free(val);
			}
		}
	}
}

void op_init_plugins(void)
{
	load_plugins();

	/* _must_ load plugin options before initialization! */
	load_plugin_options();

	/* options have been set, initialize */
	init_plugins();
}

int op_init(void)
{
	int rc;
	char *op_name = NULL;

	sconf_get_str_option(&sconf_head, "output_plugin", &op_name);

	/* select op */
	rc = -OP_ERROR_NO_PLUGIN;
	if (op_name) {
		rc = op_select(op_name);
		if (rc)
			d_print("could not initialize user defined op: %s\n", op_name);
		free(op_name);
	}
	if (rc) {
		/* default op is the first initialized op */
		struct output_plugin *o;

		list_for_each_entry(o, &op_head, node) {
			if (o->pcm_initialized) {
				rc = op_select(o->name);
				break;
			}
		}
	}
	op_for_each_option(dump_option, NULL);
	d_print("rc = %d\n", rc);
	return rc;
}

int op_exit(void)
{
	d_print("saving options\n");
	save_plugin_options();
	if (op)
		sconf_set_str_option(&sconf_head, "output_plugin", op->name);
	exit_plugins();
	return 0;
}

static void close_mixer(void)
{
	if (op == NULL)
		return;

	if (op->mixer_open) {
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
	if (op->mixer_ops) {
		int rc;

		rc = op->mixer_ops->open(&volume_max);
		if (rc == 0) {
			op->mixer_open = 1;
		}
	}
}

int op_select(const char *name)
{
	struct output_plugin *o;

	list_for_each_entry(o, &op_head, node) {
		if (strcasecmp(name, o->name) == 0) {
			if (!o->pcm_initialized) {
				/* try to initialize again */
				int rc;

				d_print("op `%s' is uninitialized, trying to initialize again\n", o->name);
				rc = o->pcm_ops->init();
				if (rc == 0) {
					o->pcm_initialized = 1;
				} else {
					d_print("could not initialize op `%s'\n", o->name);
					return -OP_ERROR_NOT_INITIALIZED;
				}
			}

			close_mixer();
			op = o;
			open_mixer();
			return 0;
		}
	}
	return -OP_ERROR_NO_PLUGIN;
}

int op_open(sample_format_t sf)
{
	int rc;

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

/* 0..volume_max => 0..100 */
static int scale_volume_from_internal(int volume)
{
	volume = (int)((double)volume * 100.0 / (double)volume_max + 0.5);
	return volume;
}

/* 0..100 => 0..volume_max
 * or
 * -100..0 => -volume_max..0
 */
static int scale_volume_to_internal(int volume)
{
	if (volume < 0) {
		volume = (int)((double)volume * (double)volume_max / 100.0 - 0.5);
	} else {
		volume = (int)((double)volume * (double)volume_max / 100.0 + 0.5);
	}
	return volume;
}

int op_set_volume(int *left, int *right)
{
	int l, r;

	if (!op->mixer_open)
		return -OP_ERROR_NOT_SUPPORTED;
	l = scale_volume_to_internal(*left);
	r = scale_volume_to_internal(*right);
	*left = scale_volume_from_internal(l);
	*right = scale_volume_from_internal(r);
	return op->mixer_ops->set_volume(l, r);
}

int op_get_volume(int *left, int *right)
{
	int l, r, rc;

	if (!op->mixer_open)
		return -OP_ERROR_NOT_SUPPORTED;
	rc = op->mixer_ops->get_volume(&l, &r);
	if (rc)
		return rc;
	*left = scale_volume_from_internal(l);
	*right = scale_volume_from_internal(r);
	return 0;
}

int op_add_volume(int *left, int *right)
{
	int l, r, rc;

	if (!op->mixer_open)
		return -OP_ERROR_NOT_SUPPORTED;
	rc = op->mixer_ops->get_volume(&l, &r);
	if (rc)
		return rc;
	*left = clamp(*left + l, 0, volume_max);
	*right = clamp(*right + r, 0, volume_max);
	if (*left != l || *right != r)
		rc = op->mixer_ops->set_volume(*left, *right);
	*left = scale_volume_from_internal(*left);
	*right = scale_volume_from_internal(*right);
	return rc;
}

int op_volume_changed(int *left, int *right)
{
	static int oldl = -1;
	static int oldr = -1;
	int l, r, rc;

	if (!op->mixer_open)
		return 0;
	rc = op->mixer_ops->get_volume(&l, &r);
	if (rc)
		return rc;
	if (l != oldl || r != oldr) {
		oldl = l;
		oldr = r;
		*left = scale_volume_from_internal(l);
		*right = scale_volume_from_internal(r);
		return 1;
	}
	return 0;
}

static const struct output_plugin_ops *dsp_option(const char *key, int *optidx)
{
	struct output_plugin *o;
	char opname[32];
	int i;

	if (strncasecmp(key, "dsp.", 4))
		return NULL;
	key += 4;
	for (i = 0; key[i] != '.'; i++) {
		if (key[i] == 0)
			return NULL;
	}
	if (i >= sizeof(opname))
		return NULL;

	/* op name */
	strncpy(opname, key, i);
	opname[i] = 0;

	/* option name */
	key += i + 1;

	list_for_each_entry(o, &op_head, node) {
		if (strcasecmp(o->name, opname))
			continue;

		for (i = 0; o->pcm_options[i]; i++) {
			if (strcasecmp(key, o->pcm_options[i]) == 0) {
				d_print("mixer.%s.%s\n", opname, o->pcm_options[i]);
				*optidx = i;
				return o->pcm_ops;
			}
		}
		break;
	}
	return NULL;
}

static const struct mixer_plugin_ops *mixer_option(const char *key, int *optidx)
{
	struct output_plugin *o;
	char opname[32];
	int i;

	if (strncasecmp(key, "mixer.", 6))
		return NULL;
	key += 6;
	for (i = 0; key[i] != '.'; i++) {
		if (key[i] == 0)
			return NULL;
	}
	if (i >= sizeof(opname))
		return NULL;

	/* op name */
	strncpy(opname, key, i);
	opname[i] = 0;

	/* option name */
	key += i + 1;

	list_for_each_entry(o, &op_head, node) {
		if (strcasecmp(o->name, opname))
			continue;
		if (o->mixer_ops == NULL)
			continue;

		for (i = 0; o->mixer_options[i]; i++) {
			if (strcasecmp(key, o->mixer_options[i]) == 0) {
				d_print("mixer.%s.%s\n", opname, o->mixer_options[i]);
				*optidx = i;
				return o->mixer_ops;
			}
		}
		break;
	}
	return NULL;
}

int op_set_option(const char *key, const char *val)
{
	const struct output_plugin_ops *oo;
	const struct mixer_plugin_ops *mo;
	int idx, rc;

	oo = dsp_option(key, &idx);
	if (oo) {
		/* dsp is always stopped when setting options, no need to reopen */
		return oo->set_option(idx, val);
	}

	mo = mixer_option(key, &idx);
	if (mo) {
		rc = mo->set_option(idx, val);
		if (rc == 0 && op && op->mixer_ops == mo && op->mixer_open) {
			/* option of the current op was set and the mixer is open
			 * need to reopen the mixer */
			close_mixer();
			open_mixer();
		}
		return rc;
	}
	return -OP_ERROR_NOT_OPTION;
}

int op_get_option(const char *key, char **val)
{
	const struct output_plugin_ops *oo;
	const struct mixer_plugin_ops *mo;
	int idx;

	oo = dsp_option(key, &idx);
	if (oo)
		return oo->get_option(idx, val);

	mo = mixer_option(key, &idx);
	if (mo)
		return mo->get_option(idx, val);
	return -OP_ERROR_NOT_OPTION;
}

int op_for_each_option(void (*callback)(void *data, const char *key), void *data)
{
	struct output_plugin *o;
	char key[64];
	int j;

	list_for_each_entry(o, &op_head, node) {
		for (j = 0; o->pcm_options[j]; j++) {
			snprintf(key, sizeof(key), "dsp.%s.%s",
					o->name,
					o->pcm_options[j]);
			callback(data, key);
		}
		if (o->mixer_ops == NULL)
			continue;
		for (j = 0; o->mixer_options[j]; j++) {
			snprintf(key, sizeof(key), "mixer.%s.%s",
					o->name,
					o->mixer_options[j]);
			callback(data, key);
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
