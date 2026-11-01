/* vf_plat_stub.c - vf_platform.h for the Linux tests: a fixed clock, no terminal,
   and an in-memory VMS disk (STUB$DKA0:) with versions, text and binary files. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "vf_platform.h"
#include "vf_util.h"
#include "vf_path.h"
#include "vf_stub.h"

#define MAXF 128
#define ST_FNF 0x18292u          /* RMS$_FNF */
#define ST_DNF 0x1C04Au          /* RMS$_DNF */
#define ST_DIRNOTEMPTY 0x0F64u   /* SS$_DIRNOTEMPTY-like */
#define ST_DUP 0x00B0u

typedef struct {
    char dir[128];               /* "STUB$DKA0:[USERS.TEST]" */
    char name[96];               /* "NOTES.TXT" or "WORK.DIR" */
    int ver, is_dir, text;
    char *data; int len;
    int used;
} lfile;

static lfile disk[MAXF];
static int rows_ = 24, cols_ = 80;
static char logbuf[32768];
static double clock_;
static int cancel_after = -1;
static const char *home = "STUB$DKA0:[USERS.TEST]";

static lfile *slot(void)
{
    int i;
    for (i = 0; i < MAXF; i++) if (!disk[i].used) { memset(&disk[i], 0, sizeof disk[i]); disk[i].used = 1; return &disk[i]; }
    return 0;
}

static int highest(const char *dir, const char *name)
{
    int i, h = 0;
    for (i = 0; i < MAXF; i++)
        if (disk[i].used && !vf_strcasecmp(disk[i].dir, dir) && !vf_strcasecmp(disk[i].name, name) && disk[i].ver > h)
            h = disk[i].ver;
    return h;
}

static lfile *put(const char *dir, const char *name, int is_dir, int text, const char *data, int len)
{
    lfile *f = slot();
    if (!f) return 0;
    vf_strlcpy(f->dir, dir, sizeof f->dir);
    vf_strlcpy(f->name, name, sizeof f->name);
    vf_upper(f->name);
    f->ver = highest(dir, f->name) + 1;
    f->is_dir = is_dir; f->text = text;
    if (len) { f->data = (char *) malloc((size_t) len); memcpy(f->data, data, (size_t) len); }
    f->len = len;
    return f;
}

void stub_add_local(const char *dir, const char *name, int text, const char *data, int len)
{
    put(dir, name, 0, text, data, len);
}

void stub_reset(void)
{
    static char bin[1536];
    int i;
    for (i = 0; i < MAXF; i++) { free(disk[i].data); disk[i].used = 0; disk[i].data = 0; }
    for (i = 0; i < (int) sizeof bin; i++) bin[i] = (char) (i * 7);
    put("STUB$DKA0:[USERS]", "TEST.DIR", 1, 0, 0, 0);
    put(home, "WORK.DIR", 1, 0, 0, 0);
    put(home, "SSH.DIR", 1, 0, 0, 0);
    put(home, "LOGIN.COM", 0, 1, "$ set noon\n$ show time\n", 23);
    put(home, "NOTES.TXT", 0, 1, "first version\n", 14);
    put(home, "NOTES.TXT", 0, 1, "Shopping list\nmilk\neggs\n", 24);
    put(home, "KIT.BCK", 0, 0, bin, (int) sizeof bin);
    put("STUB$DKA0:[USERS.TEST.SSH]", "ID_ED25519", 0, 1, "-----BEGIN OPENSSH PRIVATE KEY-----\n(test)\n", 43);
    put("STUB$DKA0:[USERS.TEST.WORK]", "PLAN.TXT", 0, 1, "one\ntwo\n", 8);
    logbuf[0] = '\0';
    clock_ = 1000.0;
    cancel_after = -1;
    rows_ = 24; cols_ = 80;
    stub_remote_reset();
}

void stub_set_size(int rows, int cols) { rows_ = rows; cols_ = cols; }
const char *stub_log(void) { return logbuf; }
void stub_cancel_after(int reads) { cancel_after = reads; }

/* "DIR]NAME.TYPE;V" -> dir, name, version (0 = none given) */
static int parse(const char *spec, char *dir, size_t dsz, char *name, size_t nsz, int *ver)
{
    const char *rb = strrchr(spec, ']'), *semi;
    if (!rb) return 0;
    if ((size_t) (rb - spec + 1) >= dsz) return 0;
    memcpy(dir, spec, (size_t) (rb - spec + 1)); dir[rb - spec + 1] = '\0';
    vf_strlcpy(name, rb + 1, nsz);
    semi = strchr(name, ';');
    *ver = 0;
    if (semi) { *ver = atoi(semi + 1); name[semi - name] = '\0'; }
    vf_upper(name);
    if (!strchr(name, '.')) vf_strlcat(name, ".", nsz);
    return 1;
}

static lfile *find(const char *spec)
{
    char dir[128], name[96];
    int ver, i;
    if (!parse(spec, dir, sizeof dir, name, sizeof name, &ver)) return 0;
    if (!ver) ver = highest(dir, name);
    for (i = 0; i < MAXF; i++)
        if (disk[i].used && !vf_strcasecmp(disk[i].dir, dir) && !vf_strcasecmp(disk[i].name, name) && disk[i].ver == ver)
            return &disk[i];
    return 0;
}

const char *stub_local(const char *spec, int *len, int *text, int *versions)
{
    lfile *f = find(spec);
    char dir[128], name[96];
    int ver;
    if (versions) {
        int i;
        *versions = 0;
        if (parse(spec, dir, sizeof dir, name, sizeof name, &ver))
            for (i = 0; i < MAXF; i++)
                if (disk[i].used && !vf_strcasecmp(disk[i].dir, dir) && !vf_strcasecmp(disk[i].name, name)) (*versions)++;
    }
    if (!f) return 0;
    if (len) *len = f->len;
    if (text) *text = f->text;
    return f->data ? f->data : "";
}

static int dir_exists(const char *dir)
{
    char parent[128], name[96], sub[128];
    int i;
    if (!vf_strcasecmp(dir, "STUB$DKA0:[000000]") || !vf_strcasecmp(dir, "STUB$DKA0:[USERS]")) return 1;
    if (!vf_lparent(dir, parent, sizeof parent)) return 0;
    for (i = 0; i < MAXF; i++) {
        if (!disk[i].used || !disk[i].is_dir || vf_strcasecmp(disk[i].dir, parent)) continue;
        vf_ldirname(disk[i].name, name, sizeof name);
        vf_lsubdir(parent, name, sub, sizeof sub);
        if (!vf_strcasecmp(sub, dir)) return 1;
    }
    return 0;
}

/* ---------- vf_platform.h ---------- */

unsigned int vfp_start(vfp_session *s) { vf_strlcpy(s->user, "TEST", sizeof s->user); vf_strlcpy(s->node, "STUBVMS", sizeof s->node); return 1; }
void vfp_stop(void) {}
void vfp_screen_size(int *rows, int *cols) { *rows = rows_; *cols = cols_; }
void vfp_draw(const vf_screen *s) { (void) s; }
void vfp_repaint(void) {}
int vfp_key(int timeout)
{
    (void) timeout;
    if (cancel_after > 0 && --cancel_after == 0) return VK_F10;
    return VK_TIMEOUT;
}
int vfp_broadcast(char *buf, int size) { (void) buf; (void) size; return 0; }
void vfp_flush_keys(void) {}
void vfp_now(vf_when *w) { w->y = 2026; w->m = 10; w->d = 25; w->hh = 10; w->mm = 12; }
double vfp_clock(void) { clock_ += 0.5; return clock_; }

void vfp_message(unsigned int st, char *buf, int size)
{
    const char *t;
    switch (st) {
    case ST_FNF: t = "%RMS-E-FNF, file not found"; break;
    case ST_DNF: t = "%RMS-E-DNF, directory not found"; break;
    case ST_DIRNOTEMPTY: t = "%SYSTEM-W-DIRNOTEMPTY, directory file is not empty"; break;
    case ST_DUP: t = "%SYSTEM-F-DUPFILENAME, duplicate file name"; break;
    default: t = (st & 1) ? "%SYSTEM-S-NORMAL, normal successful completion" : "%SYSTEM-F-ABORT, abort";
    }
    vf_strlcpy(buf, t, (size_t) size);
}

unsigned int vfp_locate(const char *spec, char *dir, int size, int *ods5)
{
    char t[256];
    *ods5 = 1;
    if (!spec || !*spec) { vf_strlcpy(dir, home, (size_t) size); return 1; }
    vf_strlcpy(t, spec, sizeof t);
    vf_upper(t);
    if (!strchr(t, ':')) {               /* [.SUB] or [DIR] relative to the disk */
        char b[256];
        if (!strncmp(t, "[.", 2)) { snprintf(b, sizeof b, "%.*s.%s", (int) strlen(home) - 1, home, t + 2); }
        else snprintf(b, sizeof b, "STUB$DKA0:%s", t);
        vf_strlcpy(t, b, sizeof t);
    }
    if (!dir_exists(t)) return ST_DNF;
    vf_strlcpy(dir, t, (size_t) size);
    return 1;
}

unsigned int vfp_list(const char *dir, int all_versions, vfp_entry_cb cb, void *ctx)
{
    int i;
    if (!dir_exists(dir)) return ST_DNF;
    for (i = 0; i < MAXF; i++) {
        lfile *f = &disk[i];
        vf_entry e;
        if (!f->used || vf_strcasecmp(f->dir, dir)) continue;
        if (!all_versions && f->ver != highest(f->dir, f->name)) continue;
        memset(&e, 0, sizeof e);
        snprintf(e.name, sizeof e.name, "%s;%d", f->name, f->ver);
        e.is_dir = f->is_dir;
        e.blocks = f->is_dir ? 1 : (unsigned int) ((f->len + 511) / 512);
        e.size = (unsigned long long) f->len;
        e.has_size = e.has_time = 1;
        e.mtime.y = 2026; e.mtime.m = 10; e.mtime.d = 20 + f->ver; e.mtime.hh = 9; e.mtime.mm = 30;
        vf_strlcpy(e.prot, f->is_dir ? "(RWE,RWE,RE,E)" : "(RWED,RWED,RE,)", sizeof e.prot);
        vf_strlcpy(e.owner, "[TEST]", sizeof e.owner);
        e.is_text = f->text;
        if (cb(ctx, &e)) return VFP_STOPPED;
    }
    return 1;
}

unsigned int vfp_mkdir(const char *dir)
{
    char parent[128], name[96], dname[100];
    const char *lb;
    if (dir_exists(dir)) return 1;        /* as LIB$CREATE_DIR: SS$_NORMAL, nothing made */
    if (!vf_lparent(dir, parent, sizeof parent) || !dir_exists(parent)) return ST_DNF;
    /* the last element: after the parent's "DEV:[A.B" and its "." ("^." is in a name) */
    lb = strstr(parent, "[000000]") ? strchr(dir, '[') : dir + strlen(parent) - 1;
    vf_strlcpy(name, lb + 1, sizeof name);
    name[strlen(name) - 1] = '\0';
    snprintf(dname, sizeof dname, "%s.DIR", name);
    put(parent, dname, 1, 0, 0, 0);
    return 1;
}

unsigned int vfp_delete(const char *spec)
{
    lfile *f;
    if (!strchr(spec, ';')) return 0x1004u;
    if (!(f = find(spec))) return ST_FNF;
    if (f->is_dir) {
        char name[96], sub[128];
        int i;
        vf_ldirname(f->name, name, sizeof name);
        vf_lsubdir(f->dir, name, sub, sizeof sub);
        for (i = 0; i < MAXF; i++) if (disk[i].used && !vf_strcasecmp(disk[i].dir, sub)) return ST_DIRNOTEMPTY;
    }
    free(f->data);
    f->data = 0;
    f->used = 0;
    return 1;
}

unsigned int vfp_rename(const char *from, const char *to)
{
    lfile *f = find(from);
    char dir[128], name[96];
    int ver;
    if (!f) return ST_FNF;
    if (!parse(to, dir, sizeof dir, name, sizeof name, &ver)) return ST_DNF;
    if (!dir_exists(dir)) return ST_DNF;
    vf_strlcpy(f->dir, dir, sizeof f->dir);
    f->ver = 0;
    f->ver = ver ? ver : highest(dir, name) + 1;
    vf_strlcpy(f->name, name, sizeof f->name);
    return 1;
}

struct vfp_file {
    lfile *f;
    int pos, text, writing;
};

unsigned int vfp_open_read(const char *spec, int *is_text, unsigned long long *bytes, vfp_file **out)
{
    lfile *f = find(spec);
    vfp_file *h;
    if (!f || f->is_dir) return ST_FNF;
    h = (vfp_file *) calloc(1, sizeof *h);
    h->f = f;
    if (*is_text < 0) *is_text = f->text;
    h->text = *is_text;
    *bytes = f->text ? (unsigned long long) ((f->len + 511) / 512 * 512) : (unsigned long long) f->len;
    *out = h;
    return 1;
}

unsigned int vfp_read(vfp_file *h, char *buf, int size, int *got)
{
    lfile *f = h->f;
    if (h->pos >= f->len) return VFP_EOF;
    if (h->text) {
        const char *s = f->data + h->pos, *lf = (const char *) memchr(s, '\n', (size_t) (f->len - h->pos));
        int n = lf ? (int) (lf - s) : f->len - h->pos;
        *got = n < size ? n : size;
        memcpy(buf, s, (size_t) *got);
        h->pos += n + (lf ? 1 : 0);
    } else {
        int n = f->len - h->pos;
        *got = n < size ? n : size;
        memcpy(buf, f->data + h->pos, (size_t) *got);
        h->pos += *got;
    }
    return 1;
}

unsigned int vfp_open_write(const char *spec, int text, unsigned long long expected, vfp_file **out,
                            char *result, int rsize)
{
    char dir[128], name[96];
    int ver;
    vfp_file *h;
    lfile *f;
    (void) expected;
    if (!parse(spec, dir, sizeof dir, name, sizeof name, &ver) || !dir_exists(dir)) return ST_DNF;
    f = put(dir, name, 0, text, 0, 0);
    if (!f) return 0x2C;
    h = (vfp_file *) calloc(1, sizeof *h);
    h->f = f; h->text = text; h->writing = 1;
    snprintf(result, (size_t) rsize, "%s%s;%d", f->dir, f->name, f->ver);
    *out = h;
    return 1;
}

unsigned int vfp_write(vfp_file *h, const char *buf, int len)
{
    lfile *f = h->f;
    f->data = (char *) realloc(f->data, (size_t) (f->len + len + 1));
    memcpy(f->data + f->len, buf, (size_t) len);
    f->len += len;
    if (h->text) f->data[f->len++] = '\n';
    return 1;
}

unsigned int vfp_close(vfp_file *h, int keep)
{
    if (h->writing && !keep) { free(h->f->data); h->f->data = 0; h->f->used = 0; }
    free(h);
    return 1;
}

void vfp_ssh_file(const char *name, char *out, int size)
{
    char t[96];
    vf_strlcpy(t, name, sizeof t);
    vf_upper(t);
    snprintf(out, (size_t) size, "STUB$DKA0:[USERS.TEST.SSH]%s.", t);
}

int vfp_exists(const char *spec) { return find(spec) != 0; }

void vfp_login_file(const char *name, char *out, int size) { snprintf(out, (size_t) size, "%s%s", home, name); }

unsigned int vfp_append_line(const char *spec, const char *line)
{
    lfile *f = find(spec);
    char dir[128], name[96];
    int ver, n = (int) strlen(line);
    if (!f) {
        if (!parse(spec, dir, sizeof dir, name, sizeof name, &ver)) return ST_DNF;
        f = put(dir, name, 0, 1, 0, 0);
        if (!f) return 0x2C;
    }
    f->data = (char *) realloc(f->data, (size_t) (f->len + n + 1));
    memcpy(f->data + f->len, line, (size_t) n);
    f->len += n;
    f->data[f->len++] = '\n';
    return 1;
}

void vfp_log(const char *line)
{
    vf_strlcat(logbuf, line, sizeof logbuf);
    vf_strlcat(logbuf, "\n", sizeof logbuf);
}

void vfp_getenv(const char *name, char *out, int size) { (void) name; if (size) out[0] = '\0'; }
