/* 
 * This code is based on Tom Wheeley's libirman v0.4.1
 * 
 *     Copyright 1998-99 Tom Wheeley <tomw@tsys.demon.co.uk>
 *     this code is placed under the LGPL, see www.gnu.org for info
 * 
 */

#ifndef _IRMAN_H
#define _IRMAN_H

struct irman;

/* number of bytes sent back from the IR interface */
#define IRMAN_CODE_LEN		6

#define IRMAN_TEXT_SIZE		(IRMAN_CODE_LEN * 2 + 1)

extern struct irman *irman_open(const char *filename);
extern void irman_close(struct irman *irman);
extern int irman_get_fd(struct irman *irman);
extern int irman_get_code(struct irman *irman, unsigned char *code);

extern void irman_code_to_text(char *text, const unsigned char *code);

/* returns number of hex digits converted */
extern int irman_text_to_code(unsigned char *code, const char *text);

#endif
