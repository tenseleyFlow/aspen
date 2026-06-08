#include "charset.h"

#include <langinfo.h>
#include <stdlib.h>
#include <strings.h>

/* Names + [0] connector forms transcribed from tree 2.3.2 color.c cstable. */
static const char *n_ansi[] = { "ANSI", NULL };
static const char *n_latin13[] = {
	"ISO-8859-1", "ISO-8859-1:1987", "ISO_8859-1", "latin1", "l1", "IBM819",
	"CP819", "csISOLatin1", "ISO-8859-3", "ISO_8859-3:1988", "ISO_8859-3",
	"latin3", "ls", "csISOLatin3", NULL
};
static const char *n_iso789[] = {
	"ISO-8859-7", "ISO_8859-7:1987", "ISO_8859-7", "ELOT_928", "ECMA-118",
	"greek", "greek8", "csISOLatinGreek", "ISO-8859-8", "ISO_8859-8:1988",
	"iso-ir-138", "ISO_8859-8", "hebrew", "csISOLatinHebrew", "ISO-8859-9",
	"ISO_8859-9:1989", "iso-ir-148", "ISO_8859-9", "latin5", "l5",
	"csISOLatin5", NULL
};
static const char *n_sjis[] = { "Shift_JIS", "MS_Kanji", "csShiftJIS", NULL };
static const char *n_eucjp[] = {
	"EUC-JP", "Extended_UNIX_Code_Packed_Format_for_Japanese",
	"csEUCPkdFmtJapanese", NULL
};
static const char *n_euckr[] = { "EUC-KR", "csEUCKR", NULL };
static const char *n_2022jp[] = {
	"ISO-2022-JP", "csISO2022JP", "ISO-2022-JP-2", "csISO2022JP2", NULL
};
static const char *n_ibmpc[] = {
	"IBM437", "cp437", "437", "csPC8CodePage437", "IBM852", "cp852", "852",
	"csPCp852", "IBM863", "cp863", "863", "csIBM863", "IBM855", "cp855",
	"855", "csIBM855", "IBM865", "cp865", "865", "csIBM865", "IBM866",
	"cp866", "866", "csIBM866", NULL
};
static const char *n_ibmps2[] = {
	"IBM850", "cp850", "850", "csPC850Multilingual", "IBM00858", "CCSID00858",
	"CP00858", "PC-Multilingual-850+euro", NULL
};
static const char *n_ibmgr[] = { "IBM869", "cp869", "869", "cp-gr", "csIBM869", NULL };
static const char *n_gb[] = { "GB2312", "csGB2312", NULL };
static const char *n_utf8[] = { "UTF-8", "utf8", NULL };
static const char *n_big5[] = { "Big5", "csBig5", NULL };
static const char *n_viscii[] = { "VISCII", "csVISCII", NULL };
static const char *n_koi8[] = { "KOI8-R", "csKOI8R", "KOI8-U", NULL };
static const char *n_win[] = {
	"ISO-8859-1-Windows-3.1-Latin-1", "csWindows31Latin1",
	"ISO-8859-2-Windows-Latin-2", "csWindows31Latin2", "windows-1250",
	"windows-1251", "windows-1253", "windows-1254", "windows-1255",
	/* tree's color.c lists "windows-1256" twice; reproduced verbatim so the
	 * --charset valid-list output is byte-identical (harmless for name matching). */
	"windows-1256", "windows-1256", "windows-1257", NULL
};

/* Most charsets use " [" for all five comment decorators (only UTF-8 differs). */
#define CDEC " [", " [", " [", " [", " ["

/* The three connector triples (widest..narrowest) per --compress level. Two
 * ASCII families recur: '+'-corner (latin/iso/greek) and '`'-corner (viscii/
 * windows/fallback); IBM PC/PS2/GR share the \263/\303/\300 triple. */
#define LD_ASCII_PLUS \
	{ "|  ", "| ", "|" }, { "|--", "|-", "+" }, { "&middot;--", "&middot;-", "&middot;" }
#define LD_ASCII_BT \
	{ "|  ", "| ", "|" }, { "|--", "|-", "+" }, { "`--", "`-", "`" }
#define LD_IBM \
	{ "\263  ", "\263 ", "\263" }, { "\303\304\304", "\303\304", "\303" }, \
	{ "\300\304\304", "\300\304", "\300" }

static const struct {
	const char **names;
	struct linedraw ld;
} cstable[] = {
	{ n_ansi,    { { "\033(0\170  \033(B", "\033(0\170 \033(B", "\033(0\170\033(B" },
		       { "\033(0\164\161\161\033(B", "\033(0\164\161\033(B", "\033(0\164\033(B" },
		       { "\033(0\155\161\161\033(B", "\033(0\155\161\033(B", "\033(0\155\033(B" }, CDEC } },
	{ n_latin13, { LD_ASCII_PLUS, CDEC } },
	{ n_iso789,  { LD_ASCII_PLUS, CDEC } },
	{ n_sjis,    { { "\204\240  ", "\204\240 ", "\204\240" },
		       { "\204\245\204\237\204\237", "\204\245\204\237", "\204\245" },
		       { "\204\244\204\237\204\237", "\204\244\204\237", "\204\244" }, CDEC } },
	{ n_eucjp,   { { "\250\242  ", "\250\242 ", "\250\242" },
		       { "\250\247\250\241\250\241", "\250\247\250\241", "\250\247" },
		       { "\250\246\250\241\250\241", "\250\246\250\241", "\250\246" }, CDEC } },
	{ n_euckr,   { { "\246\242  ", "\246\242 ", "\246\242" },
		       { "\246\247\246\241\246\241", "\246\247\246\241", "\246\247" },
		       { "\246\246\246\241\246\241", "\246\246\246\241", "\246\246" }, CDEC } },
	{ n_2022jp,  { { "\033$B(\"\033(B  ", "\033$B(\"\033(B ", "\033$B(\"\033(B" },
		       { "\033$B('\033$B(!\033$B(!\033(B", "\033$B('\033$B(!\033(B", "\033$B('\033(B" },
		       { "\033$B(&\033$B(!\033$B(!\033(B", "\033$B(&\033$B(!\033(B", "\033$B(&\033(B" }, CDEC } },
	{ n_ibmpc,   { LD_IBM, CDEC } },
	{ n_ibmps2,  { LD_IBM, CDEC } },
	{ n_ibmgr,   { LD_IBM, CDEC } },
	{ n_gb,      { { "\251\246  ", "\251\246 ", "\251\246" },
		       { "\251\300\251\244\251\244", "\251\300\251\244", "\251\300" },
		       { "\251\270\251\244\251\244", "\251\270\251\244", "\251\270" }, CDEC } },
	{ n_utf8,    { { "\342\224\202\302\240\302\240", "\342\224\202\302\240", "\342\224\202" },
		       { "\342\224\234\342\224\200\342\224\200", "\342\224\234\342\224\200", "\342\224\234" },
		       { "\342\224\224\342\224\200\342\224\200", "\342\224\224\342\224\200", "\342\224\224" },
		       " \342\216\247", " \342\216\251", " \342\216\250", " \342\216\252", " {" } },
	{ n_big5,    { { "\242x  ", "\242x ", "\242x" },
		       { "\242u\242\167\242\167", "\242u\242\167", "\242u" },
		       { "\242|\242\167\242\167", "\242|\242\167", "\242|" }, CDEC } },
	{ n_viscii,  { LD_ASCII_BT, CDEC } },
	{ n_koi8,    { { "\201  ", "\201 ", "\201" }, { "\206\200\200", "\206\200", "\206" },
		       { "\204\200\200", "\204\200", "\204" }, CDEC } },
	{ n_win,     { LD_ASCII_BT, CDEC } },
	{ NULL,      { LD_ASCII_BT, CDEC } }, /* ASCII fallback */
};
#define NCS (sizeof cstable / sizeof cstable[0])

static int is_utf8(const char *cs)
{
	return cs && (!strcasecmp(cs, "UTF-8") || !strcasecmp(cs, "utf8"));
}

void asp_charset_list(FILE *f)
{
	/* tree's initlinedraw(true): the header, then every name in every table row
	 * (the trailing names==NULL fallback row is skipped), 2-space indented. */
	fprintf(f, "Valid charsets include:\n");
	for (size_t i = 0; i < NCS; i++) {
		const char **s = cstable[i].names;
		if (!s)
			continue;
		for (; *s; ++s)
			fprintf(f, "  %s\n", *s);
	}
}

const char *asp_charset_name(const struct options *o)
{
	const char *cs = o->charset; /* --charset / -S */
	if (!cs)
		cs = getenv("TREE_CHARSET");
	if (!cs) {
		const char *codeset = nl_langinfo(CODESET);
		if (is_utf8(codeset))
			cs = "UTF-8";
	}
	return cs;
}

const struct linedraw *asp_linedraw(const struct options *o)
{
	if (o->ansilines)
		return &cstable[0].ld; /* ANSI */

	const char *cs = asp_charset_name(o);

	if (cs)
		for (size_t i = 0; i < NCS; i++)
			for (const char **n = cstable[i].names; n && *n; n++)
				if (!strcasecmp(cs, *n))
					return &cstable[i].ld;

	return &cstable[NCS - 1].ld; /* ASCII fallback */
}
