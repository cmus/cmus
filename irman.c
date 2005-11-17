/* 
 * This code is based on Tom Wheeley's libirman v0.4.1
 * 
 *     Copyright 1998-99 Tom Wheeley <tomw@tsys.demon.co.uk>
 *     this code is placed under the LGPL, see www.gnu.org for info
 * 
 */

#include <irman.h>
#include <utils.h>
#include <xmalloc.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>

struct irman {
	struct termios old_term;
	int old_flags;
	int fd;
};

/*
 * all times in microseconds
 */

/* time we allow the port to sort itself out with */
#define IRMAN_POWER_ON_LATENCY	10e3

/* gap between sending 'I' and 'R' */
#define IRMAN_HANDSHAKE_GAP	500

/* successive initial garbage characters should not be more than this apart */
#define IRMAN_GARBAGE_TIMEOUT	50e3

/* letters 'O' and 'K' should arrive within this */
#define IRMAN_HANDSHAKE_TIMEOUT	2e6

/* successive bytes of an ir pseudocode should arrive within this time limit */
#define IRMAN_POLL_TIMEOUT	1e3

static inline int hex_to_int(unsigned char hex)
{
	if (hex >= '0' && hex <= '9')
		return hex - '0';
	if (hex >= 'a' && hex <= 'f')
		return hex - 'a' + 10;
	if (hex >= 'A' && hex <= 'F')
		return hex - 'A' + 10;
	return -1;
}

/*
 * Read a character, with a timeout.
 *   timeout < 0: block indefinitely
 *   timeout = 0: return immediately
 *   timeout > 0: timeout after `timeout' microseconds
 */
static int read_char(int fd, unsigned char *ch, long time_out)
{
	fd_set fds;
	int rc;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	if (time_out < 0) {
		rc = select(fd + 1, &fds, NULL, NULL, NULL);
	} else {
		struct timeval tv;

		tv.tv_sec = time_out / 1000000;
		tv.tv_usec = (time_out % 1000000);
		rc = select(fd + 1, &fds, NULL, NULL, &tv);
	}
	if (rc == -1)
		return -1;
	if (rc == 0) {
		errno = ETIMEDOUT;
		return -1;
	}
	rc = read(fd, ch, 1);
	if (rc == 0)
		return -1;
	return 0;
}

static inline int write_char(int fd, unsigned char ch)
{
	if (write(fd, &ch, 1) != 1)
		return -1;
	return 0;
}

static inline int read_code(int fd, unsigned char *code, unsigned long time_out)
{
	int i, rc;

	rc = read_char(fd, &code[0], time_out);
	if (rc == -1)
		return -1;
	for (i = 1; i < IRMAN_CODE_LEN; i++) {
		rc = read_char(fd, &code[i], IRMAN_POLL_TIMEOUT);
		if (rc == -1)
			return -1;
	}
	return 0;
}

static int open_port(struct irman *irman, const char *filename)
{
	struct termios term;
	int flags;
	int parnum = 0;
	int baudrate = B9600;

	irman->fd = open(filename, O_RDWR | O_NOCTTY | O_NDELAY);
	if (irman->fd == -1)
		return -1;

	/* check to see that the file is a terminal */
	if (!isatty(irman->fd)) {
		close(irman->fd);
		return -1;
	}

	/* get port attributes, store in oldterm */
	if (tcgetattr(irman->fd, &irman->old_term) == -1) {
		close(irman->fd);
		return -1;
	}

	/* get port flags, save in oldflags */
	irman->old_flags = fcntl(irman->fd, F_GETFL);
	if (irman->old_flags == -1) {
		close(irman->fd);
		return -1;
	}

	/* copy old attrs into new structure */
	term = irman->old_term;
	flags = irman->old_flags;

	/* remove old parity setting, size and stop setting */
	term.c_cflag &= ~PARENB;
	term.c_cflag &= ~PARODD;
	term.c_cflag &= ~CSTOPB;
	term.c_cflag &= ~CSIZE;

	/* set character size, stop bits and parity */
	term.c_cflag |= CS8;
	term.c_cflag |= parnum;

	/* enable receiver, and don't change ownership */
	term.c_cflag |= CREAD | CLOCAL;

	/* disable flow control */
#ifdef CNEW_RTSCTS
	term.c_cflag &= ~CNEW_RTSCTS;
#else
 #ifdef CRTSCTS
	term.c_cflag &= ~CRTSCTS;
 #endif
 #ifdef CRTSXOFF
	term.c_cflag &= ~CRTSXOFF;
 #endif
#endif

	/* read characters immediately in non-canonical mode */
	term.c_cc[VMIN] = 1;
	term.c_cc[VTIME] = 1;

	/* set the input and output baud rate */
	cfsetispeed(&term, baudrate);
	cfsetospeed(&term, baudrate);

	/* set non-canonical mode */
	term.c_lflag = 0;

	/* ignore breaks and make terminal raw and dumb */
	term.c_iflag = 0;
	term.c_iflag |= IGNBRK;
	term.c_oflag &= ~OPOST;

	/* set the input and output baud rate */
	cfsetispeed(&term, baudrate);
	cfsetospeed(&term, baudrate);

	/* now clean the serial line and activate the new settings */
	tcflush(irman->fd, TCIOFLUSH);
	if (tcsetattr(irman->fd, TCSANOW, &term) == -1) {
		return -1;
	}

	/* set non-blocking */
	flags |= O_NONBLOCK;
	if (fcntl(irman->fd, F_SETFL, flags) == -1) {
		return -1;
	}

	/* wait a little while for everything to settle through */
	us_sleep(IRMAN_POWER_ON_LATENCY);
	return 0;
}

struct irman *irman_open(const char *filename)
{
	struct irman *irman;
	unsigned char ch;
	int rc;

	irman = xnew(struct irman, 1);
	errno = 0;
	if (open_port(irman, filename) == -1) {
		free(irman);
		return NULL;
	}

	/* clean buffer */
	while (read_char(irman->fd, &ch, IRMAN_GARBAGE_TIMEOUT) == 0)
		; /* nothing */

	if (write_char(irman->fd, 'I') < 0)
		goto close_exit;
	us_sleep(IRMAN_HANDSHAKE_GAP);
	if (write_char(irman->fd, 'R') < 0)
		goto close_exit;

	/* we'll be nice and give the box a good chance to send an 'O' */
	do {
		rc = read_char(irman->fd, &ch, IRMAN_HANDSHAKE_TIMEOUT);
		if (rc == -1) {
			/* error or timeout */
			goto close_exit;
		}
	} while (ch != 'O');

	/* as regards the 'K', however, that really must be the next character */
	rc = read_char(irman->fd, &ch, IRMAN_HANDSHAKE_TIMEOUT);
	if (rc == -1)
		goto close_exit;

	/*
	 * ENOEXEC is the closest error I could find, that would also not be
	 * generated by ir_read_char().  Anyway, ENOEXEC does more or less mean
	 * "I don't understand this data I've been given"
	 */
	if (ch != 'K') {
		errno = ENOEXEC;
		goto close_exit;
	}
	return irman;
close_exit:
	irman_close(irman);
	return NULL;
}

void irman_close(struct irman *irman)
{
	/* restore old settings */
	if (tcsetattr(irman->fd, TCSADRAIN, &irman->old_term) == -1) {
	}
	if (fcntl(irman->fd, F_SETFL, irman->old_flags) == -1) {
	}
	close(irman->fd);
	free(irman);
}

int irman_get_fd(struct irman *irman)
{
	return irman->fd;
}

int irman_get_code(struct irman *irman, unsigned char *code)
{
	return read_code(irman->fd, code, -1);
}

int irman_text_to_code(unsigned char *code, const char *text)
{
	int i = 0, j = 0;
	int tmp;

	while (i < IRMAN_CODE_LEN) {
		if (text[j] == 0 || text[j + 1] == 0)
			return i;
		code[i++] = (hex_to_int(text[j]) << 4) +
			hex_to_int(text[j + 1]);
		j += 2;
	}
	tmp = i;
	while (tmp < IRMAN_CODE_LEN)
		code[tmp++] = 0;
	return i;
}

void irman_code_to_text(char *text, const unsigned char *code)
{
	static const char hexdigit[16] = "0123456789abcdef";
	int i = 0, j = 0;

	while (i < IRMAN_CODE_LEN) {
		register unsigned char c = code[i++];

		text[j++] = hexdigit[c >> 4];
		text[j++] = hexdigit[c & 0x0f];
	}
	text[j] = 0;
}
