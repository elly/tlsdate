/* uconf.c */

#define _BSD_SOURCE
#include "uconf.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXSTR		4096
#define MAXERR		1024

/* uconf parse state */
struct usc {
	FILE *f;
	struct uconf *u;		/* root expr of this parse */
	int line;
	int lbyte;
	int byte;
};

static char ufgetc(struct usc *u);
static void uungetc(struct usc *u, char c);

static struct uconf *newu(void);
static void freeu(struct uconf *u);

static void addlist(struct uconf *l, struct uconf *t);

static int tonext(struct usc *s);
static int toeol(struct usc *s);

static char doesc(char c);
static int symchar(char c);

static void error(struct usc *s, const char *fmt, ...);

static struct uconf *readint(struct usc *s);
static struct uconf *readstr(struct usc *s);
static struct uconf *readsym(struct usc *s);
static struct uconf *readexp(struct usc *s);

static void printaux(struct uconf *u, int d);

struct uconf *uconf_read(FILE *f) {
	struct usc s;
	struct uconf *n = newu();
	struct uconf *t;

	s.f = f;
	s.byte = 0;
	s.lbyte = 0;
	s.line = 0;

	s.u = n;

	n->type = UCONF_TLIST;
	n->err = malloc(MAXERR + 1);
	if (!n->err) {
		freeu(n);
		return NULL;
	}

	while (tonext(&s) && !feof(f) && !ferror(f)) {
		t = readexp(&s);
		if (!t) {
			break;
		}
		addlist(n, t);
	}

	return n;
}

void uconf_print(struct uconf *u) {
	printaux(u, 0);
}

void uconf_free(struct uconf *u) {
	freeu(u);
}

const char *uconf_error(struct uconf *u) {
	if (!u->waserr) { return NULL; }
	return u->err;
}

static char ufgetc(struct usc *s) {
	char c = fgetc(s->f);
	if (c == '\n') {
		s->lbyte = s->byte;
		s->byte = 0;
		s->line++;
	} else {
		s->byte++;
	}
	return c;
}

static void uungetc(struct usc *s, char c) {
	if (c == '\n') {
		s->byte = s->lbyte;
		s->lbyte = 0;
		s->line--;
	} else {
		s->byte--;
	}
	ungetc(c, s->f);
}

static struct uconf *readint(struct usc *s) {
	int c;
	int sign = 1;
	int v = 0;
	struct uconf *u = newu();

	if (!u) {
		error(s, "Out of memory.");
		return NULL;
	}
	u->type = UCONF_TINT;
	u->next = NULL;

	c = ufgetc(s);
	if (c == '-') {
		sign = -1;
		if (feof(s->f) || ferror(s->f)) {
			error(s, "'-' at end of input, or I/O error.");
			freeu(u);
			return NULL;
		}
		c = ufgetc(s);
	}
	
	while (isdigit(c)) {
		v = v * 10;
		v += (c - '0');
		c = ufgetc(s);
	}

	if (!isspace(c) && c != ')') {
		error(s, "junk '%c' after end of integer", c);
		freeu(u);
		return u;
	}

	uungetc(s, c);
	u->u.ival = sign * v;
	return u;
}

static int symchar(char c) {
	if (c == '(' || c == ')') { return 0; }
	if (isspace(c)) { return 0; }
	return 1;
}

static struct uconf *readsym(struct usc *s) {
	char buf[MAXSTR];
	int bufidx = 0;
	int c;
	struct uconf *u;

	while (!feof(s->f) && !ferror(s->f) && bufidx < MAXSTR - 2) {
		c = ufgetc(s);
		if (!symchar(c)) {
			if (c == '(') {
				error(s, "'%c' immediately follows symbol", c);
				return NULL;
			}
			uungetc(s, c);
			break;
		}
		buf[bufidx++] = c;
	}

	if (bufidx == MAXSTR - 2) {
		error(s, "Symbol too long.");
		return NULL;
	}

	buf[bufidx] = '\0';

	u = newu();
	if (!u) {
		error(s, "Out of memory.");
		return NULL;
	}

	u->type = UCONF_TSYM;
	u->u.sval = malloc(bufidx + 1);
	if (!u->u.sval) {
		error(s, "Out of memory.");
		freeu(u);
		return NULL;
	}

	strcpy(u->u.sval, buf);
	return u;
}

static char doesc(char c) {
	switch (c) {
		case 'n': return '\n';
		case 'r': return '\r';
		case 't': return '\t';
		default: return c;
	}
}

static struct uconf *readstr(struct usc *s) {
	int esc = 0;
	char buf[MAXSTR];
	int bufidx = 0;
	int c;
	struct uconf *u;

	while (!feof(s->f) && !ferror(s->f) && bufidx < MAXSTR - 2) {
		c = ufgetc(s);
		if (esc) {
			buf[bufidx++] = doesc(c);
			esc = 0;
			continue;
		} else if (c == '\\') {
			esc = 1;
			continue;
		} else if (c == '"') {
			break;
		} else {
			buf[bufidx++] = c;
			continue;
		}
	}

	if (bufidx == MAXSTR - 2) {
		error(s, "String too large.");
		return NULL;
	}

	buf[bufidx] = '\0';
	u = newu();

	if (!u) {
		error(s, "Out of memory.");
		return NULL;
	}

	u->type = UCONF_TSTR;
	u->u.sval = malloc(bufidx + 1);
	if (!u->u.sval) {
		error(s, "Out of memory.");
		freeu(u);
		return NULL;
	}
	strcpy(u->u.sval, buf);

	return u;
}

static struct uconf *readexp(struct usc *s) {
	int c;
	struct uconf *u;
	struct uconf *t;

	if (!tonext(s) || feof(s->f) || ferror(s->f)) {
		return NULL;
	}
	c = ufgetc(s);
	if (isdigit(c) || c == '-') {
		uungetc(s, c);
		return readint(s);
	} else if (c == '"') {
		return readstr(s);
	} else if (c != '(' && c != ')') {
		uungetc(s, c);
		return readsym(s);
	} else if (c == ')') {
		error(s, "Unexpected '%c'", c);
		return NULL;
	}

	u = newu();
	u->type = UCONF_TLIST;
	while (tonext(s) && !feof(s->f) && !ferror(s->f)) {
		c = ufgetc(s);

		if (c == ')') {
			return u;
		}

		uungetc(s, c);
		t = readexp(s);
		if (!t) {
			return u;
		}
		addlist(u, t);
	}

	error(s, "Expected ')'");
	freeu(u);
	return NULL;
}

static struct uconf *newu(void) {
	struct uconf *u = malloc(sizeof(struct uconf));
	if (!u) { return NULL; }
	memset(u, 0, sizeof(*u));

	return u;
}

static void freeu(struct uconf *u) {
	struct uconf *t, *n;
	if (u->type == UCONF_TLIST) {
		t = u->u.lval.h;
		while (t) {
			n = t->next;
			freeu(t);
			t = n;
		}
	} else if (u->type == UCONF_TSTR && u->u.sval) {
		free(u->u.sval);
	}

	if (u->err) {
		free(u->err);
	}

	free(u);
}

static void addlist(struct uconf *u, struct uconf *e) {
	if (u->u.lval.h) {
		u->u.lval.t->next = e;
	} else {
		u->u.lval.h = e;
	}
	u->u.lval.t = e;
	e->next = NULL;
}

static void indent(int d) {
	while (d--) {
		printf("  ");
	}
}

static void printaux(struct uconf *u, int d) {
	struct uconf *t;

	indent(d);
	switch (u->type) {
		case UCONF_TNONE:
			printf("none\n");
			break;
		case UCONF_TINT:
			printf("int %d\n", u->u.ival);
			break;
		case UCONF_TSYM:
			printf("symbol %s\n", u->u.sval);
			break;
		case UCONF_TSTR:
			printf("str \"%s\"\n", u->u.sval);
			break;
		case UCONF_TLIST:
			printf("list\n");
			for (t = u->u.lval.h; t; t = t->next) {
				printaux(t, d + 1);
			}
			break;
	}
}

static int toeol(struct usc *s) {
	int c = ufgetc(s);
	while (c != '\n' && !feof(s->f) && !ferror(s->f)) {
		c = ufgetc(s);
	}
	return ferror(s->f);
}

static int tonext(struct usc *s) {
	int c = ufgetc(s);
	if (ferror(s->f)) { return 1; }
	if (feof(s->f)) { return 0; }
	while (isspace(c) || c == ';') {
		if (isspace(c)) {
			c = ufgetc(s);
			if (ferror(s->f)) {
				error(s, "I/O Error: %s", strerror(errno));
				return 0;
			}
			if (feof(s->f)) { return 1; }
			continue;
		} else if (c == ';') {
			if (toeol(s)) {
				error(s, "I/O Error: %s", strerror(errno));
				return 0;
			}
			if (feof(s->f)) { return 1; }
			c = ufgetc(s);
			continue;
		}
	}
	uungetc(s, c);
	return 1;
}

static void error(struct usc *s, const char *fmt, ...) {
	char fbuf[MAXERR + 1];
	va_list ap;

	va_start(ap, fmt);

	s->u->waserr = 1;
	memset(s->u->err, 0, MAXERR + 1);

	snprintf(fbuf, MAXERR, "line %d, byte %d: %s", s->line, s->byte, fmt);
	vsnprintf(s->u->err, MAXERR, fbuf, ap);
	va_end(ap);
}

void uconf_walk_alist(struct uconf *u,
                      void (*f)(void *x, const char *name, struct uconf *val),
                      void *x) {
	struct uconf *c;
	if (u->type != UCONF_TLIST)
		return;
	for (c = u->u.lval.h; c; c = c->next) {
		if (c->type == UCONF_TLIST && c->u.lval.h) {
			struct uconf *z = c->u.lval.h;
			if (z->type == UCONF_TSYM)
				f(x, z->u.sval, z->next);
		} else if (c->type == UCONF_TSYM) {
			f(x, c->u.sval, NULL);
		}
	}
}
