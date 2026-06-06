#include "options.h"
#include "util.h"
#include "version.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void options_init(struct options *o)
{
	memset(o, 0, sizeof *o);
	o->level = -1; /* unlimited */
	o->filelimit = 0;
	o->sort = SORT_NAME;
	o->format = OUT_UNIX;
}

static void die_msg(const char *msg)
{
	fprintf(stderr, "%s: %s\n", ASP_PROGNAME, msg);
	exit(1);
}

static void die_invalid_long(const char *a)
{
	/* tree dumps full usage here; that text is deferred (see plan.md ledger). */
	fprintf(stderr, "%s: Invalid argument `%s'.\n", ASP_PROGNAME, a);
	fprintf(stderr, "usage: %s [options] [directory ...]\n", ASP_PROGNAME);
	exit(1);
}

static void die_invalid_short(char c)
{
	fprintf(stderr, "%s: Invalid argument -`%c'.\n", ASP_PROGNAME, c);
	fprintf(stderr, "usage: %s [options] [directory ...]\n", ASP_PROGNAME);
	exit(1);
}

static const char *need_arg(int *i, int argc, char **argv, const char *what)
{
	if (*i + 1 >= argc) {
		char buf[64];
		snprintf(buf, sizeof buf, "Missing argument to %s option.", what);
		die_msg(buf);
	}
	return argv[++(*i)];
}

static void add_pat(const char ***arr, size_t *n, size_t *cap, const char *p)
{
	if (*n == *cap) {
		*cap = *cap ? *cap * 2 : 8;
		*arr = asp_xrealloc(*arr, *cap * sizeof **arr);
	}
	(*arr)[(*n)++] = p;
}

static const char *long_val(char *a, const char *pfx, int *i, int argc, char **argv)
{
	size_t len = strlen(pfx);
	if (strncmp(a, pfx, len) != 0)
		return NULL;
	if (a[len] == '=')
		return a + len + 1;
	if (a[len] == '\0') {
		char buf[80];
		snprintf(buf, sizeof buf, "%s", pfx);
		return need_arg(i, argc, argv, buf);
	}
	return NULL; /* a longer flag that merely shares this prefix */
}

#define TOG(f) (o->f = opt_toggle ? !o->f : 1)

static void set_sort(struct options *o, const char *name)
{
	static const struct {
		const char *n;
		enum sort_kind k;
	} tbl[] = { { "name", SORT_NAME }, { "version", SORT_VERSION },
		    { "size", SORT_SIZE }, { "mtime", SORT_MTIME },
		    { "ctime", SORT_CTIME }, { "none", SORT_NONE } };
	for (size_t i = 0; i < sizeof tbl / sizeof tbl[0]; i++)
		if (!strcasecmp(name, tbl[i].n)) {
			o->sort = tbl[i].k;
			return;
		}
	fprintf(stderr, "%s: Sort type '%s' not valid, should be one of: "
			"name,version,size,mtime,ctime,none\n", ASP_PROGNAME, name);
	exit(1);
}

static int parse_long(char *a, int *i, int argc, char **argv,
		      struct options *o, int *optf, int opt_toggle)
{
	const char *v;
	if (!strcmp(a, "--")) {
		*optf = 0;
		return 0;
	}
	if (!strcmp(a, "--help")) {
		printf("usage: %s [options] [directory ...]\n", ASP_PROGNAME);
		exit(0);
	}
	if (!strcmp(a, "--version")) {
		printf("%s v%s\n", ASP_PROGNAME, ASP_VERSION);
		exit(0);
	}
	if (!strcmp(a, "--inodes")) { TOG(inodeflag); return 0; }
	if (!strcmp(a, "--device")) { TOG(devflag); return 0; }
	if (!strcmp(a, "--noreport")) { TOG(noreport); return 0; }
	if (!strcmp(a, "--nolinks")) { TOG(nolinks); return 0; }
	if (!strcmp(a, "--dirsfirst")) { o->dirsfirst = 1; o->filesfirst = 0; return 0; }
	if (!strcmp(a, "--filesfirst")) { o->filesfirst = 1; o->dirsfirst = 0; return 0; }
	if (!strcmp(a, "--si")) { o->siflag = o->humanflag = o->sizeflag = 1; return 0; }
	if (!strcmp(a, "--du")) { o->duflag = o->sizeflag = 1; return 0; }
	if (!strcmp(a, "--prune")) { TOG(prune); return 0; }
	if (!strcmp(a, "--ignore-case")) { TOG(ignorecase); return 0; }
	if (!strcmp(a, "--matchdirs")) { TOG(matchdirs); return 0; }
	if (!strcmp(a, "--metafirst")) { TOG(metafirst); return 0; }
	if (!strcmp(a, "--gitignore")) { TOG(gitignore); return 0; }
	if (!strcmp(a, "--info")) { TOG(showinfo); return 0; }
	if (!strcmp(a, "--fromfile")) { o->fromfile = 1; return 0; }
	if (!strcmp(a, "--fromtabfile")) { o->fromtabfile = 1; return 0; }
	if (!strcmp(a, "--fflinks")) { TOG(fflinks); return 0; }
	if (!strcmp(a, "--hyperlink")) { TOG(hyperlink); return 0; }
	if (!strcmp(a, "--opt-toggle")) return 1; /* signal toggle flip to caller */
	if ((v = long_val(a, "--charset", i, argc, argv))) { o->charset = v; return 0; }
	if ((v = long_val(a, "--filelimit", i, argc, argv))) { o->filelimit = atol(v); return 0; }
	if ((v = long_val(a, "--timefmt", i, argc, argv))) { o->timefmt = v; o->dateflag = 1; return 0; }
	if ((v = long_val(a, "--sort", i, argc, argv))) { set_sort(o, v); return 0; }
	if ((v = long_val(a, "--gitfile", i, argc, argv))) { o->gitignore = 1; o->gitfile = v; return 0; }
	if ((v = long_val(a, "--infofile", i, argc, argv))) { o->showinfo = 1; o->infofile = v; return 0; }
	if ((v = long_val(a, "--hintro", i, argc, argv))) { o->hintro = v; return 0; }
	if ((v = long_val(a, "--houtro", i, argc, argv))) { o->houtro = v; return 0; }
	if ((v = long_val(a, "--scheme", i, argc, argv))) { o->scheme = v; return 0; }
	if ((v = long_val(a, "--authority", i, argc, argv))) { o->authority = v; return 0; }
	die_invalid_long(a);
	return 0;
}

void options_parse(int argc, char **argv, struct options *o,
		   const char ***roots, int *nroots)
{
	const char **rv = asp_xmalloc((size_t)(argc + 2) * sizeof *rv);
	int rn = 0;
	int optf = 1, opt_toggle = 0;

	for (int i = 1; i < argc; i++) {
		char *a = argv[i];
		if (optf && a[0] == '-' && a[1]) {
			if (a[1] == '-') {
				if (parse_long(a, &i, argc, argv, o, &optf, opt_toggle))
					opt_toggle = !opt_toggle;
				continue;
			}
			for (size_t j = 1; a[j]; j++) {
				char c = a[j];
				switch (c) {
				case 'a': TOG(all); break;
				case 'd': TOG(dirsonly); break;
				case 'f': TOG(fullpath); break;
				case 'i': TOG(noindent); break;
				case 'x': TOG(xdev); break;
				case 'l': TOG(follow); break;
				case 'R': TOG(rerun); break;
				case 'F': TOG(classify); break;
				case 's': TOG(sizeflag); break;
				case 'h': o->humanflag = opt_toggle ? !o->humanflag : 1;
					  o->sizeflag = o->humanflag; break;
				case 'u': TOG(userflag); break;
				case 'g': TOG(groupflag); break;
				case 'p': TOG(permflag); break;
				case 'D': TOG(dateflag); break;
				case 'q': TOG(qmark); break;
				case 'N': TOG(noprint); break;
				case 'Q': TOG(quote); break;
				case 'C': TOG(forcecolor); break;
				case 'n': TOG(nocolor); break;
				case 'A': TOG(ansilines); break;
				case 'S': o->charset = "IBM437"; break;
				case 't': o->sort = SORT_MTIME; break;
				case 'c': o->sort = SORT_CTIME; o->ctimeflag = 1; break;
				case 'r': TOG(reverse); break;
				case 'v': o->sort = SORT_VERSION; break;
				case 'U': o->sort = SORT_NONE; break;
				case 'X': o->format = OUT_XML; break;
				case 'J': o->format = OUT_JSON; break;
				case 'H':
					o->format = OUT_HTML;
					o->host = need_arg(&i, argc, argv, "-H");
					if (o->host[0] == '-') { o->htmloffset = 1; o->host++; }
					j = strlen(a) - 1;
					break;
				case 'T': o->title = need_arg(&i, argc, argv, "-T"); j = strlen(a) - 1; break;
				case 'o': o->outfilename = need_arg(&i, argc, argv, "-o"); j = strlen(a) - 1; break;
				case 'P': add_pat(&o->patterns, &o->npat, &o->patcap,
						  need_arg(&i, argc, argv, "-P")); j = strlen(a) - 1; break;
				case 'I': add_pat(&o->ipatterns, &o->nipat, &o->ipatcap,
						  need_arg(&i, argc, argv, "-I")); j = strlen(a) - 1; break;
				case 'L': {
					long lv;
					if (isdigit((unsigned char)a[j + 1])) {
						char buf[32];
						size_t k = 0;
						while (a[j + 1] && isdigit((unsigned char)a[j + 1]) &&
						       k < sizeof buf - 1)
							buf[k++] = a[++j];
						buf[k] = '\0';
						lv = strtol(buf, NULL, 0);
					} else {
						lv = strtol(need_arg(&i, argc, argv, "-L"), NULL, 0);
						j = strlen(a) - 1;
					}
					if (lv < 1)
						die_msg("Invalid level, must be greater than 0.");
					o->level = lv;
					break;
				}
				default:
					die_invalid_short(c);
				}
			}
		} else {
			rv[rn++] = a;
		}
	}

	rv[rn] = NULL;
	*roots = rv;
	*nroots = rn;
}
