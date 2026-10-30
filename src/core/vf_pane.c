/* vf_pane.c - a pane's entries, order, filter and tags. No I/O. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vf_pane.h"
#include "vf_util.h"

void vf_pane_init(vf_pane *p, int local)
{
    memset(p, 0, sizeof *p);
    p->local = local;
    p->status = 1;
    vf_strlcpy(p->filter, "*", sizeof p->filter);
}

void vf_pane_clear(vf_pane *p)
{
    free(p->e); free(p->vis);
    p->e = 0; p->vis = 0;
    p->n = p->cap = p->nvis = 0;
}

void vf_pane_free(vf_pane *p) { vf_pane_clear(p); }

int vf_pane_add(vf_pane *p, const vf_entry *e)
{
    if (p->n == p->cap) {
        int cap = p->cap ? p->cap * 2 : 64;
        vf_entry *ne = (vf_entry *) realloc(p->e, (size_t) cap * sizeof *ne);
        if (!ne) return 0;
        p->e = ne; p->cap = cap;
    }
    p->e[p->n++] = *e;
    return 1;
}

static void base_name(const vf_pane *p, const char *name, char *out, size_t size);
static const vf_pane *sort_pane;          /* qsort has no context argument */

static const char *type_of(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot ? dot : "";
}

static int name_cmp(const vf_pane *p, const char *a, const char *b)
{
    return p->local ? vf_strcasecmp(a, b) : strcmp(a, b);
}

static int cmp_entry(const void *x, const void *y)
{
    const vf_pane *p = sort_pane;
    const vf_entry *a = &p->e[*(const int *) x], *b = &p->e[*(const int *) y];
    int r = 0;
    if (a->is_parent != b->is_parent) return a->is_parent ? -1 : 1;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    switch (p->sort) {
    case VF_SORT_TYPE: r = vf_strcasecmp(type_of(a->name), type_of(b->name)); break;
    case VF_SORT_SIZE:
        if (p->local) r = a->blocks < b->blocks ? 1 : a->blocks > b->blocks ? -1 : 0;
        else r = a->size < b->size ? 1 : a->size > b->size ? -1 : 0;
        break;
    case VF_SORT_DATE: r = -vf_when_cmp(&a->mtime, &b->mtime); break;
    default: break;
    }
    if (!r && p->local) {
        /* versions of one file: the highest first, as DIRECTORY shows them */
        char na[256], nb[256];
        const char *sa = strrchr(a->name, ';'), *sb = strrchr(b->name, ';');
        base_name(p, a->name, na, sizeof na);
        base_name(p, b->name, nb, sizeof nb);
        r = vf_strcasecmp(na, nb);
        if (!r && sa && sb) r = atoi(sb + 1) - atoi(sa + 1);
    }
    if (!r) r = name_cmp(p, a->name, b->name);
    return r;
}

/* The name without the ";version", for matching a filter. */
static void base_name(const vf_pane *p, const char *name, char *out, size_t size)
{
    char *semi;
    vf_strlcpy(out, name, size);
    if (p->local && (semi = strrchr(out, ';')) != 0) *semi = '\0';
}

static int shown(const vf_pane *p, const vf_entry *e)
{
    char b[256];
    if (e->is_parent || e->is_dir) return 1;
    if (!p->filter[0] || !strcmp(p->filter, "*")) return 1;
    base_name(p, e->name, b, sizeof b);
    return vf_match(p->filter, b);
}

void vf_pane_rebuild(vf_pane *p)
{
    char was[256];
    vf_entry *c = vf_pane_cur(p);
    int i;
    was[0] = '\0';
    if (c) vf_strlcpy(was, c->name, sizeof was);
    free(p->vis);
    p->vis = (int *) malloc((size_t) (p->n ? p->n : 1) * sizeof (int));
    p->nvis = 0;
    if (!p->vis) return;
    for (i = 0; i < p->n; i++) if (shown(p, &p->e[i])) p->vis[p->nvis++] = i;
    sort_pane = p;
    qsort(p->vis, (size_t) p->nvis, sizeof (int), cmp_entry);
    sort_pane = 0;
    if (was[0]) vf_pane_goto(p, was);
    if (p->cur >= p->nvis) p->cur = p->nvis ? p->nvis - 1 : 0;
    if (p->cur < 0) p->cur = 0;
}

vf_entry *vf_pane_cur(vf_pane *p)
{
    if (!p->vis || p->cur < 0 || p->cur >= p->nvis) return 0;
    return &p->e[p->vis[p->cur]];
}

vf_entry *vf_pane_find(vf_pane *p, const char *name)
{
    int i;
    for (i = 0; i < p->n; i++) if (!name_cmp(p, p->e[i].name, name)) return &p->e[i];
    return 0;
}

void vf_pane_goto(vf_pane *p, const char *name)
{
    int i;
    for (i = 0; i < p->nvis; i++)
        if (!name_cmp(p, p->e[p->vis[i]].name, name)) { p->cur = i; return; }
}

int vf_pane_tag_match(vf_pane *p, const char *pattern, int on)
{
    int i, changed = 0;
    char b[256];
    for (i = 0; i < p->nvis; i++) {
        vf_entry *e = &p->e[p->vis[i]];
        if (e->is_parent || e->is_dir) continue;   /* files only (Phase 1) */
        base_name(p, e->name, b, sizeof b);
        if (!vf_match(pattern, b) && !vf_match(pattern, e->name)) continue;
        if (e->tagged != on) { e->tagged = on; changed++; }
    }
    return changed;
}

void vf_pane_tagged(const vf_pane *p, int *count, unsigned long long *bytes, unsigned int *blocks)
{
    int i;
    *count = 0; *bytes = 0; *blocks = 0;
    for (i = 0; i < p->n; i++) {
        if (!p->e[i].tagged) continue;
        (*count)++;
        *bytes += p->e[i].size;
        *blocks += p->e[i].blocks;
    }
}

void vf_pane_totals(const vf_pane *p, int *files, int *dirs, unsigned long long *bytes,
                    unsigned int *blocks)
{
    int i;
    *files = *dirs = 0; *bytes = 0; *blocks = 0;
    for (i = 0; i < p->nvis; i++) {
        const vf_entry *e = &p->e[p->vis[i]];
        if (e->is_parent) continue;
        if (e->is_dir) { (*dirs)++; continue; }
        (*files)++;
        *bytes += e->size;
        *blocks += e->blocks;
    }
}

int vf_when_cmp(const vf_when *a, const vf_when *b)
{
    if (a->y != b->y) return a->y - b->y;
    if (a->m != b->m) return a->m - b->m;
    if (a->d != b->d) return a->d - b->d;
    if (a->hh != b->hh) return a->hh - b->hh;
    return a->mm - b->mm;
}

char *vf_fmt_bytes(unsigned long long n, char *buf, size_t size)
{
    static const char units[] = "KMGT";
    double v = (double) n;
    int u = -1;
    if (n < 100000ULL) { snprintf(buf, size, "%u", (unsigned int) n); return buf; }
    while (v >= 1000.0 && u < 3) { v /= 1024.0; u++; }
    if (v < 10.0) snprintf(buf, size, "%.1f%c", v, units[u]);
    else snprintf(buf, size, "%.0f%c", v, units[u]);
    return buf;
}

char *vf_fmt_perms(unsigned int mode, int is_dir, int is_link, char *buf, size_t size)
{
    static const char rwx[] = "rwxrwxrwx";
    char t[11];
    int i;
    t[0] = is_link ? 'l' : is_dir ? 'd' : '-';
    for (i = 0; i < 9; i++) t[i + 1] = (mode & (0400u >> i)) ? rwx[i] : '-';
    t[10] = '\0';
    vf_strlcpy(buf, t, size);
    return buf;
}

static const char *mon[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                             "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

static void fmt_date(const vf_entry *e, int wide, int year, char *buf, size_t size)
{
    const vf_when *w = &e->mtime;
    const char *m = w->m >= 1 && w->m <= 12 ? mon[w->m - 1] : "???";
    if (!e->has_time) { vf_strlcpy(buf, "", size); return; }
    if (wide) snprintf(buf, size, "%2d-%s-%04d %02d:%02d", w->d, m, w->y, w->hh, w->mm);
    else if (w->y == year) snprintf(buf, size, "%2d-%s", w->d, m);
    else snprintf(buf, size, "  %04d", w->y);
}

void vf_pane_row(const vf_pane *p, const vf_entry *e, int width, int year, char *buf, size_t size)
{
    char sz[16], date[32], extra[64], name[300];
    int wide = width >= 60, nw;
    if (e->is_parent) {
        snprintf(buf, size, "%-*s", width, p->local ? "[-]  (parent directory)" : "..   (parent directory)");
        buf[width < (int) size ? width : (int) size - 1] = '\0';
        return;
    }
    if (e->is_dir) vf_strlcpy(sz, p->local ? "" : "<DIR>", sizeof sz);
    else if (!e->has_size) vf_strlcpy(sz, "?", sizeof sz);
    else if (p->local) snprintf(sz, sizeof sz, "%u", e->blocks);
    else vf_fmt_bytes(e->size, sz, sizeof sz);
    if (p->local && e->is_dir) vf_strlcpy(sz, "<DIR>", sizeof sz);
    fmt_date(e, wide, year, date, sizeof date);
    extra[0] = '\0';
    if (wide) {
        /* protection or permissions, then the owner, as long as names keep 18 columns */
        char perms[16], t[64];
        int base = width - 1 - 6 - 1 - 17;
        if (p->local) snprintf(t, sizeof t, " %-16.16s", e->prot);
        else snprintf(t, sizeof t, " %s", e->perms ? vf_fmt_perms(e->perms, e->is_dir, e->is_link, perms, sizeof perms)
                                                   : "          ");
        if (base - (int) strlen(t) >= 18) {
            vf_strlcpy(extra, t, sizeof extra);
            snprintf(t, sizeof t, " %-10.10s", e->owner);
            if (base - (int) strlen(extra) - (int) strlen(t) >= 18) vf_strlcat(extra, t, sizeof extra);
        }
    }
    nw = width - 1 - 6 - 1 - (int) strlen(wide ? "25-OCT-2026 10:01" : "25-OCT") - (int) strlen(extra);
    if (nw < 8) nw = 8;
    vf_strlcpy(name, e->name, sizeof name);
    if (!p->local && e->is_dir) {                  /* "data/"; a VMS server's SUB.DIR;1 as it is */
        const char *dot = strrchr(name, '.');
        if (!dot || vf_strncasecmp(dot, ".DIR;", 5)) vf_strlcat(name, "/", sizeof name);
    }
    if ((int) strlen(name) > nw) { name[nw - 1] = '>'; name[nw] = '\0'; }   /* cut, marked */
    snprintf(buf, size, "%-*s %6s %-*s%s", nw, name, sz, wide ? 17 : 6, date, extra);
    if ((int) strlen(buf) > width) buf[width] = '\0';
}
