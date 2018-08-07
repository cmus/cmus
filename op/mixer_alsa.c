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

#include "../mixer.h"
#include "../op.h"
#include "../xmalloc.h"
#include "../debug.h"

#include <strings.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API

#include <alsa/asoundlib.h>

static snd_mixer_t *alsa_mixer_handle;
static snd_mixer_elem_t *mixer_elem = NULL;
static long mixer_vol_min, mixer_vol_max;

/* configuration */
static char *alsa_mixer_device = NULL;
static char *alsa_mixer_element = NULL;

static int alsa_mixer_init(void)
{
	if (alsa_mixer_device == NULL)
		alsa_mixer_device = xstrdup("default");
	if (alsa_mixer_element == NULL)
		alsa_mixer_element = xstrdup("PCM");
	/* FIXME: check device */
	return 0;
}

static int alsa_mixer_exit(void)
{
	free(alsa_mixer_device);
	alsa_mixer_device = NULL;
	free(alsa_mixer_element);
	alsa_mixer_element = NULL;
	return 0;
}

static snd_mixer_elem_t *find_mixer_elem_by_name(const char *goal_name)
{
	snd_mixer_elem_t *elem;
	snd_mixer_selem_id_t *sid = NULL;

	snd_mixer_selem_id_malloc(&sid);

	for (elem = snd_mixer_first_elem(alsa_mixer_handle); elem;
		 elem = snd_mixer_elem_next(elem)) {

		const char *name;

		snd_mixer_selem_get_id(elem, sid);
		name = snd_mixer_selem_id_get_name(sid);
		d_print("name = %s\n", name);
		d_print("has playback volume = %d\n", snd_mixer_selem_has_playback_volume(elem));
		d_print("has playback switch = %d\n", snd_mixer_selem_has_playback_switch(elem));

		if (strcasecmp(name, goal_name) == 0) {
			if (!snd_mixer_selem_has_playback_volume(elem)) {
				d_print("mixer element `%s' does not have playback volume\n", name);
				elem = NULL;
			}
			break;
		}
	}

	snd_mixer_selem_id_free(sid);
	return elem;
}

static int alsa_mixer_open(int *volume_max)
{
	snd_mixer_elem_t *elem;
	int count;
	int rc;

	rc = snd_mixer_open(&alsa_mixer_handle, 0);
	if (rc < 0)
		goto error;
	rc = snd_mixer_attach(alsa_mixer_handle, alsa_mixer_device);
	if (rc < 0)
		goto error;
	rc = snd_mixer_selem_register(alsa_mixer_handle, NULL, NULL);
	if (rc < 0)
		goto error;
	rc = snd_mixer_load(alsa_mixer_handle);
	if (rc < 0)
		goto error;
	count = snd_mixer_get_count(alsa_mixer_handle);
	if (count == 0) {
		d_print("error: mixer does not have elements\n");
		return -2;
	}

	elem = find_mixer_elem_by_name(alsa_mixer_element);
	if (!elem) {
		d_print("mixer element `%s' not found, trying `Master'\n", alsa_mixer_element);
		elem = find_mixer_elem_by_name("Master");
		if (!elem) {
			d_print("error: cannot find suitable mixer element\n");
			return -2;
		}
	}
	snd_mixer_selem_get_playback_volume_range(elem, &mixer_vol_min, &mixer_vol_max);
	/* FIXME: get number of channels */
	mixer_elem = elem;
	*volume_max = mixer_vol_max - mixer_vol_min;

	return 0;

error:
	d_print("error: %s\n", snd_strerror(rc));
	return -1;
}

static int alsa_mixer_close(void)
{
	snd_mixer_close(alsa_mixer_handle);
	return 0;
}

static int alsa_mixer_get_fds(int *fds)
{
	struct pollfd pfd[NR_MIXER_FDS];
	int count, i;

	count = snd_mixer_poll_descriptors(alsa_mixer_handle, pfd, NR_MIXER_FDS);
	for (i = 0; i < count; i++)
		fds[i] = pfd[i].fd;
	return count;
}

static int alsa_mixer_set_volume(int l, int r)
{
	if (mixer_elem == NULL) {
		return -1;
	}
	l += mixer_vol_min;
	r += mixer_vol_min;
	if (l > mixer_vol_max)
		d_print("error: left volume too high (%d > %ld)\n",
				l, mixer_vol_max);
	if (r > mixer_vol_max)
		d_print("error: right volume too high (%d > %ld)\n",
				r, mixer_vol_max);
	snd_mixer_selem_set_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, l);
	snd_mixer_selem_set_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_RIGHT, r);
	return 0;
}

static int alsa_mixer_get_volume(int *l, int *r)
{
	long lv, rv;

	if (mixer_elem == NULL)
		return -1;
	snd_mixer_handle_events(alsa_mixer_handle);
	snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &lv);
	snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_RIGHT, &rv);
	*l = lv - mixer_vol_min;
	*r = rv - mixer_vol_min;
	return 0;
}

static int alsa_mixer_set_channel(const char *val)
{
	free(alsa_mixer_element);
	alsa_mixer_element = xstrdup(val);
	return 0;
}

static int alsa_mixer_get_channel(char **val)
{
	if (alsa_mixer_element)
		*val = xstrdup(alsa_mixer_element);
	return 0;
}

static int alsa_mixer_set_device(const char *val)
{
	free(alsa_mixer_device);
	alsa_mixer_device = xstrdup(val);
	return 0;
}

static int alsa_mixer_get_device(char **val)
{
	if (alsa_mixer_device)
		*val = xstrdup(alsa_mixer_device);
	return 0;
}

const struct mixer_plugin_ops op_mixer_ops = {
	.init = alsa_mixer_init,
	.exit = alsa_mixer_exit,
	.open = alsa_mixer_open,
	.close = alsa_mixer_close,
	.get_fds = alsa_mixer_get_fds,
	.set_volume = alsa_mixer_set_volume,
	.get_volume = alsa_mixer_get_volume,
};

const struct mixer_plugin_opt op_mixer_options[] = {
	OPT(alsa_mixer, channel),
	OPT(alsa_mixer, device),
	{ NULL },
};
