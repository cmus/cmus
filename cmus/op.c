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

#include <op.h>
#include <sf.h>
#include <sconf.h>
#include <misc.h>
#include <utils.h>
#include <xmalloc.h>
#include <config.h>

#if defined(CONFIG_ARTS)
#include <op_arts.h>
#endif

#if defined(CONFIG_ALSA)
#include <op_alsa.h>
#include <mixer_alsa.h>
#endif

#if defined(CONFIG_OSS)
#include <op_oss.h>
#include <mixer_oss.h>
#endif

#include <debug.h>

#include <string.h>
#include <stdlib.h>

struct mixer_plugin {
	const char *name;
	const struct mixer_plugin_ops *ops;
	const char * *options;
	unsigned int initialized : 1;
};

struct output_plugin {
	const char *name;
	const struct output_plugin_ops *ops;
	const char * const *options;
	unsigned int initialized : 1;
};

static struct mixer_plugin mixer_plugins[] = {
#if defined(CONFIG_ALSA)
	{ "alsa", &mixer_alsa_ops, mixer_alsa_options, 0 },
#endif
#if defined(CONFIG_OSS)
	{ "oss", &mixer_oss_ops, mixer_oss_options, 0 },
#endif
	{ NULL, NULL, NULL, 0 }
};

static struct output_plugin output_plugins[] = {
#if defined(CONFIG_ARTS)
	{ "arts", &op_arts_ops, op_arts_options, 0 },
#endif
#if defined(CONFIG_ALSA)
	{ "alsa", &op_alsa_ops, op_alsa_options, 0 },
#endif
#if defined(CONFIG_OSS)
	{ "oss", &op_oss_ops, op_oss_options, 0 },
#endif
	{ NULL, NULL, NULL, 0 }
};

static const struct mixer_plugin *mixer = NULL;
static const struct output_plugin *op = NULL;
static struct sample_format current_sf = { 0, 0, 0, 0, 0 };

/* volume is between 0 and volume_max */
static int volume_max;

/* hack */
static int arts_use_alsa_mixer = 1;

static void dump_option(void *data, const char *key)
{
	d_print("%s\n", key);
}

int op_init(void)
{
	char key[64];
	int i, j, rc = 0;
	char *op_name = NULL;

	/* _must_ load plugin options before initialization! */
	for (i = 0; output_plugins[i].name; i++) {
		for (j = 0; output_plugins[i].options[j]; j++) {
			char *val = NULL;

			snprintf(key, sizeof(key), "dsp.%s.%s",
					output_plugins[i].name,
					output_plugins[i].options[j]);
			sconf_get_str_option(&sconf_head, key, &val);
			if (val) {
				d_print("loaded: '%s=%s'\n", key, val);
				output_plugins[i].ops->set_option(j, val);
				free(val);
			}
		}
	}
	for (i = 0; mixer_plugins[i].name; i++) {
		for (j = 0; mixer_plugins[i].options[j]; j++) {
			char *val = NULL;

			snprintf(key, sizeof(key), "mixer.%s.%s",
					mixer_plugins[i].name,
					mixer_plugins[i].options[j]);
			sconf_get_str_option(&sconf_head, key, &val);
			if (val) {
				d_print("loaded: '%s=%s'\n", key, val);
				mixer_plugins[i].ops->set_option(j, val);
				free(val);
			}
		}
	}

	/* options have been set, initialize */
	for (i = 0; mixer_plugins[i].name; i++) {
		rc = mixer_plugins[i].ops->init();
		if (rc == 0) {
			mixer_plugins[i].initialized = 1;
		} else {
			d_print("could not initialize mixer `%s'\n", mixer_plugins[i].name);
		}
	}
	for (i = 0; output_plugins[i].name; i++) {
		rc = output_plugins[i].ops->init();
		if (rc == 0) {
			output_plugins[i].initialized = 1;
		} else {
			d_print("could not initialize op `%s'\n", output_plugins[i].name);
		}
	}

	sconf_get_str_option(&sconf_head, "output_plugin", &op_name);
	sconf_get_bool_option(&sconf_head, "mixer.arts.use_alsa", &arts_use_alsa_mixer);

	/* select op */
	if (op_name) {
		rc = op_select(op_name);
		if (rc) {
			d_print("could not initialize user defined op: %s\n", op_name);
			for (i = 0; output_plugins[i].name; i++) {
				if (output_plugins[i].initialized) {
					rc = op_select(output_plugins[i].name);
					break;
				}
			}
		}
		free(op_name);
	} else {
		/* default op is the first initialized op */
		for (i = 0; output_plugins[i].name; i++) {
			if (output_plugins[i].initialized) {
				rc = op_select(output_plugins[i].name);
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
	char key[64];
	int i, j;

	d_print("saving options\n");

	/* save plugin options */
	for (i = 0; output_plugins[i].name; i++) {
		if (!output_plugins[i].initialized)
			continue;

		for (j = 0; output_plugins[i].options[j]; j++) {
			char *val = NULL;

			output_plugins[i].ops->get_option(j, &val);
			if (val) {
				snprintf(key, sizeof(key), "dsp.%s.%s",
						output_plugins[i].name,
						output_plugins[i].options[j]);
				d_print("saving: '%s=%s'\n", key, val);
				sconf_set_str_option(&sconf_head, key, val);
				free(val);
			}
		}
	}
	for (i = 0; mixer_plugins[i].name; i++) {
		if (!mixer_plugins[i].initialized)
			continue;

		for (j = 0; mixer_plugins[i].options[j]; j++) {
			char *val = NULL;

			mixer_plugins[i].ops->get_option(j, &val);
			if (val) {
				snprintf(key, sizeof(key), "mixer.%s.%s",
						mixer_plugins[i].name,
						mixer_plugins[i].options[j]);
				d_print("saving: '%s=%s'\n", key, val);
				sconf_set_str_option(&sconf_head, key, val);
				free(val);
			}
		}
	}

	sconf_set_bool_option(&sconf_head, "mixer.arts.use_alsa", arts_use_alsa_mixer);

	/* free plugins */
	for (i = 0; mixer_plugins[i].name; i++) {
		if (mixer_plugins[i].initialized)
			mixer_plugins[i].ops->exit();
	}
	for (i = 0; output_plugins[i].name; i++) {
		if (output_plugins[i].initialized)
			output_plugins[i].ops->exit();
	}
	if (op)
		sconf_set_str_option(&sconf_head, "output_plugin", op->name);
	return 0;
}

void set_and_open_mixer(void)
{
	const char *name;
	int i;

	/* close current mixer */
	if (mixer) {
		mixer->ops->close();
		mixer = NULL;
	}
	if (op == NULL)
		return;

	/* hack */
	if (strcmp(op->name, "arts") == 0) {
		if (arts_use_alsa_mixer) {
			name = "alsa";
		} else {
			name = "oss";
		}
	} else {
		name = op->name;
	}

	for (i = 0; mixer_plugins[i].name; i++) {
		if (strcmp(name, mixer_plugins[i].name) == 0) {
			if (mixer_plugins[i].initialized) {
				int rc;

				mixer = &mixer_plugins[i];
				rc = mixer->ops->open(&volume_max);
				if (rc) {
					mixer = NULL;
				}
			}
			break;
		}
	}
}

int op_select(const char *name)
{
	int i;

	for (i = 0; output_plugins[i].name; i++) {
		if (strcasecmp(name, output_plugins[i].name) == 0) {
			if (!output_plugins[i].initialized) {
				/* try to initialize again */
				int rc;

				d_print("op `%s' is uninitialized, trying to initialize again\n", output_plugins[i].name);
				rc = output_plugins[i].ops->init();
				if (rc == 0) {
					output_plugins[i].initialized = 1;
				} else {
					d_print("could not initialize op `%s'\n", output_plugins[i].name);
					return -OP_ERROR_NOT_INITIALIZED;
				}
			}

			op = &output_plugins[i];
			set_and_open_mixer();
			return 0;
		}
	}
	return -OP_ERROR_NO_PLUGIN;
}

int op_open(const struct sample_format *sf)
{
	int rc;

	current_sf = *sf;
	rc = op->ops->open(sf);
	if (rc)
		current_sf.bits = 0;
	return rc;
}

static int sample_format_changed(const struct sample_format *sf)
{
	return sf->rate != current_sf.rate ||
		sf->bits != current_sf.bits ||
		sf->channels != current_sf.channels ||
		sf->is_signed != current_sf.is_signed ||
		sf->big_endian != current_sf.big_endian;
}

int op_set_sf(const struct sample_format *sf)
{
	int rc = 0;

	if (sample_format_changed(sf)) {
		rc = op_close();
		if (rc) {
			d_print("op_close returned %d\n", rc);
			return rc;
		}
		rc = op_open(sf);
		if (rc) {
			return rc;
		} else {
			current_sf = *sf;
			return 1;
		}
	}
	return 0;
}

int op_second_size(void)
{
	return current_sf.rate * current_sf.bits * current_sf.channels / 8;
}

int op_drop(void)
{
	if (op->ops->drop == NULL)
		return -OP_ERROR_NOT_SUPPORTED;
	return op->ops->drop();
}

int op_close(void)
{
	current_sf.bits = 0;
	return op->ops->close();
}

int op_write(const char *buffer, int count)
{
	int rc;

	rc = op->ops->write(buffer, count);
	return rc;
}

int op_pause(void)
{
	if (op->ops->pause == NULL)
		return 0;
	return op->ops->pause();
}

int op_unpause(void)
{
	if (op->ops->unpause == NULL)
		return 0;
	return op->ops->unpause();
}

int op_buffer_space(void)
{
	int rc;
	
	rc = op->ops->buffer_space();
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

int op_set_volume(int left, int right)
{
	int l, r;

	if (mixer == NULL)
		return -OP_ERROR_NOT_SUPPORTED;
	l = scale_volume_to_internal(left);
	r = scale_volume_to_internal(right);
	return mixer->ops->set_volume(l, r);
}

int op_get_volume(int *left, int *right)
{
	int l, r, rc;

	if (mixer == NULL)
		return -OP_ERROR_NOT_SUPPORTED;
	rc = mixer->ops->get_volume(&l, &r);
	if (rc)
		return rc;
	*left = scale_volume_from_internal(l);
	*right = scale_volume_from_internal(r);
	return 0;
}

int op_add_volume(int *left, int *right)
{
	int l, r, rc;

	if (mixer == NULL)
		return -OP_ERROR_NOT_SUPPORTED;
	rc = mixer->ops->get_volume(&l, &r);
	if (rc)
		return rc;
	*left = clamp(*left + l, 0, volume_max);
	*right = clamp(*right + r, 0, volume_max);
	if (*left != l || *right != r)
		rc = mixer->ops->set_volume(*left, *right);
	*left = scale_volume_from_internal(*left);
	*right = scale_volume_from_internal(*right);
	return rc;
}

int op_volume_changed(int *left, int *right)
{
	static int oldl = -1;
	static int oldr = -1;
	int l, r, rc;

	if (mixer == NULL)
		return 0;
	rc = mixer->ops->get_volume(&l, &r);
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

int op_set_option(const char *key, const char *val)
{
	int i, j, len;

	/*
	 * dsp.alsa.device
	 * mixer.oss.channel
	 * ...
	 */
	d_print("begin '%s=%s'\n", key, val);
	if (strncasecmp(key, "dsp.", 4) == 0) {
		key += 4;
		for (len = 0; key[len] != '.'; len++) {
			if (key[len] == 0)
				return -OP_ERROR_NOT_OPTION;
		}
/* 		d_print("dsp: '%s'\n", key); */
		for (i = 0; output_plugins[i].name; i++) {
			if (strncasecmp(output_plugins[i].name, key, len))
				continue;

			key += len + 1;
			BUG_ON(output_plugins[i].ops->set_option == NULL);
			for (j = 0; output_plugins[i].options[j]; j++) {
/* 				d_print("dsp.%s.%s\n", output_plugins[i].name, output_plugins[i].options[j]); */
				if (strcasecmp(key, output_plugins[i].options[j]) == 0) {
					d_print("setting to '%s'\n", val);
					return output_plugins[i].ops->set_option(j, val);
				}
			}
			break;
		}
	} else if (strncasecmp(key, "mixer.", 6) == 0) {
		key += 6;
		for (len = 0; key[len] != '.'; len++) {
			if (key[len] == 0)
				return -OP_ERROR_NOT_OPTION;
		}
/* 		d_print("mixer: '%s'\n", key); */
		for (i = 0; mixer_plugins[i].name; i++) {
			if (strncasecmp(mixer_plugins[i].name, key, len))
				continue;

			key += len + 1;
			BUG_ON(mixer_plugins[i].ops->set_option == NULL);
			for (j = 0; mixer_plugins[i].options[j]; j++) {
/* 				d_print("mixer.%s.%s\n", mixer_plugins[i].name, mixer_plugins[i].options[j]); */
				if (strcasecmp(key, mixer_plugins[i].options[j]) == 0) {
					int rc;

					d_print("setting to '%s'\n", val);
					rc = mixer_plugins[i].ops->set_option(j, val);
					if (rc == 0)
						set_and_open_mixer();
					return rc;
				}
			}
			break;
		}
	}
	return -OP_ERROR_NOT_OPTION;
}

int op_for_each_option(void (*callback)(void *data, const char *key), void *data)
{
	char key[64];
	int i, j;

	for (i = 0; output_plugins[i].name; i++) {
		for (j = 0; output_plugins[i].options[j]; j++) {
			snprintf(key, sizeof(key), "dsp.%s.%s",
					output_plugins[i].name,
					output_plugins[i].options[j]);
			callback(data, key);
		}
	}
	for (i = 0; mixer_plugins[i].name; i++) {
		for (j = 0; mixer_plugins[i].options[j]; j++) {
			snprintf(key, sizeof(key), "mixer.%s.%s",
					mixer_plugins[i].name,
					mixer_plugins[i].options[j]);
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
	case OP_ERROR_SUCCESS:
	default:
		snprintf(buffer, sizeof(buffer),
				"%s: this is not an error (%d), this is a bug",
				arg, rc);
		break;
	}
	return xstrdup(buffer);
}
