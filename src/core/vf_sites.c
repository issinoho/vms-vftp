/* vf_sites.c - the sites file, parsed and written (vf_sites.h). No I/O. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "vf_sites.h"
#include "vf_util.h"

void vf_sites_init(vf_sites *t) { memset(t, 0, sizeof *t); t->cur = -1; }

void vf_sites_free(vf_sites *t) { free(t->s); vf_sites_init(t); }

static void bad(vf_sites *t, const char *why)
{
    if (!t->bad++) snprintf(t->why, sizeof t->why, "line %d: %s", t->line, why);
}

int vf_site_name_ok(const char *name)
{
    size_t n = strlen(name), i;
    int blank = 1;
    if (!n || n >= VF_SITE_NAME) return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char) name[i];
        if (c == '[' || c == ']' || c < 32 || c == 127) return 0;
        if (c != ' ') blank = 0;
    }
    return !blank;
}

int vf_sites_find(const vf_sites *t, const char *name)
{
    int i;
    for (i = 0; i < t->n; i++) if (!vf_strcasecmp(t->s[i].name, name)) return i;
    return -1;
}

static int grow(vf_sites *t)
{
    vf_site *n;
    int cap;
    if (t->n < t->cap) return 1;
    cap = t->cap ? t->cap * 2 : 16;
    n = (vf_site *) realloc(t->s, (size_t) cap * sizeof *n);
    if (!n) return 0;
    t->s = n; t->cap = cap;
    return 1;
}

int vf_sites_put(vf_sites *t, const vf_site *s)
{
    int i;
    if (!vf_site_name_ok(s->name)) return 0;
    if ((i = vf_sites_find(t, s->name)) >= 0) { t->s[i] = *s; return 1; }
    if (!grow(t)) return 0;
    t->s[t->n++] = *s;
    return 1;
}

void vf_sites_remove(vf_sites *t, int i)
{
    if (i < 0 || i >= t->n) return;
    memmove(&t->s[i], &t->s[i + 1], (size_t) (t->n - i - 1) * sizeof *t->s);
    t->n--;
}

/* key = value: the field it fills, and its size; 0 if no such key */
static char *field(vf_site *s, const char *key, size_t *size)
{
    static const char *keys[] = { "protocol", "host", "port", "user", "key", "remote", "local" };
    char *f[7];
    size_t z[7];
    int i;
    f[0] = s->proto; z[0] = sizeof s->proto;
    f[1] = s->host; z[1] = sizeof s->host;
    f[2] = s->port; z[2] = sizeof s->port;
    f[3] = s->user; z[3] = sizeof s->user;
    f[4] = s->key; z[4] = sizeof s->key;
    f[5] = s->remote; z[5] = sizeof s->remote;
    f[6] = s->local; z[6] = sizeof s->local;
    for (i = 0; i < 7; i++) if (!vf_strcasecmp(key, keys[i])) { *size = z[i]; return f[i]; }
    return 0;
}

void vf_sites_line(vf_sites *t, const char *line)
{
    char b[600], *p, *eq, *val, *f;
    size_t size;
    t->line++;
    vf_strlcpy(b, line, sizeof b);
    p = vf_trim(b);
    if (!*p || *p == '!' || *p == '#') return;
    if (*p == '[') {
        char *rb = strchr(p, ']');
        vf_site s;
        t->cur = -1;
        if (!rb || rb[1]) { bad(t, "a site's name is [name]"); return; }
        *rb = '\0';
        memset(&s, 0, sizeof s);
        vf_strlcpy(s.name, vf_trim(p + 1), sizeof s.name);
        if (!vf_site_name_ok(s.name)) { bad(t, "not a usable site name"); return; }
        if (vf_sites_find(t, s.name) >= 0) { bad(t, "a second site of that name"); return; }
        vf_strlcpy(s.proto, "SFTP", sizeof s.proto);
        if (!grow(t)) { bad(t, "no memory"); return; }
        t->s[t->n] = s;
        t->cur = t->n++;
        return;
    }
    if (!(eq = strchr(p, '='))) { bad(t, "not \"key = value\""); return; }
    *eq = '\0';
    val = vf_trim(eq + 1);
    vf_trim(p);
    if (!vf_strcasecmp(p, "password") || !vf_strcasecmp(p, "passphrase")) {
        bad(t, "passwords are never kept (D9): take the line out");
        return;
    }
    if (t->cur < 0) { bad(t, "a key before the first [name]"); return; }
    if (!(f = field(&t->s[t->cur], p, &size))) { bad(t, "not a key VFTP knows"); return; }
    if (strlen(val) >= size) { bad(t, "too long"); return; }
    if (!vf_strcasecmp(p, "protocol")) {
        if (vf_strcasecmp(val, "SFTP") && vf_strcasecmp(val, "FTP")) { bad(t, "the protocol is SFTP or FTP"); return; }
        vf_upper(val);
    }
    if (!vf_strcasecmp(p, "port")) {
        const char *d;
        for (d = val; *d; d++) if (!isdigit((unsigned char) *d)) { bad(t, "a port is a number"); return; }
    }
    vf_strlcpy(f, val, size);
}

static int by_name(const void *x, const void *y)
{
    return vf_strcasecmp(((const vf_site *) x)->name, ((const vf_site *) y)->name);
}

void vf_sites_sort(vf_sites *t)
{
    if (t->n > 1) qsort(t->s, (size_t) t->n, sizeof *t->s, by_name);
}

static unsigned int out(vf_sites_cb cb, void *ctx, const char *key, const char *val)
{
    char b[600];
    if (!val[0]) return 1;
    snprintf(b, sizeof b, "%s = %s", key, val);
    return cb(ctx, b, (int) strlen(b));
}

unsigned int vf_sites_write(const vf_sites *t, vf_sites_cb cb, void *ctx)
{
    static const char *head[] = {
        "! VFTP's saved sites (D9). Never a password: VFTP asks for it each time.",
        "! Written by VFTP, a new version each time; comments are not kept. A line",
        "! VFTP cannot read stops it saving over the file.",
    };
    vf_site *s;
    unsigned int st = 1;
    int i;
    char b[VF_SITE_NAME + 4];
    for (i = 0; i < 3 && (st & 1); i++) st = cb(ctx, head[i], (int) strlen(head[i]));
    if (!(st & 1)) return st;
    if (!t->n) return 1;
    s = (vf_site *) malloc((size_t) t->n * sizeof *s);
    if (!s) return 0;
    memcpy(s, t->s, (size_t) t->n * sizeof *s);
    qsort(s, (size_t) t->n, sizeof *s, by_name);
    for (i = 0; i < t->n && (st & 1); i++) {
        snprintf(b, sizeof b, "[%s]", s[i].name);
        if ((st = cb(ctx, "", 0)) & 1) st = cb(ctx, b, (int) strlen(b));
        if (st & 1) st = out(cb, ctx, "protocol", s[i].proto);
        if (st & 1) st = out(cb, ctx, "host", s[i].host);
        if (st & 1) st = out(cb, ctx, "port", s[i].port);
        if (st & 1) st = out(cb, ctx, "user", s[i].user);
        if (st & 1) st = out(cb, ctx, "key", s[i].key);
        if (st & 1) st = out(cb, ctx, "remote", s[i].remote);
        if (st & 1) st = out(cb, ctx, "local", s[i].local);
    }
    free(s);
    return st;
}
