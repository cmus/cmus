/* 
 * Copyright 2005 Timo Hirvonen
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

#include <spawn.h>
#include <file.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int spawn(char *argv[])
{
	pid_t pid;
	int err_pipe[2];

	if (pipe(err_pipe) == -1)
		return -1;

	pid = fork();
	if (pid == -1) {
		/* error */
		return -1;
	} else if (pid == 0) {
		/* child */
		int dev_null, err;

		close(err_pipe[0]);
		fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC);

		/* redirect stdout and stderr to /dev/null if possible */
		dev_null = open("/dev/null", O_WRONLY);
		if (dev_null != -1) {
			dup2(dev_null, 1);
			dup2(dev_null, 2);
		}

		execvp(argv[0], argv);

		/* error */
		err = errno;
		write_all(err_pipe[1], &err, sizeof(int));
		exit(1);
	} else {
		/* parent */
		int rc, errno_save, child_errno, status;

		close(err_pipe[1]);
		rc = read_all(err_pipe[0], &child_errno, sizeof(int));
		errno_save = errno;
		close(err_pipe[0]);

		waitpid(pid, &status, 0);

		if (rc == -1) {
			errno = errno_save;
			return -1;
		}
		if (rc == sizeof(int)) {
			errno = child_errno;
			return -1;
		}
		if (rc != 0) {
			errno = EMSGSIZE;
			return -1;
		}
		return 0;
	}
}
