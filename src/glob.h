#ifndef ASP_GLOB_H
#define ASP_GLOB_H

/*
 * tree's custom globber (patmatch), ported exactly — NOT fnmatch. Supports
 * literals, ?, * (stops at '/'), ** (crosses '/', for gitignore), [..]/[^..]
 * classes with ranges and '\' escapes, '|' alternation, and a trailing '/' that
 * makes the pattern directory-only. Returns 1 match, 0 mismatch, -1 syntax error.
 *
 * NOTE: like tree, the '|' split rewrites `pat` in place and restores it, so pat
 * must be writable (argv strings are). ic = case-insensitive (--ignore-case).
 */
int asp_patmatch(const char *buf, char *pat, int isdir, int ic);

#endif /* ASP_GLOB_H */
