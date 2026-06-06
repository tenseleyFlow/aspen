#include "color.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

enum {
	COL_RESET, COL_NORMAL, COL_FILE, COL_DIR, COL_LINK, COL_FIFO, COL_DOOR,
	COL_BLK, COL_CHR, COL_ORPHAN, COL_SOCK, COL_SETUID, COL_SETGID,
	COL_STICKY_OW, COL_OW, COL_STICKY, COL_EXEC, COL_MISSING,
	COL_LEFTCODE, COL_RIGHTCODE, COL_ENDCODE, NCOL
};
#define CMD_EXT (-2)
#define CMD_ERR (-1)

struct cext {
	const char *ext;
	const char *flg;
	struct cext *nxt;
};

/* tree's built-in default map, used when neither TREE_COLORS nor LS_COLORS is
 * set and color is forced/CLICOLOR (color.c:140) — copied verbatim. */
static const char DEFAULT_MAP[] =
	":no=00:rs=0:fi=00:di=01;34:ln=01;36:pi=40;33:so=01;35:bd=40;33;01:cd=40;33;01:"
	"or=40;31;01:ex=01;32:*.bat=01;32:*.BAT=01;32:*.btm=01;32:*.BTM=01;32:*.cmd=01;32:"
	"*.CMD=01;32:*.com=01;32:*.COM=01;32:*.dll=01;32:*.DLL=01;32:*.exe=01;32:*.EXE=01;32:"
	"*.arj=01;31:*.bz2=01;31:*.deb=01;31:*.gz=01;31:*.lzh=01;31:*.rpm=01;31:*.tar=01;31:"
	"*.taz=01;31:*.tb2=01;31:*.tbz2=01;31:*.tbz=01;31:*.tgz=01;31:*.tz2=01;31:*.z=01;31:"
	"*.Z=01;31:*.zip=01;31:*.ZIP=01;31:*.zoo=01;31:*.asf=01;35:*.ASF=01;35:*.avi=01;35:"
	"*.AVI=01;35:*.bmp=01;35:*.BMP=01;35:*.flac=01;35:*.FLAC=01;35:*.gif=01;35:*.GIF=01;35:"
	"*.jpg=01;35:*.JPG=01;35:*.jpeg=01;35:*.JPEG=01;35:*.m2a=01;35:*.M2a=01;35:*.m2v=01;35:"
	"*.M2V=01;35:*.mov=01;35:*.MOV=01;35:*.mp3=01;35:*.MP3=01;35:*.mpeg=01;35:*.MPEG=01;35:"
	"*.mpg=01;35:*.MPG=01;35:*.ogg=01;35:*.OGG=01;35:*.ppm=01;35:*.rm=01;35:*.RM=01;35:"
	"*.tga=01;35:*.TGA=01;35:*.tif=01;35:*.TIF=01;35:*.wav=01;35:*.WAV=01;35:*.wmv=01;35:"
	"*.WMV=01;35:*.xbm=01;35:*.xpm=01;35:";

static int cmd(const char *s)
{
	static const struct {
		const char *cmd;
		int num;
	} cmds[] = {
		{ "rs", COL_RESET }, { "no", COL_NORMAL }, { "fi", COL_FILE },
		{ "di", COL_DIR }, { "ln", COL_LINK }, { "pi", COL_FIFO },
		{ "do", COL_DOOR }, { "bd", COL_BLK }, { "cd", COL_CHR },
		{ "or", COL_ORPHAN }, { "so", COL_SOCK }, { "su", COL_SETUID },
		{ "sg", COL_SETGID }, { "tw", COL_STICKY_OW }, { "ow", COL_OW },
		{ "st", COL_STICKY }, { "ex", COL_EXEC }, { "mi", COL_MISSING },
		{ "lc", COL_LEFTCODE }, { "rc", COL_RIGHTCODE }, { "ec", COL_ENDCODE },
		{ NULL, 0 }
	};
	if (!s)
		return CMD_ERR;
	if (s[0] == '*')
		return CMD_EXT;
	for (int i = 0; cmds[i].cmd; i++)
		if (!strcmp(cmds[i].cmd, s))
			return cmds[i].num;
	return CMD_ERR;
}

static void parse(struct colorizer *c, const char *src)
{
	c->code = asp_xmalloc(NCOL * sizeof *c->code);
	for (int i = 0; i < NCOL; i++)
		c->code[i] = NULL;
	c->buf = asp_xmalloc(strlen(src) + 1);
	memcpy(c->buf, src, strlen(src) + 1);

	for (char *tok = strtok(c->buf, ":"); tok; tok = strtok(NULL, ":")) {
		char *eq = strchr(tok, '=');
		char *val = NULL;
		if (eq) {
			*eq = '\0';
			val = eq + 1;
		}
		int col = cmd(tok);
		if (col == CMD_EXT) {
			if (val) {
				struct cext *e = asp_xmalloc(sizeof *e);
				e->ext = tok + 1; /* skip '*' */
				e->flg = val;
				e->nxt = c->ext;
				c->ext = e;
			}
		} else if (col == COL_LINK && val && !strcasecmp(val, "target")) {
			c->linktargetcolor = 1;
			c->code[COL_LINK] = "01;36"; /* never actually used */
		} else if (col >= 0 && val) {
			c->code[col] = val;
		}
	}
}

static const char *get(struct colorizer *c, int col, const char *dflt)
{
	return (c->code && c->code[col]) ? c->code[col] : dflt;
}

void color_init(struct colorizer *c, const struct options *o, int outfd)
{
	c->enabled = 0;
	c->linktargetcolor = 0;
	c->code = NULL;
	c->ext = NULL;
	c->buf = NULL;

	if (o->format != OUT_UNIX) /* tree skips color for -H; other formats later */
		return;

	int nocolor = o->nocolor;
	const char *s = getenv("NO_COLOR");
	if (s && s[0])
		nocolor = 1;

	if (getenv("TERM") == NULL)
		return;

	int cc = getenv("CLICOLOR") != NULL;
	int force = o->forcecolor;
	if (getenv("CLICOLOR_FORCE") != NULL && !nocolor)
		force = 1;

	const char *cs = getenv("TREE_COLORS");
	if (!cs)
		cs = getenv("LS_COLORS");
	if ((cs == NULL || cs[0] == '\0') && (force || cc))
		cs = DEFAULT_MAP;

	if (cs == NULL || (!force && (nocolor || !isatty(outfd))))
		return;

	c->enabled = 1;
	parse(c, cs);
}

void color_free(struct colorizer *c)
{
	struct cext *e = c->ext;
	while (e) {
		struct cext *n = e->nxt;
		free(e);
		e = n;
	}
	free(c->code);
	free(c->buf);
	c->ext = NULL;
	c->code = NULL;
	c->buf = NULL;
}

static int print_color(struct colorizer *c, struct dstr *out, int col)
{
	if (!c->code || !c->code[col])
		return 0;
	dstr_appendz(out, get(c, COL_LEFTCODE, "\033["));
	dstr_appendz(out, c->code[col]);
	dstr_appendz(out, get(c, COL_RIGHTCODE, "m"));
	return 1;
}

int color_apply(struct colorizer *c, struct dstr *out, mode_t mode,
		const char *name, int orphan, int islink)
{
	if (orphan) {
		if (islink) {
			if (print_color(c, out, COL_MISSING))
				return 1;
		} else if (print_color(c, out, COL_ORPHAN)) {
			return 1;
		}
	}

	switch (mode & S_IFMT) {
	case S_IFIFO:
		return print_color(c, out, COL_FIFO);
	case S_IFCHR:
		return print_color(c, out, COL_CHR);
	case S_IFDIR:
		if (mode & S_ISVTX) {
			if ((mode & S_IWOTH) && print_color(c, out, COL_STICKY_OW))
				return 1;
			if (!(mode & S_IWOTH) && print_color(c, out, COL_STICKY))
				return 1;
		}
		if ((mode & S_IWOTH) && print_color(c, out, COL_OW))
			return 1;
		return print_color(c, out, COL_DIR);
	case S_IFBLK:
		return print_color(c, out, COL_BLK);
	case S_IFLNK:
		return print_color(c, out, COL_LINK);
#ifdef S_IFDOOR
	case S_IFDOOR:
		return print_color(c, out, COL_DOOR);
#endif
	case S_IFSOCK:
		return print_color(c, out, COL_SOCK);
	case S_IFREG:
		if ((mode & S_ISUID) && print_color(c, out, COL_SETUID))
			return 1;
		if ((mode & S_ISGID) && print_color(c, out, COL_SETGID))
			return 1;
		if ((mode & (S_IXUSR | S_IXGRP | S_IXOTH)) && print_color(c, out, COL_EXEC))
			return 1;
		size_t l = strlen(name);
		for (struct cext *e = c->ext; e; e = e->nxt) {
			size_t xl = strlen(e->ext);
			if (!strcmp((l > xl) ? name + (l - xl) : name, e->ext)) {
				dstr_appendz(out, get(c, COL_LEFTCODE, "\033["));
				dstr_appendz(out, e->flg);
				dstr_appendz(out, get(c, COL_RIGHTCODE, "m"));
				return 1;
			}
		}
		return print_color(c, out, COL_FILE);
	}
	return print_color(c, out, COL_NORMAL);
}

void color_end(struct colorizer *c, struct dstr *out)
{
	const char *ec = get(c, COL_ENDCODE, NULL);
	if (ec) {
		dstr_appendz(out, ec);
		return;
	}
	/* default end: leftcode + reset + rightcode */
	dstr_appendz(out, get(c, COL_LEFTCODE, "\033["));
	dstr_appendz(out, get(c, COL_RESET, "0"));
	dstr_appendz(out, get(c, COL_RIGHTCODE, "m"));
}
