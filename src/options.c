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

/* Usage/help text — byte-for-byte tree 2.3.2's, with the program name our own.
 * tree builds this with fancy() markup (\b bold, \f italic, \r reset) that is
 * elided whenever colorization is off, i.e. on any pipe — which is exactly what
 * the golden differ sees, so we emit the plain text directly. The synopsis goes
 * to `out` (stderr for a bad flag, stdout for --help); the body always to
 * stdout. `full` distinguishes --help (1) from a usage-on-error blurb (0). The
 * --acl/--selinux lines are Linux-only in tree; mirror that with __linux__. */
static void asp_usage(FILE *out, int full)
{
	fprintf(out,
		"usage: %s [-acdfghilnpqrstuvxACDFJQNSUX] [-L level [-R]] [-H [-]baseHREF]\n"
		"\t[-T title] [-o filename] [-P pattern] [-I pattern] [--gitignore]\n"
		"\t[--gitfile[=]file] [--matchdirs] [--metafirst] [--ignore-case]\n"
		"\t[--nolinks] [--hintro[=]file] [--houtro[=]file] [--inodes] [--device]\n"
		"\t[--sort[=]name] [--dirsfirst] [--filesfirst] [--filelimit[=]#] [--si]\n"
		"\t[--du] [--prune] [--charset[=]X] [--timefmt[=]format] [--fromfile]\n"
		"\t[--fromtabfile] [--fflinks] [--info] [--infofile[=]file] [--noreport]\n"
		"\t[--hyperlink] [--scheme[=]schema] [--authority[=]host] [--opt-toggle]\n"
		"\t[--compress[=]#] [--condense] [--version] [--help]"
#ifdef __linux__
		" [--acl] [--selinux]\n"
#else
		"\n"
#endif
		"\t[--] [directory ...]\n",
		ASP_PROGNAME);

	if (!full)
		return;

	fputs(
		"  ------- Listing options -------\n"
		"  -a            All files are listed.\n"
		"  -d            List directories only.\n"
		"  -l            Follow symbolic links like directories.\n"
		"  -f            Print the full path prefix for each file.\n"
		"  -x            Stay on current filesystem only.\n"
		"  -L level      Descend only level directories deep.\n"
		"  -R            Rerun tree when max dir level reached.\n"
		"  -P pattern    List only those files that match the pattern given.\n"
		"  -I pattern    Do not list files that match the given pattern.\n"
		"  --gitignore   Filter by using .gitignore files.\n"
		"  --gitfile X   Explicitly read a gitignore file.\n"
		"  --ignore-case Ignore case when pattern matching.\n"
		"  --matchdirs   Include directory names in -P pattern matching.\n"
		"  --metafirst   Print meta-data at the beginning of each line.\n"
		"  --prune       Prune empty directories from the output.\n"
		"  --info        Print information about files found in .info files.\n"
		"  --infofile X  Explicitly read info file.\n"
		"  --noreport    Turn off file/directory count at end of tree listing.\n"
		"  --charset X   Use charset X for terminal/HTML and indentation line output.\n"
		"  --filelimit # Do not descend dirs with more than # files in them.\n"
		"  --condense    Condense directory singletons to a single line of output.\n"
		"  -o filename   Output to file instead of stdout.\n"
		"  ------- File options -------\n"
		"  -q            Print non-printable characters as '?'.\n"
		"  -N            Print non-printable characters as is.\n"
		"  -Q            Quote filenames with double quotes.\n"
		"  -p            Print the protections for each file.\n"
		"  -u            Displays file owner or UID number.\n"
		"  -g            Displays file group owner or GID number.\n"
		"  -s            Print the size in bytes of each file.\n"
		"  -h            Print the size in a more human readable way.\n"
		"  --si          Like -h, but use in SI units (powers of 1000).\n"
		"  --du          Compute size of directories by their contents.\n"
		"  -D            Print the date of last modification or (-c) status change.\n"
		"  --timefmt fmt Print and format time according to the format fmt.\n"
		"  -F            Appends '/', '=', '*', '@', '|' or '>' as per ls -F.\n"
		"  --inodes      Print inode number of each file.\n"
		"  --device      Print device ID number to which each file belongs.\n"
#ifdef __linux__
		"  --acl         Print permissions with a + if an ACL is present.\n"
		"  --selinux     Print the selinux security label if present.\n"
#endif
		, stdout);

	fputs(
		"  ------- Sorting options -------\n"
		"  -v            Sort files alphanumerically by version.\n"
		"  -t            Sort files by last modification time.\n"
		"  -c            Sort files by last status change time.\n"
		"  -U            Leave files unsorted.\n"
		"  -r            Reverse the order of the sort.\n"
		"  --dirsfirst   List directories before files (-U disables).\n"
		"  --filesfirst  List files before directories (-U disables).\n"
		"  --sort X      Select sort: name,version,size,mtime,ctime,none.\n"
		"  ------- Graphics options -------\n"
		"  -i            Don't print indentation lines.\n"
		"  -A            Print ANSI lines graphic indentation lines.\n"
		"  -S            Print with CP437 (console) graphics indentation lines.\n"
		"  -n            Turn colorization off always (-C overrides).\n"
		"  -C            Turn colorization on always.\n"
		"  --compress #  Compress indentation lines.\n"
		"  ------- XML/HTML/JSON/HYPERLINK options -------\n"
		"  -X            Prints out an XML representation of the tree.\n"
		"  -J            Prints out an JSON representation of the tree.\n"
		"  -H baseHREF   Prints out HTML format with baseHREF as top directory.\n"
		"  -T string     Replace the default HTML title and H1 header with string.\n"
		"  --nolinks     Turn off hyperlinks in HTML output.\n"
		"  --hintro X    Use file X as the HTML intro.\n"
		"  --houtro X    Use file X as the HTML outro.\n"
		"  --hyperlink   Turn on OSC 8 terminal hyperlinks.\n"
		"  --scheme X    Set OSC 8 hyperlink scheme, default file://\n"
		"  --authority X Set OSC 8 hyperlink authority/hostname.\n"
		"  ------- Input options -------\n"
		"  --fromfile    Reads paths from files (.=stdin)\n"
		"  --fromtabfile Reads trees from tab indented files (.=stdin)\n"
		"  --fflinks     Process link information when using --fromfile.\n"
		"  ------- Miscellaneous options -------\n"
		"  --opt-toggle  Enable option toggling.\n"
		"  --version     Print version and exit.\n"
		"  --help        Print usage and this help message and exit.\n"
		"  --            Options processing terminator.\n"
		, stdout);
}

static void die_invalid_long(const char *a)
{
	fprintf(stderr, "%s: Invalid argument `%s'.\n", ASP_PROGNAME, a);
	asp_usage(stderr, 0);
	exit(1);
}

static void die_invalid_short(char c)
{
	fprintf(stderr, "%s: Invalid argument -`%c'.\n", ASP_PROGNAME, c);
	asp_usage(stderr, 0);
	exit(1);
}

static const char *need_arg(int *i, int argc, char **argv, const char *what)
{
	if (*i + 1 >= argc) {
		char buf[128]; /* fits the 28-char literal + any option name */
		snprintf(buf, sizeof buf, "Missing argument to %s option.", what);
		die_msg(buf);
	}
	return argv[++(*i)];
}

/* Argument for an arg-taking short flag, getopt-style: the glued remainder of
 * the token (-Pfoo -> "foo") if present, else the next argv (-P foo). Mirrors
 * -L's glued-digit handling and ends the cluster either way. (DEVIATION D5: tree
 * has no glued-arg form — it takes the next argv and then parses the remainder
 * as MORE short flags, so -Pfoo misparses; aspen never silently drops it.) */
static const char *short_arg(char *a, size_t *j, int *i, int argc, char **argv,
			     const char *what)
{
	const char *v = (a[*j + 1] != '\0') ? a + *j + 1
					    : need_arg(i, argc, argv, what);
	*j = strlen(a) - 1; /* consume the rest of this token */
	return v;
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
		asp_usage(stdout, 1);
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
	if ((v = long_val(a, "--threads", i, argc, argv))) { o->threads = atoi(v); if (o->threads < 0) o->threads = 0; return 0; }
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
					o->host = short_arg(a, &j, &i, argc, argv, "-H");
					if (o->host[0] == '-') { o->htmloffset = 1; o->host++; }
					break;
				case 'T': o->title = short_arg(a, &j, &i, argc, argv, "-T"); break;
				case 'o': o->outfilename = short_arg(a, &j, &i, argc, argv, "-o"); break;
				case 'P': add_pat(&o->patterns, &o->npat, &o->patcap,
						  short_arg(a, &j, &i, argc, argv, "-P")); break;
				case 'I': add_pat(&o->ipatterns, &o->nipat, &o->ipatcap,
						  short_arg(a, &j, &i, argc, argv, "-I")); break;
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
