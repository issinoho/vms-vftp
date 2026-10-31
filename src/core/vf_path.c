/* vf_path.c - names and paths on both sides. No I/O. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "vf_path.h"
#include "vf_util.h"

/* ---------- remote ---------- */

void vf_rjoin(const char *dir, const char *name, char *out, size_t size)
{
    size_t n = strlen(dir);
    if (name[0] == '/') { vf_strlcpy(out, name, size); return; }
    snprintf(out, size, "%s%s%s", dir, n && dir[n - 1] == '/' ? "" : "/", name);
}

int vf_rparent(const char *dir, char *out, size_t size)
{
    char t[1024];
    char *slash;
    size_t n;
    vf_strlcpy(t, dir, sizeof t);
    n = strlen(t);
    while (n > 1 && t[n - 1] == '/') t[--n] = '\0';
    if (n <= 1) { vf_strlcpy(out, "/", size); return 0; }
    slash = strrchr(t, '/');
    if (!slash) { vf_strlcpy(out, ".", size); return 0; }
    if (slash == t) slash[1] = '\0'; else *slash = '\0';
    vf_strlcpy(out, t, size);
    return 1;
}

const char *vf_rbase(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* ---------- local ---------- */

/* Split "DEV:[A.B]" into "DEV:" and "A.B"; 0 if it is not in that form. */
static int split_dir(const char *dir, char *dev, size_t dsz, char *body, size_t bsz)
{
    const char *lb = strchr(dir, '['), *rb;
    if (!lb) lb = strchr(dir, '<');
    if (!lb) return 0;
    rb = strchr(lb, lb[0] == '[' ? ']' : '>');
    if (!rb) return 0;
    if ((size_t) (lb - dir) >= dsz || (size_t) (rb - lb - 1) >= bsz) return 0;
    memcpy(dev, dir, (size_t) (lb - dir)); dev[lb - dir] = '\0';
    memcpy(body, lb + 1, (size_t) (rb - lb - 1)); body[rb - lb - 1] = '\0';
    return 1;
}

void vf_lsubdir(const char *dir, const char *name, char *out, size_t size)
{
    char dev[256], body[512];
    if (!split_dir(dir, dev, sizeof dev, body, sizeof body)) { snprintf(out, size, "%s[.%s]", dir, name); return; }
    if (!strcmp(body, "000000") || !body[0]) snprintf(out, size, "%s[%s]", dev, name);
    else snprintf(out, size, "%s[%s.%s]", dev, body, name);
}

int vf_lparent(const char *dir, char *out, size_t size)
{
    char dev[256], body[512], *dot;
    if (!split_dir(dir, dev, sizeof dev, body, sizeof body)) { vf_strlcpy(out, dir, size); return 0; }
    if (!strcmp(body, "000000") || !body[0]) { snprintf(out, size, "%s[000000]", dev); return 0; }
    /* the last "." that is not escaped by "^" */
    for (dot = body + strlen(body); dot > body; dot--)
        if (dot[-1] == '.' && !(dot - 1 > body && dot[-2] == '^')) break;
    if (dot > body) { dot[-1] = '\0'; snprintf(out, size, "%s[%s]", dev, body); }
    else snprintf(out, size, "%s[000000]", dev);
    return 1;
}

void vf_lfile(const char *dir, const char *name, char *out, size_t size)
{
    snprintf(out, size, "%s%s", dir, name);
}

void vf_ldirname(const char *entry, char *out, size_t size)
{
    char *p;
    vf_strlcpy(out, entry, size);
    if ((p = strrchr(out, ';')) != 0) *p = '\0';
    p = out + strlen(out);
    if (p - out >= 4 && !vf_strcasecmp(p - 4, ".DIR")) p[-4] = '\0';
}

/* ---------- either kind of server ---------- */

void vf_xfile(int vms, const char *dir, const char *name, char *out, size_t size)
{
    if (vms) vf_lfile(dir, name, out, size);
    else vf_rjoin(dir, name, out, size);
}

void vf_xsubdir(int vms, const char *dir, const char *entry, char *out, size_t size)
{
    char d[256];
    if (!vms) { vf_rjoin(dir, entry, out, size); return; }
    vf_ldirname(entry, d, sizeof d);
    vf_lsubdir(dir, d, out, size);
}

int vf_xparent(int vms, const char *dir, char *out, size_t size)
{
    return vms ? vf_lparent(dir, out, size) : vf_rparent(dir, out, size);
}

void vf_xleft(int vms, const char *dir, char *out, size_t size)
{
    char dev[256], body[512], *dot;
    if (!vms) { vf_strlcpy(out, vf_rbase(dir), size); return; }
    out[0] = '\0';
    if (!split_dir(dir, dev, sizeof dev, body, sizeof body) || !strcmp(body, "000000") || !body[0]) return;
    for (dot = body + strlen(body); dot > body; dot--)
        if (dot[-1] == '.' && !(dot - 1 > body && dot[-2] == '^')) break;
    snprintf(out, size, "%s.DIR;1", dot);
}

/* ---------- names across ---------- */

static int ods2_char(int c) { return isalnum(c) || c == '_' || c == '$' || c == '-'; }

int vf_local_name(const char *remote, int ods5, char *out, size_t size)
{
    char src[256], name[600];
    const char *lastdot;
    size_t o = 0, i, len;
    int changed = 0;
    vf_strlcpy(src, remote, sizeof src);
    len = strlen(src);
    /* a VMS server's ";n" */
    for (i = len; i > 0 && isdigit((unsigned char) src[i - 1]); i--) ;
    if (i > 0 && i < len && src[i - 1] == ';') { src[i - 1] = '\0'; len = i - 1; }
    else if (len && src[len - 1] == ';') src[--len] = '\0';
    lastdot = strrchr(src, '.');                         /* ".profile": empty name, a type */
    if (ods5) {
        for (i = 0; i < len && o + 3 < sizeof name; i++) {
            unsigned char c = (unsigned char) src[i];
            if (c == ' ') { name[o++] = '^'; name[o++] = '_'; changed = 1; }
            else if (c == '.' && src + i != lastdot) { name[o++] = '^'; name[o++] = '.'; changed = 1; }
            else if (strchr("!#&'()+,;=@[]^`{}~%", c) && c) { name[o++] = '^'; name[o++] = (char) c; changed = 1; }
            else if (c < 32 || c == 127) { name[o++] = '_'; changed = 1; }
            else name[o++] = (char) c;
        }
    } else {
        int nlen = 0, tlen = 0, in_type = 0;
        for (i = 0; i < len && o + 1 < sizeof name; i++) {
            unsigned char c = (unsigned char) src[i];
            if (src + i == lastdot) { name[o++] = '.'; in_type = 1; continue; }
            if (in_type ? tlen >= 39 : nlen >= 39) { changed = 1; continue; }
            if (ods2_char(c)) name[o++] = (char) toupper(c);
            else { name[o++] = '_'; changed = 1; }
            if (in_type) tlen++; else nlen++;
        }
    }
    name[o] = '\0';
    if (!name[0]) { vf_strlcpy(name, "UNNAMED", sizeof name); changed = 1; }
    vf_strlcpy(out, name, size);
    return changed;
}

void vf_remote_name(const char *local, char *out, size_t size)
{
    char t[256];
    size_t i, o = 0, n;
    int lower = 0;
    char *semi;
    vf_strlcpy(t, local, sizeof t);
    if ((semi = strrchr(t, ';')) != 0) *semi = '\0';
    n = strlen(t);
    if (n && t[n - 1] == '.') t[--n] = '\0';            /* "LICENSE." */
    for (i = 0; i < n && o + 1 < size; i++) {
        char c = t[i];
        if (c == '^' && i + 1 < n) {
            c = t[++i];
            if (c == '_') c = ' ';
        }
        if (islower((unsigned char) c)) lower = 1;
        out[o++] = c;
    }
    out[o] = '\0';
    if (!lower) for (i = 0; out[i]; i++) out[i] = (char) tolower((unsigned char) out[i]);
}

const char vf_default_text_types[] =
    ".TXT .C .H .CC .CPP .HPP .COM .MMS .MAR .FOR .F .F90 .PAS .BAS .COB .PL .PY .SH "
    ".HTML .HTM .CSS .JS .JSON .XML .YAML .YML .MD .RST .CSV .TSV .INI .CONF .CFG .LOG "
    ".LIS .MAP .OPT .SQL .DIFF .PATCH .MAK .AWK .SED .TEX .LIST .README .PEM .PUB";

int vf_text_type(const char *name, const char *list)
{
    char t[256], type[64];
    const char *dot, *p;
    size_t n;
    vf_strlcpy(t, name, sizeof t);
    {
        char *semi = strrchr(t, ';');
        if (semi) *semi = '\0';
    }
    dot = strrchr(t, '.');
    if (!dot || !dot[1]) return 0;
    vf_strlcpy(type, dot, sizeof type);
    vf_upper(type);
    n = strlen(type);
    if (!list) list = vf_default_text_types;
    for (p = list; *p; ) {
        char w[64];
        size_t k = 0;
        while (*p == ' ' || *p == ',') p++;
        while (*p && *p != ' ' && *p != ',' && k + 1 < sizeof w) w[k++] = (char) toupper((unsigned char) *p++);
        w[k] = '\0';
        if (k && w[0] != '.') { memmove(w + 1, w, k + 1); w[0] = '.'; k++; }
        if (k == n && !strcmp(w, type)) return 1;
    }
    return 0;
}
