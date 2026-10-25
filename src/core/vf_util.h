/* vf_util.h - pure helpers: strings, dates, wildcards. */
#ifndef VF_UTIL_H
#define VF_UTIL_H

#include <stddef.h>
#include "vf_types.h"

/* Copy at most size-1 characters and always terminate. Returns strlen(src). */
size_t vf_strlcpy(char *dst, const char *src, size_t size);
size_t vf_strlcat(char *dst, const char *src, size_t size);
int vf_strcasecmp(const char *a, const char *b);
int vf_strncasecmp(const char *a, const char *b, size_t n);
/* Blanks off both ends, in place. Returns s. */
char *vf_trim(char *s);
/* Upper case, in place. Returns s. */
char *vf_upper(char *s);

/* "24-OCT-2026 15:02" */
char *vf_fmt_when(const vf_when *w, char *buf, size_t size);
/* "1-JAN-2027" or " 1-JAN-2027" (any case) -> 1; *days = days since 1970-01-01.
   Checks the day against the month. */
int vf_parse_date(const char *s, long *days);
long vf_days(int y, int m, int d);

/* VMS-style wildcard match, case-blind: * any run, % one character. */
int vf_match(const char *pattern, const char *s);

#endif
