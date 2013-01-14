/* uconf.h - Tiny config file parser
 * Basically, it reads S-expressions.
 */

#ifndef UCONF_H
#define UCONF_H

#include <stdio.h>

#define UCONF_TNONE		0
#define UCONF_TINT		1
#define UCONF_TSTR		2
#define UCONF_TLIST		3
#define UCONF_TSYM		4

struct uconf {
	int type;
	int waserr;
	char *err;
	struct uconf *next;
	union {
		int ival;
		char *sval;
		struct {
			struct uconf *h;
			struct uconf *t;
		} lval;
	} u;
};

/* Reads a list of S-expressions from the given stream; returns the list, or
 * NULL if something goes wrong, in which case you should call uconf_error().
 */
struct uconf *uconf_read(FILE *f);

/* Prints a uconf value. */
void uconf_print(struct uconf *u);

/* Deallocates a uconf value, and all its children. */
void uconf_free(struct uconf *u);

/* Returns the string representation of whatever last went wrong. */
const char *uconf_error(struct uconf *u);

/* Walks an association list, which is a list of lists in which the first
 * element of each sublist is a symbol (the key) and the rest of the list is the
 * value, like so:
 * ((apple "red")
 *  (orange "orange")
 *  (banana "yellow"))
 * uconf_walk_alist(that, f, x) would call:
 *  f(x, "apple", ("red"))
 *  f(x, "orange", ("orange"))
 *  f(x, "banana", ("yellow"))
 * It is also legal for elements in the list to be bare symbols instead of lists
 * starting with a symbol; these are treated as being symbols with an empty list
 * value, so:
 *  (blue
 *   (red "another color"))
 * Would yield calls to:
 *  f(x, "blue", NULL)
 *  f(x, "red", ("another color"))
 */
void uconf_walk_alist(struct uconf *u,
                      void (*f)(void *x, const char *name, struct uconf *val),
                      void *x);

#endif /* !UCONF_H */
