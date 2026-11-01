/* vf_remote_stub.c - vf_remote.h for the Linux tests: a server in memory at
   test.example (user "test", password "secret" or the stub's key file): SFTP on
   port 22, whose key known_hosts may or may not know (stub_trust,
   stub_change_key), and the same files by FTP on port 21, with no TLS. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vf_remote.h"
#include "vf_platform.h"
#include "vf_util.h"
#include "vf_path.h"
#include "vf_stub.h"

#define MAXR 64

typedef struct { char path[256]; int is_dir; char *data; int len; unsigned int perms; int used; } rnode;

static rnode srv[MAXR];
static int trusted, changed, ftps, ftps_trusted;

struct vf_remote { char err[256]; int logged_in; int ftp, tls; };
struct vfr_file { vf_remote *r; rnode *n; int pos, writing; char *buf; int len; char path[256]; };

static rnode *rfind(const char *path)
{
    int i;
    for (i = 0; i < MAXR; i++) if (srv[i].used && !strcmp(srv[i].path, path)) return &srv[i];
    return 0;
}

static rnode *radd(const char *path, int is_dir, const char *data, int len)
{
    int i;
    rnode *n = rfind(path);
    if (!n) for (i = 0; i < MAXR; i++) if (!srv[i].used) { n = &srv[i]; break; }
    if (!n) return 0;
    free(n->data);
    memset(n, 0, sizeof *n);
    n->used = 1;
    vf_strlcpy(n->path, path, sizeof n->path);
    n->is_dir = is_dir;
    n->perms = is_dir ? 0755 : 0644;
    if (len) { n->data = (char *) malloc((size_t) len); memcpy(n->data, data, (size_t) len); }
    n->len = len;
    return n;
}

void stub_add_remote(const char *path, const char *data, int len) { radd(path, 0, data, len); }
void stub_trust(int on) { trusted = on; }
void stub_ftps(int on) { ftps = on; ftps_trusted = 0; }
void stub_change_key(int on) { changed = on; }

const char *stub_remote(const char *path, int *len)
{
    rnode *n = rfind(path);
    if (!n) return 0;
    if (len) *len = n->len;
    return n->data ? n->data : "";
}

int stub_remote_like(const char *prefix)
{
    int i;
    for (i = 0; i < MAXR; i++) if (srv[i].used && !strncmp(srv[i].path, prefix, strlen(prefix))) return 1;
    return 0;
}

static void remote_reset(void)
{
    static char zip[2000];
    int i;
    for (i = 0; i < MAXR; i++) { free(srv[i].data); srv[i].data = 0; srv[i].used = 0; }
    for (i = 0; i < (int) sizeof zip; i++) zip[i] = (char) (i % 251);
    radd("/", 1, 0, 0);
    radd("/home", 1, 0, 0);
    radd("/home/test", 1, 0, 0);
    radd("/home/test/data", 1, 0, 0);
    radd("/home/test/notes.txt", 0, "remote notes\r\nline two\n", 23);
    radd("/home/test/README.md", 0, "# Read me\n", 10);
    radd("/home/test/kit.zip", 0, zip, (int) sizeof zip);
    radd("/home/test/data/big.log", 0, "x\n", 2);
    trusted = 0; changed = 0;
}

void stub_remote_reset(void) { remote_reset(); }   /* called by stub_reset */

unsigned int vfr_connect(int proto, const char *host, int port, vf_remote **r, vfr_hostkey *hk,
                         char *err, int errsz)
{
    *r = 0;
    if (strcmp(host, "test.example") || port != (proto == VFR_FTP ? 21 : 22)) {
        snprintf(err, (size_t) errsz, "%s port %d: connection refused", host, port);
        return VFR_FAIL;
    }
    *r = (vf_remote *) calloc(1, sizeof **r);
    memset(hk, 0, sizeof *hk);
    if (proto == VFR_FTP) {
        (*r)->ftp = 1;
        if (!ftps) { hk->known = VFR_HK_MATCH; return 1; }           /* plain: nothing to check */
        (*r)->tls = 1;                                               /* AUTH TLS taken (D12) */
        vf_strlcpy(hk->type, "TLS certificate", sizeof hk->type);
        vf_strlcpy(hk->fingerprint, "AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89:"
                   "AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89", sizeof hk->fingerprint);
        vf_strlcpy(hk->about, "/CN=test.example, issued by itself, valid until Nov  1 2027 GMT",
                   sizeof hk->about);
        vfp_ssh_file("vftp_known_tls", hk->file, sizeof hk->file);
        hk->known = ftps_trusted ? VFR_HK_MATCH : VFR_HK_NOTFOUND;
        return 1;
    }
    vf_strlcpy(hk->type, "ecdsa-sha2-nistp256", sizeof hk->type);
    vf_strlcpy(hk->fingerprint, "SHA256:TESTtestTESTtestTESTtestTESTtestTESTtes", sizeof hk->fingerprint);
    vfp_ssh_file("known_hosts", hk->file, sizeof hk->file);
    hk->known = changed ? VFR_HK_MISMATCH : trusted ? VFR_HK_MATCH : VFR_HK_NOTFOUND;
    return 1;
}

unsigned int vfr_trust(vf_remote *r)
{
    char kh[256];
    if (r->tls) { ftps_trusted = 1; return 1; }
    vfp_ssh_file("known_hosts", kh, sizeof kh);
    trusted = 1;
    return vfp_append_line(kh, "test.example ecdsa-sha2-nistp256 AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAI");
}

int vfr_secure(vf_remote *r) { return r && (!r->ftp || r->tls); }
int vfr_vms_paths(vf_remote *r) { (void) r; return 0; }

unsigned int vfr_methods(vf_remote *r, const char *user, char *out, int size)
{
    (void) user;
    vf_strlcpy(out, r->ftp ? "password" : "publickey,password,keyboard-interactive", (size_t) size);
    return 1;
}

unsigned int vfr_login_password(vf_remote *r, const char *user, const char *password)
{
    if (strcmp(user, "test") || strcmp(password, "secret")) {
        vf_strlcpy(r->err, "Authentication failed (username/password)", sizeof r->err);
        return VFR_AUTH;
    }
    r->logged_in = 1;
    return 1;
}

unsigned int vfr_login_key(vf_remote *r, const char *user, const char *keyfile, const char *passphrase)
{
    (void) passphrase;
    if (strcmp(user, "test") || !vfp_exists(keyfile)) {
        vf_strlcpy(r->err, "Username/PublicKey combination invalid", sizeof r->err);
        return VFR_AUTH;
    }
    r->logged_in = 1;
    return 1;
}

unsigned int vfr_realpath(vf_remote *r, const char *path, char *out, int size)
{
    char t[512], *p, parts[32][128];
    int n = 0, i;
    if (!strcmp(path, ".")) { vf_strlcpy(out, "/home/test", (size_t) size); return 1; }
    vf_strlcpy(t, path, sizeof t);
    for (p = strtok(t, "/"); p && n < 32; p = strtok(0, "/")) {
        if (!strcmp(p, ".")) continue;
        if (!strcmp(p, "..")) { if (n) n--; continue; }
        vf_strlcpy(parts[n++], p, sizeof parts[0]);
    }
    out[0] = '\0';
    for (i = 0; i < n; i++) { vf_strlcat(out, "/", (size_t) size); vf_strlcat(out, parts[i], (size_t) size); }
    if (!n) vf_strlcpy(out, "/", (size_t) size);
    if (!rfind(out) || !rfind(out)->is_dir) { vf_strlcpy(r->err, "No such file", sizeof r->err); return VFR_FAIL; }
    return 1;
}

static void fill(const rnode *n, vf_entry *e)
{
    memset(e, 0, sizeof *e);
    vf_strlcpy(e->name, vf_rbase(n->path), sizeof e->name);
    e->is_dir = n->is_dir;
    e->size = (unsigned long long) n->len;
    e->has_size = e->has_time = 1;
    e->perms = n->perms;
    e->mtime.y = 2026; e->mtime.m = 10; e->mtime.d = 24; e->mtime.hh = 18; e->mtime.mm = 2;
    vf_strlcpy(e->owner, "test", sizeof e->owner);
}

static int child_of(const char *dir, const char *path)
{
    size_t n = strlen(dir);
    const char *rest;
    if (!strcmp(dir, "/")) n = 0;
    if (strncmp(path, dir, n) || path[n] != '/' || !path[n + 1]) return 0;
    rest = path + n + 1;
    return !strchr(rest, '/');
}

unsigned int vfr_list(vf_remote *r, const char *dir, vfr_entry_cb cb, void *ctx)
{
    int i;
    rnode *d = rfind(dir);
    if (!d || !d->is_dir) { vf_strlcpy(r->err, "No such file", sizeof r->err); return VFR_FAIL; }
    for (i = 0; i < MAXR; i++) {
        vf_entry e;
        if (!srv[i].used || !child_of(dir, srv[i].path)) continue;
        fill(&srv[i], &e);
        if (cb(ctx, &e)) return VFP_STOPPED;
    }
    return 1;
}

unsigned int vfr_stat(vf_remote *r, const char *path, vf_entry *e)
{
    rnode *n = rfind(path);
    if (!n) { vf_strlcpy(r->err, "No such file", sizeof r->err); return VFR_FAIL; }
    fill(n, e);
    return 1;
}

unsigned int vfr_mkdir(vf_remote *r, const char *path)
{
    if (rfind(path)) { vf_strlcpy(r->err, "Failure (file exists)", sizeof r->err); return VFR_FAIL; }
    radd(path, 1, 0, 0);
    return 1;
}

unsigned int vfr_rmdir(vf_remote *r, const char *path)
{
    int i;
    rnode *n = rfind(path);
    if (!n || !n->is_dir) { vf_strlcpy(r->err, "No such file", sizeof r->err); return VFR_FAIL; }
    for (i = 0; i < MAXR; i++)
        if (srv[i].used && child_of(path, srv[i].path)) { vf_strlcpy(r->err, "Failure (directory not empty)", sizeof r->err); return VFR_FAIL; }
    n->used = 0;
    return 1;
}

unsigned int vfr_unlink(vf_remote *r, const char *path)
{
    rnode *n = rfind(path);
    if (!n || n->is_dir) { vf_strlcpy(r->err, "No such file", sizeof r->err); return VFR_FAIL; }
    free(n->data); n->data = 0; n->used = 0;
    return 1;
}

unsigned int vfr_rename(vf_remote *r, const char *from, const char *to)
{
    rnode *n = rfind(from);
    if (!n) { vf_strlcpy(r->err, "No such file", sizeof r->err); return VFR_FAIL; }
    if (rfind(to)) { vf_strlcpy(r->err, "Failure (file exists)", sizeof r->err); return VFR_FAIL; }
    vf_strlcpy(n->path, to, sizeof n->path);
    return 1;
}

unsigned int vfr_replace(vf_remote *r, const char *from, const char *to)
{
    rnode *n = rfind(from), *o = rfind(to);
    if (!n) { vf_strlcpy(r->err, "No such file", sizeof r->err); return VFR_FAIL; }
    if (o) { free(o->data); o->data = 0; o->used = 0; }
    vf_strlcpy(n->path, to, sizeof n->path);
    return 1;
}

unsigned int vfr_open_read(vf_remote *r, const char *path, int text, vfr_file **f)
{
    rnode *n = rfind(path);
    (void) text;
    if (!n || n->is_dir) { vf_strlcpy(r->err, "No such file", sizeof r->err); return VFR_FAIL; }
    *f = (vfr_file *) calloc(1, sizeof **f);
    (*f)->r = r; (*f)->n = n;
    return 1;
}

unsigned int vfr_read(vfr_file *f, char *buf, int size, int *got)
{
    int n = f->n->len - f->pos;
    if (size > 500) size = 500;            /* several chunks, so progress is drawn */
    *got = n < size ? n : size;
    if (*got) memcpy(buf, f->n->data + f->pos, (size_t) *got);
    f->pos += *got;
    return 1;
}

unsigned int vfr_open_write(vf_remote *r, const char *path, int text, vfr_file **f)
{
    char parent[256];
    rnode *d;
    (void) text;
    vf_rparent(path, parent, sizeof parent);
    d = rfind(parent);
    if (!d || !d->is_dir) { vf_strlcpy(r->err, "No such file", sizeof r->err); return VFR_FAIL; }
    *f = (vfr_file *) calloc(1, sizeof **f);
    (*f)->r = r; (*f)->writing = 1;
    vf_strlcpy((*f)->path, path, sizeof (*f)->path);
    radd(path, 0, 0, 0);                   /* created (empty) at open, as SFTP does */
    return 1;
}

unsigned int vfr_write(vfr_file *f, const char *buf, int len)
{
    f->buf = (char *) realloc(f->buf, (size_t) (f->len + len));
    memcpy(f->buf + f->len, buf, (size_t) len);
    f->len += len;
    return 1;
}

unsigned int vfr_close(vfr_file *f)
{
    if (f->writing) radd(f->path, 0, f->buf, f->len);
    free(f->buf);
    free(f);
    return 1;
}

void vfr_keepalive(vf_remote *r) { (void) r; }
void vfr_disconnect(vf_remote *r) { free(r); }
const char *vfr_error(vf_remote *r) { return r ? r->err : ""; }
const char *vfr_banner(vf_remote *r)
{
    return r && r->ftp ? "220 test.example FTP Server (Stub) Ready." : "SSH-2.0-StubSSH_1.0";
}
