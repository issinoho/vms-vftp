/* vf_util.c - pure helpers. No I/O. */
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "vf_util.h"

size_t vf_strlcpy(char *dst, const char *src, size_t size)
{
    size_t n = strlen(src);
    if (size) {
        size_t c = n < size - 1 ? n : size - 1;
        memcpy(dst, src, c);
        dst[c] = '\0';
    }
    return n;
}

size_t vf_strlcat(char *dst, const char *src, size_t size)
{
    size_t d = strlen(dst);
    if (d >= size) return d + strlen(src);
    return d + vf_strlcpy(dst + d, src, size - d);
}

int vf_strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int ca = toupper((unsigned char) *a), cb = toupper((unsigned char) *b);
        if (ca != cb) return ca - cb;
        if (!ca) return 0;
    }
    return 0;
}

int vf_strcasecmp(const char *a, const char *b) { return vf_strncasecmp(a, b, (size_t) -1); }

char *vf_trim(char *s)
{
    size_t n = strlen(s), i = 0;
    while (n && isspace((unsigned char) s[n - 1])) s[--n] = '\0';
    while (s[i] && isspace((unsigned char) s[i])) i++;
    if (i) memmove(s, s + i, n - i + 1);
    return s;
}

char *vf_upper(char *s)
{
    char *p;
    for (p = s; *p; p++) *p = (char) toupper((unsigned char) *p);
    return s;
}

static const char *months[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

char *vf_fmt_when(const vf_when *w, char *buf, size_t size)
{
    snprintf(buf, size, "%2d-%s-%04d %02d:%02d", w->d,
             w->m >= 1 && w->m <= 12 ? months[w->m - 1] : "???", w->y, w->hh, w->mm);
    return buf;
}

long vf_days(int y, int m, int d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int vf_parse_date(const char *s, long *days)
{
    int d = 0, y = 0, m = 0, i, nd = 0, ny = 0;
    static const int mdays[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    while (*s == ' ') s++;
    for (; isdigit((unsigned char) *s) && nd < 2; s++, nd++) d = d * 10 + (*s - '0');
    if (!nd || *s++ != '-') return 0;
    for (i = 0; i < 12; i++) if (vf_strncasecmp(s, months[i], 3) == 0) { m = i + 1; break; }
    if (!m) return 0;
    s += 3;
    if (*s++ != '-') return 0;
    for (; isdigit((unsigned char) *s) && ny < 4; s++, ny++) y = y * 10 + (*s - '0');
    if (ny != 4) return 0;
    while (*s == ' ') s++;
    if (*s) return 0;
    if (d < 1 || d > mdays[m - 1]) return 0;
    if (m == 2 && d == 29 && !((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 0;
    if (days) *days = vf_days(y, m, d);
    return 1;
}

int vf_match(const char *p, const char *s)
{
    if (!*p) return !*s;
    if (*p == '*') {
        for (;; s++) {
            if (vf_match(p + 1, s)) return 1;
            if (!*s) return 0;
        }
    }
    if (!*s) return 0;
    if (*p == '%' || toupper((unsigned char) *p) == toupper((unsigned char) *s))
        return vf_match(p + 1, s + 1);
    return 0;
}

int vf_shquote(const char *s, char *out, size_t size)
{
    size_t n = 0;
    if (size < 3) { if (size) out[0] = '\0'; return 0; }
    out[n++] = '\'';
    for (; *s; s++) {
        if (*s == '\'') {
            if (n + 4 >= size) { out[0] = '\0'; return 0; }
            memcpy(out + n, "'\\''", 4);
            n += 4;
        } else {
            if (n + 1 >= size) { out[0] = '\0'; return 0; }
            out[n++] = *s;
        }
    }
    if (n + 2 > size) { out[0] = '\0'; return 0; }
    out[n++] = '\'';
    out[n] = '\0';
    return 1;
}
