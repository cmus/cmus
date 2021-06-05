/*
 * Copyright 2021 Patrick Gaskin
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

#ifndef CMUS_PL_ENV_H
#define CMUS_PL_ENV_H

/**
 * ABOUT
 *   This file contains functions related to the pl_env_vars configuration
 *   option, which allows the library to be relocated by specifying a list of
 *   environment variables to use instead of the base path they contain when
 *   saving library/playlist and cache paths.
 *
 * CONFIGURATION
 * - options.h / pl_env_vars
 *   The array of environment variables to substitute. These are checked in the
 *   order they are specified in, and the first matching variable is used.
 *   Note that it is safe for the configuration to be changed regardless of if
 *   cmus is currently running, but cmus must be restarted for this to take full
 *   effect.
 *
 * - options.c / get_pl_env_vars + set_pl_env_vars
 *   Encodes and decodes the config entry to/from a comma-separated list.
 *
 * SUBSTITUTION
 * - cmus.c / save_playlist_cb
 *   Where the in-memory library/playlists are written on exit.
 *
 *   Paths are transformed into the versions with env var substitutions here to
 *   allow tracks to be relocated based on the path in the env var.
 *
 * - job.c / handle_line
 *   Where the on-disk library/playlists are parsed on startup.
 *
 *   Paths are subsituted from environment variables here as the inverse of
 *   save_playlist_cb.
 *
 * - cache.c / write_ti
 *   Where the in-memory track_info's are converted into cache entries, which
 *   are written to the on-disk cache file.
 *
 *   Paths are transformed into the versions with env var substitutions here to
 *   allow metadata (including the play count) to follow the track regardless of
 *   its real on-disk location.
 *
 * - cache.c / cache_entry_to_ti
 *   Where the on-disk cache entries are parsed into the in-memory hashtable of
 *   read/write track_info's on startup.
 *
 *   Paths are subsituted from environment variables here as the inverse of
 *   write_ti.
 *
 * - cache.c / cache_get_ti
 *   Where cmus reads the metadata for tracks when reading the library/playlists
 *   at startup, or when the user adds a track.
 *
 *   This is important because if this returns an error, the track is removed
 *   from the library/playlist. Since we don't want the user to lose their
 *   tracks if one of their environment variables in pl_env_vars was unset
 *   mistakenly, we just return a dummy track if the filename still contains a
 *   substitution.
 *
 * - player.c / _producer_play
 *   Where track playback is started and errors are handled.
 *
 *   If the error is due to a missing env var (see cache_get_ti above), we add
 *   to the error message so the cause is clear.
 *
 * FAQ
 * - Why use '\x1F' as the delimiter?
 *   1. To prevent compatibility issues if the user happened to use unusual
 *      characters in their filenames. Control characters like this one will not
 *      generally be found in filenames on any platform.
 *   2. This is the ASCII "Unit Separator" control character, which is supposed
 *      to be an (old-fashioned) invisible delimiter, which makes sense for our
 *      purposes.
 *   3. Since it is invisible, we don't need to deal with everywhere it might be
 *      printed to the UI if it is not substituted by pl_env_expand (i.e. if
 *      the env var is missing or invalid).
 *
 * - Why not remove the substitution if the env var is missing or invalid?
 *   1. When combined with the invisibility of the delimiter, this allows us to
 *      easily show the user the missing env var without needing to hook into
 *      all of the UI printing/formatting code.
 *   2. See the part above about cache_get_ti for a technical reason why we
 *      leave missing env var substitutions in.
 *
 * - What other approaches were considered?
 *   1. Using relative paths and resolving the filenames passed to input
 *      plugins. This would have required rewriting the library/playlist and
 *      cache code to handle relative paths, and would have resulted in a high
 *      risk of regressions and taken a long time to implement.
 *   2. Wrapping the filesystem functions. This is simpler to implement than the
 *      previous option, but even more risky.
 *   3. Same as this approach, but using native OS env var syntax. This would be
 *      significantly more complex to implement, and is likely to cause
 *      compatibility issues requiring manual intervention.
 *   4. Adding a configuration option for a base directory, and only storing
 *      relative paths against that. This seems simple at first, but it's has
 *      all the disadvantages of the aforementioned approaches, is more complex
 *      to implement, makes it harder to handle configuration changes while cmus
 *      is not running, and is a lot less flexible.
 *
 * - What are the advantages of this approach?
 *    1. This approach is mostly self-contained. There are very few places which
 *       this needs to hook, and the mechanism is easy to reason about.
 *    2. This approach works well with cmus. It takes advantage of the way it
 *       keeps all metadata in-memory during runtime.
 *    3. This approach does not have a risk of causing compatibility issues,
 *       behaviour changes, or regressions for users who do not enable this
 *       feature.
 *    4. This approach can handle configuration changes regardless of whether
 *       cmus is running.
 *    5. This approach is easy to disable, simply by clearing the configuration
 *       option. On the next exit, the paths will be restored to their
 *       un-substituted values. Note that this will still work even if the path
 *       in the environment variable does not exist, keeping the behaviour
 *       consistent with how cmus normally works (the paths will be preserved as
 *       long as they are also in the metadata cache, and an error will be
 *       displayed on playback). This is possible because env var substitions
 *       are always parsed at launch regardless of pl_env_vars.
 *    6. This approach does not require modifying input/output plugins.
 *    7. This approach is os-independent, and can even handle sharing libraries
 *       with platforms using the backslash as a path separator.
 *    8. This approach does not interfere with the stream mechanism.
 *    9. This approach can handle multiple replacement paths (i.e. base
 *       folders).
 *   10. This approach preserves library/playlist/cache backwards compatibility
 *       while pl_env_vars is disabled.
 *   11. This approach results in behaviour comparable to without it.
 *
 * - What are the disadvantages of this approach?
 *   1. While the substitution works correctly and reliably, it is a somewhat
 *      hacky method. Nevertheless, it is also the least intrusive method, which
 *      is a significant advantage.
 *   2. Certain error messages will be somewhat misleading when an environment
 *      variable is missing. This is mitigated by improving the error message
 *      for the most common error, failed playback due to a nonexistent path
 *      caused by a missing environment variable.
 *   3. It is not possible to implement a mechanism for fallback paths within
 *      cmus for searching for individual tracks.
 *
 * - What are some potential uses of pl_env_vars?
 *   1. Syncing a $CMUS_HOME with multiple devices with different home folders.
 *   2. Syncing a $CMUS_HOME with multiple devices with one or more different
 *      music paths.
 *   3. The above case, but also with a path which only exists on certain
 *      devices.
 *   4. Easily relocating the cache/library/playlists while preserving metadata
 *      including the play count. This can be done by setting pl_env_vars,
 *      exiting cmus, updating the env var to the new path, starting cmus,
 *      clearing pl_env_vars, and exiting cmus again.
 */

/**
 * PL_ENV_DELIMITER surrounds env var substitutions at the beginning of paths.
 */
#define PL_ENV_DELIMITER '\x1F'

/**
 * pl_env_init initializes the environment variable cache used by pl_env_get. It
 * must be called before loading the library, playlists, or cache.
 */
void pl_env_init(void);

/**
 * pl_env_reduce checks the base path against the configured environment
 * variables, replaces the first match with a substitution, and returns a
 * malloc'd copy of the result. If there isn't any valid match or the path
 * already contains a substitution, a copy of the original path is returned
 * as-is.
 */
char *pl_env_reduce(const char *path);

/**
 * pl_env_expand returns a malloc'd copy of path, with the environment variable
 * substitution. The provided path must use forward slashes, and begin with a
 * slash or a substitution followed by a slash (which will always be true for
 * library paths within cmus). If the path does not have a substitution, the
 * original path is returned. If the environment variable does not exist or is
 * invalid, the original path is also returned.
 */
char *pl_env_expand(const char *path);

/**
 * pl_env_var returns a pointer to the start of the substituted environment
 * variable name, or NULL if it is not present. If a variable is present and
 * out_length is not NULL, it is set to the length of the variable name. The
 * remainder of the path will be at pl_env_var_remainder(path, out_length).
 */
const char *pl_env_var(const char *path, int *out_length);

/**
 * pl_env_var_remainder returns a pointer to the remainder of the path after the
 * substitution. See pl_env_var.
 */
const char *pl_env_var_remainder(const char *path, int length);

/**
 * pl_env_var_len returns the length of the substituted environment variable
 * name, if present. Otherwise, it returns 0.
 */
int pl_env_var_len(const char *path);

#endif
