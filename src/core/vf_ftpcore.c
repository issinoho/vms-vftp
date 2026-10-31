/* vf_ftpcore.c - FTP replies and LIST lines; no I/O (D4). See vf_ftpcore.h. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vf_ftpcore.h"
#include "vf_util.h"

/* ---------- replies ---------- */

void vf_ftp_reply_init(vf_ftp_reply *r)
{
    memset(r, 0, sizeof *r);
}

static int three_digits(const char *s)
{
    return isdigit((unsigned char) s[0]) && isdigit((unsigned char) s[1]) && isdigit((unsigned char) s[2]);
}

static void add_text(vf_ftp_reply *r, const char *t)
{
    if (r->text[0]) vf_strlcat(r->text, "\n", sizeof r->text);
    vf_strlcat(r->text, t, sizeof r->text);
}

int vf_ftp_reply_line(vf_ftp_reply *r, const char *line)
{
    int starts = three_digits(line) && (line[3] == ' ' || line[3] == '-' || line[3] == '\0');
    if (r->done) vf_ftp_reply_init(r);
    if (!r->code) {
        if (!starts) { add_text(r, line); return 0; }
        r->code = atoi(line);
        r->multi = line[3] == '-';
        add_text(r, line[3] ? line + 4 : "");
        r->done = !r->multi;
        return r->done;
    }
    /* inside a multi-line reply: it ends at "ddd " with the same code */
    if (starts && atoi(line) == r->code && line[3] != '-') {
        add_text(r, line[3] ? line + 4 : "");
        r->done = 1;
        return 1;
    }
    add_text(r, starts ? line + 4 : line);
    return 0;
}

void vf_ftp_reply_msg(const vf_ftp_reply *r, char *out, size_t size)
{
    const char *last = strrchr(r->text, '\n');
    char first[512];
    size_t n;
    if (!last) { vf_strlcpy(out, r->text, size); return; }
    n = (size_t) (strchr(r->text, '\n') - r->text);
    if (n >= sizeof first) n = sizeof first - 1;
    memcpy(first, r->text, n);
    first[n] = '\0';
    vf_trim(first);
    snprintf(out, size, "%s (%s)", last + 1, first);
}

/* ---------- PASV, EPSV, PWD, 150 ---------- */

int vf_ftp_pasv(const char *text, char *host, size_t hsize, int *port)
{
    const char *p;
    int v[6], i;
    for (p = text; *p; p++) {
        const char *q = p;
        if (!isdigit((unsigned char) *p) || (p > text && isdigit((unsigned char) p[-1]))) continue;
        for (i = 0; i < 6; i++) {
            char *end;
            long n = strtol(q, &end, 10);
            if (end == q || n < 0 || n > 255) break;
            v[i] = (int) n;
            q = end;
            if (i < 5) { if (*q != ',') break; q++; }
        }
        if (i == 6) {
            snprintf(host, hsize, "%d.%d.%d.%d", v[0], v[1], v[2], v[3]);
            *port = v[4] * 256 + v[5];
            return 1;
        }
    }
    return 0;
}

int vf_ftp_epsv(const char *text, int *port)
{
    const char *p = strchr(text, '(');
    char d;
    long n;
    char *end;
    if (!p || !p[1]) return 0;
    d = p[1];                                          /* the delimiter, usually '|' */
    if (p[2] != d || p[3] != d) return 0;
    n = strtol(p + 4, &end, 10);
    if (end == p + 4 || *end != d || n <= 0 || n > 65535) return 0;
    *port = (int) n;
    return 1;
}

int vf_ftp_pwd(const char *text, char *out, size_t size)
{
    const char *p = strchr(text, '"');
    size_t o = 0;
    if (!p) return 0;
    for (p++; *p; p++) {
        if (*p == '"') {
            if (p[1] != '"') { out[o] = '\0'; return o > 0; }
            p++;
        }
        if (o + 1 < size) out[o++] = *p;
    }
    return 0;
}

int vf_ftp_size150(const char *text, unsigned long long *bytes)
{
    const char *p = strstr(text, " bytes)");
    const char *q;
    if (!p) return 0;
    for (q = p; q > text && isdigit((unsigned char) q[-1]); q--) ;
    if (q == p || q == text || q[-1] != '(') return 0;
    *bytes = strtoull(q, 0, 10);
    return 1;
}

/* ---------- names ---------- */

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = toupper(c);
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

void vf_ftp_unescape(const char *name, char *out, size_t size)
{
    size_t o = 0;
    const char *p;
    for (p = name; *p && o + 1 < size; p++) {
        char c = *p;
        if (c == '^' && p[1]) {
            int h = hexval((unsigned char) p[1]), l = p[1] ? hexval((unsigned char) p[2]) : -1;
            if (h >= 0 && l >= 0) { c = (char) (h * 16 + l); p += 2; }   /* ^20 */
            else { c = *++p; if (c == '_') c = ' '; }                   /* ^_ ^. ^] */
        }
        out[o++] = c;
    }
    out[o] = '\0';
}

/* ---------- LIST ---------- */

void vf_ftp_list_init(vf_ftp_list *l, int year, int month)
{
    memset(l, 0, sizeof *l);
    l->year = year;
    l->month = month;
}

static const char months[] = "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC";

static int month_of(const char *s)
{
    int i;
    for (i = 0; i < 12; i++) if (!vf_strncasecmp(s, months + i * 3, 3)) return i + 1;
    return 0;
}

/* Split on blanks: up to max words, their starts in w[] (the line is not changed). */
static int words(const char *line, const char **w, int *len, int max)
{
    int n = 0;
    const char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        w[n] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        len[n] = (int) (p - w[n]);
        n++;
    }
    return n;
}

static void copy_word(char *dst, size_t size, const char *w, int len)
{
    if ((size_t) len >= size) len = (int) size - 1;
    memcpy(dst, w, (size_t) len);
    dst[len] = '\0';
}

/* "NAME.TYPE;V": a semicolon and digits at the end */
static int vms_name(const char *w, int len)
{
    const char *semi = 0;
    int i;
    for (i = 0; i < len; i++) if (w[i] == ';') semi = w + i;
    if (!semi || semi == w || semi + 1 == w + len) return 0;
    for (semi++; semi < w + len; semi++) if (!isdigit((unsigned char) *semi)) return 0;
    return 1;
}

/* the rest of a VMS entry: "used/alloc D-MMM-YYYY HH:MM:SS [G,O] (S,O,G,W)" */
static int vms_rest(const char *rest, vf_entry *e)
{
    const char *w[8];
    int len[8], n, i, d, y, hh, mm;
    char t[64];
    n = words(rest, w, len, 8);
    if (n < 1 || !isdigit((unsigned char) w[0][0])) return 0;
    e->blocks = (unsigned int) strtoul(w[0], 0, 10);     /* used (of used/alloc) */
    e->size = (unsigned long long) e->blocks * 512;
    e->has_size = 1;                                     /* in blocks: see the pane */
    for (i = 1; i < n; i++) {
        copy_word(t, sizeof t, w[i], len[i]);
        if (sscanf(t, "%d-", &d) == 1 && strchr(t, '-') && month_of(strchr(t, '-') + 1)
            && sscanf(strrchr(t, '-') + 1, "%d", &y) == 1) {
            e->mtime.d = d; e->mtime.m = month_of(strchr(t, '-') + 1); e->mtime.y = y;
            e->has_time = 1;
        } else if (sscanf(t, "%d:%d", &hh, &mm) == 2 && e->has_time) {
            e->mtime.hh = hh; e->mtime.mm = mm;
        } else if (t[0] == '[') {
            char *close = strchr(t, ']');
            if (close) *close = '\0';
            vf_strlcpy(e->owner, t + 1, sizeof e->owner);
        } else if (t[0] == '(') {
            vf_strlcpy(e->prot, t, sizeof e->prot);
        }
    }
    return 1;
}

static int vms_finish(vf_ftp_list *l, const char *name, const char *rest, vf_entry *e)
{
    const char *semi;
    memset(e, 0, sizeof *e);
    vf_strlcpy(e->name, name, sizeof e->name);
    if (!vms_rest(rest, e)) {
        /* "%RMS-E-PRV, insufficient privilege" in place of the sizes: the name alone */
        memset(e, 0, sizeof *e);
        vf_strlcpy(e->name, name, sizeof e->name);
    }
    semi = strrchr(e->name, ';');
    if (semi && semi - e->name >= 4 && !vf_strncasecmp(semi - 4, ".DIR", 4)) e->is_dir = 1;
    l->style = VF_LS_VMS;
    return 1;
}

static unsigned int unix_perms(const char *w)
{
    unsigned int m = 0;
    int i;
    for (i = 1; i < 10; i++) {
        m <<= 1;
        if (w[i] != '-') m |= 1;
    }
    if (w[3] == 's' || w[3] == 'S') m |= 04000;
    if (w[6] == 's' || w[6] == 'S') m |= 02000;
    if (w[9] == 't' || w[9] == 'T') m |= 01000;
    if (w[3] == 'S') m &= ~0100u;
    if (w[6] == 'S') m &= ~010u;
    if (w[9] == 'T') m &= ~01u;
    return m;
}

/* "drwxr-xr-x 2 user group 4096 Oct 31 15:31 name" (or "Oct 31  2025"; the
   group may be missing, as on some servers) */
static int unix_line(vf_ftp_list *l, const char *line, vf_entry *e)
{
    const char *w[10];
    int len[10], n, i, mon = 0, day;
    char t[64];
    if (!strchr("-dlbcps", line[0]) || strlen(line) < 10) return 0;
    for (i = 1; i < 10; i++) if (!strchr("-rwxsStTlL", line[i])) return 0;
    n = words(line, w, len, 10);
    /* find the month word: the size is before it, the name after day and time */
    for (i = 3; i + 2 < n; i++) {
        copy_word(t, sizeof t, w[i], len[i]);
        if (len[i] == 3 && (mon = month_of(t)) != 0 && isdigit((unsigned char) w[i - 1][0])) break;
        mon = 0;
    }
    if (!mon) return 0;
    memset(e, 0, sizeof *e);
    e->is_dir = line[0] == 'd';
    e->is_link = line[0] == 'l';
    e->perms = unix_perms(line);
    e->size = strtoull(w[i - 1], 0, 10);
    e->has_size = 1;
    copy_word(e->owner, sizeof e->owner, w[2], len[2]);
    day = atoi(w[i + 1]);
    copy_word(t, sizeof t, w[i + 2], len[i + 2]);
    e->mtime.m = mon; e->mtime.d = day;
    if (strchr(t, ':')) {
        sscanf(t, "%d:%d", &e->mtime.hh, &e->mtime.mm);
        e->mtime.y = l->year;
        if (mon > l->month) e->mtime.y--;               /* "Dec 20" in January */
    } else {
        e->mtime.y = atoi(t);
    }
    e->has_time = 1;
    {
        /* the name is the rest of the line after the time or year */
        const char *p = w[i + 2] + len[i + 2];
        char *arrow;
        if (*p == ' ') p++;
        vf_strlcpy(e->name, p, sizeof e->name);
        if (e->is_link && (arrow = strstr(e->name, " -> ")) != 0) *arrow = '\0';
    }
    if (!strcmp(e->name, ".") || !strcmp(e->name, "..") || !e->name[0]) return 0;
    l->style = VF_LS_UNIX;
    return 1;
}

/* "10-31-26  03:31PM       <DIR>          SUBDIR" or "...  1234 file.txt" (IIS) */
static int dos_line(vf_ftp_list *l, const char *line, vf_entry *e)
{
    int mo, d, y, hh, mm;
    char ap[3];
    const char *w[4];
    int len[4], n;
    if (sscanf(line, "%d-%d-%d %d:%d%2s", &mo, &d, &y, &hh, &mm, ap) != 6) return 0;
    n = words(line, w, len, 4);
    if (n < 4) return 0;
    memset(e, 0, sizeof *e);
    if (y < 100) y += y < 70 ? 2000 : 1900;
    if (toupper((unsigned char) ap[0]) == 'P' && hh < 12) hh += 12;
    if (toupper((unsigned char) ap[0]) == 'A' && hh == 12) hh = 0;
    e->mtime.y = y; e->mtime.m = mo; e->mtime.d = d; e->mtime.hh = hh; e->mtime.mm = mm;
    e->has_time = 1;
    if (len[2] == 5 && !strncmp(w[2], "<DIR>", 5)) e->is_dir = 1;
    else { e->size = strtoull(w[2], 0, 10); e->has_size = 1; }
    vf_strlcpy(e->name, w[3], sizeof e->name);          /* the rest: names may hold blanks */
    if (!strcmp(e->name, ".") || !strcmp(e->name, "..")) return 0;
    l->style = VF_LS_DOS;
    return 1;
}

int vf_ftp_list_line(vf_ftp_list *l, const char *line, vf_entry *e)
{
    const char *w[2];
    int len[2], n;
    char name[256];
    if (l->held[0]) {
        /* the line after a long VMS name: its sizes, dates and protection */
        vf_strlcpy(name, l->held, sizeof name);
        l->held[0] = '\0';
        if (line[0] == ' ' || line[0] == '\t') return vms_finish(l, name, line, e);
        vms_finish(l, name, "", e);       /* none came: the name alone, and this line next */
        vf_strlcpy(l->again, line, sizeof l->again);
        return 1;
    }
    n = words(line, w, len, 2);
    if (n == 0) return 0;
    if (l->style != VF_LS_UNIX && l->style != VF_LS_DOS && vms_name(w[0], len[0])) {
        copy_word(name, sizeof name, w[0], len[0]);
        if (n == 1) { vf_strlcpy(l->held, name, sizeof l->held); return 0; }
        return vms_finish(l, name, w[0] + len[0], e);
    }
    if (l->style != VF_LS_VMS && l->style != VF_LS_DOS && unix_line(l, line, e)) return 1;
    if (l->style != VF_LS_VMS && l->style != VF_LS_UNIX && dos_line(l, line, e)) return 1;
    return 0;                       /* "Directory X", "Total of N files", "total 12", ... */
}

int vf_ftp_list_more(vf_ftp_list *l, vf_entry *e)
{
    char line[512];
    if (!l->again[0]) return 0;
    vf_strlcpy(line, l->again, sizeof line);
    l->again[0] = '\0';
    return vf_ftp_list_line(l, line, e);
}

int vf_ftp_list_end(vf_ftp_list *l, vf_entry *e)
{
    char name[256];
    if (!l->held[0]) return 0;
    vf_strlcpy(name, l->held, sizeof name);
    l->held[0] = '\0';
    return vms_finish(l, name, "", e);
}
