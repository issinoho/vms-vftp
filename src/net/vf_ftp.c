/* vf_ftp.c - vf_remote.h over FTP, our own (D4): a control connection from the
   C RTL's sockets, passive data connections (EPSV, else PASV to the control
   connection's host), text in TYPE A and bytes in TYPE I. The protocol's text is
   src/core/vf_ftpcore.c. What a VMS server does is NOTES N8: paths are VMS specs,
   names go in commands unescaped, DELE needs the version, RMD takes the bare
   directory name, nothing is ever replaced. No TLS yet: vf_remote.secure is 0 and
   the app asks before a password goes (D10). Blocking, with a time limit on each
   wait. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include "vf_rimpl.h"
#include "vf_ftpcore.h"
#include "vf_platform.h"
#include "vf_util.h"

#define WAIT_SECS 60

typedef struct {
    vf_remote base;          /* first: a vf_remote * is one of these */
    int fd;
    char host[256];
    char buf[8192];          /* control connection input not yet read as lines */
    int blen;
    vf_ftp_reply rep;        /* the last reply */
    char cwd[VF_SPEC_MAX];   /* the server's current directory as we last set it, "" unknown */
    char listed[VF_SPEC_MAX];/* the directory last listed (VMS: where relative specs start) */
    int type;                /* 'A', 'I', 0 not sent yet */
    int no_epsv;
} ftp_remote;

typedef struct {
    vfr_file base;
    ftp_remote *r;
    int dfd;
    int text, writing;
    int cr;                  /* a CR at the end of the last read, not yet given out */
} ftp_file;

static const vfr_ops ops;

static unsigned int failf(ftp_remote *r, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->base.err, sizeof r->base.err, fmt, ap);
    va_end(ap);
    return VFR_FAIL;
}

/* the last reply as the error: "MKD NEWDIR: directory exists" */
static unsigned int fail_rep(ftp_remote *r, const char *what)
{
    char m[256];
    vf_ftp_reply_msg(&r->rep, m, sizeof m);
    return failf(r, "%s%s%s", what, *what ? ": " : "", m[0] ? m : "no answer");
}

/* wait for fd to be readable: 1, or 0 after WAIT_SECS */
static int wait_in(int fd)
{
    fd_set rd;
    struct timeval tv;
    FD_ZERO(&rd);
    FD_SET(fd, &rd);
    tv.tv_sec = WAIT_SECS;
    tv.tv_usec = 0;
    return select(fd + 1, &rd, 0, 0, &tv) > 0;
}

/* one line from the control connection, CRLF taken off: 1, or 0 if it closed */
static int read_line(ftp_remote *r, char *line, int size)
{
    for (;;) {
        char *nl = (char *) memchr(r->buf, '\n', (size_t) r->blen);
        int n;
        if (nl) {
            int len = (int) (nl - r->buf);
            int keep = len < size - 1 ? len : size - 1;
            memcpy(line, r->buf, (size_t) keep);
            line[keep] = '\0';
            if (keep && line[keep - 1] == '\r') line[keep - 1] = '\0';
            memmove(r->buf, nl + 1, (size_t) (r->blen - len - 1));
            r->blen -= len + 1;
            return 1;
        }
        if (r->blen == (int) sizeof r->buf) r->blen = 0;       /* a line too long: dropped */
        if (!wait_in(r->fd)) return 0;
        n = (int) recv(r->fd, r->buf + r->blen, sizeof r->buf - (size_t) r->blen, 0);
        if (n <= 0) return 0;
        r->blen += n;
    }
}

/* the next whole reply: its code, or 0 if the connection closed or timed out */
static int reply(ftp_remote *r)
{
    char line[1024];
    vf_ftp_reply_init(&r->rep);
    while (read_line(r, line, sizeof line))
        if (vf_ftp_reply_line(&r->rep, line)) return r->rep.code;
    vf_strlcpy(r->rep.text, "the server did not answer", sizeof r->rep.text);
    r->rep.code = 0;
    return 0;
}

static int send_all(int fd, const char *b, int len)
{
    while (len > 0) {
        int n = (int) send(fd, b, (size_t) len, 0);
        if (n <= 0) return 0;
        b += n; len -= n;
    }
    return 1;
}

/* send a command, read its reply: the code (0 if the connection is gone) */
static int cmd(ftp_remote *r, const char *fmt, ...)
{
    char line[VF_SPEC_MAX + 16];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line - 2, fmt, ap);
    va_end(ap);
    if (n < 0 || n > (int) sizeof line - 3) n = (int) sizeof line - 3;
    line[n++] = '\r'; line[n++] = '\n';
    if (!send_all(r->fd, line, n)) {
        vf_strlcpy(r->rep.text, "the connection was lost", sizeof r->rep.text);
        return r->rep.code = 0;
    }
    return reply(r);
}

/* ---------- paths ---------- */

/* "DEV:[A]x.txt;1" -> "DEV:[A]" and "x.txt;1" (unescaped for the command, N8);
   "/a/b" -> "/a" and "b" */
static void split(ftp_remote *r, const char *path, char *dir, size_t dsz, char *name, size_t nsz)
{
    const char *cut;
    if (r->base.vms_paths) {
        cut = strrchr(path, ']');
        if (!cut) cut = strrchr(path, '>');
        if (!cut) cut = strrchr(path, ':');
        if (cut) cut++;
        else cut = path;
        snprintf(dir, dsz, "%.*s", (int) (cut - path), path);
        vf_ftp_unescape(cut, name, nsz);
        return;
    }
    cut = strrchr(path, '/');
    if (!cut) { dir[0] = '\0'; vf_strlcpy(name, path, nsz); return; }
    if (cut == path) vf_strlcpy(dir, "/", dsz);
    else snprintf(dir, dsz, "%.*s", (int) (cut - path), path);
    vf_strlcpy(name, cut + 1, nsz);
}

/* make dir the server's current directory (nothing to do if it is already) */
static unsigned int go(ftp_remote *r, const char *dir)
{
    if (!dir[0] || !strcmp(dir, r->cwd)) return 1;
    if (cmd(r, "CWD %s", dir) / 100 != 2) { r->cwd[0] = '\0'; return fail_rep(r, dir); }
    vf_strlcpy(r->cwd, dir, sizeof r->cwd);
    return 1;
}

static unsigned int set_type(ftp_remote *r, int t)
{
    if (r->type == t) return 1;
    if (cmd(r, "TYPE %c", t) / 100 != 2) return fail_rep(r, "TYPE");
    r->type = t;
    return 1;
}

/* a passive data connection: the socket, or -1 */
static int data_conn(ftp_remote *r)
{
    char host[64], err[200];
    int port = 0, fd;
    if (!r->no_epsv) {
        if (cmd(r, "EPSV") == 229 && vf_ftp_epsv(r->rep.text, &port)) goto dial;
        if (!r->rep.code) { fail_rep(r, "EPSV"); return -1; }
        r->no_epsv = 1;
    }
    if (cmd(r, "PASV") != 227 || !vf_ftp_pasv(r->rep.text, host, sizeof host, &port)) {
        fail_rep(r, "PASV");
        return -1;
    }
dial:
    /* the control connection's host, not PASV's address: it may be one we cannot reach */
    fd = vfr_dial(r->host, port, err, sizeof err);
    if (fd < 0) failf(r, "data connection: %s", err);
    return fd;
}

/* open a data connection and start a transfer command; the data socket or -1 */
static int start(ftp_remote *r, int type, const char *fmt, const char *arg)
{
    int fd, code;
    if (!(set_type(r, type) & 1)) return -1;
    if ((fd = data_conn(r)) < 0) return -1;
    code = cmd(r, fmt, arg);
    if (code != 150 && code != 125) {
        close(fd);
        fail_rep(r, arg);
        return -1;
    }
    return fd;
}

/* after the data connection is closed: the transfer's own end (226) */
static unsigned int finish(ftp_remote *r, const char *what)
{
    int code = reply(r);
    return code / 100 == 2 ? 1 : fail_rep(r, what);
}

/* ---------- connect and log in ---------- */

unsigned int vfr_ftp_connect(const char *host, int port, vf_remote **out, vfr_hostkey *hk,
                             char *err, int errsz)
{
    ftp_remote *r;
    int code;
    *out = 0;
    memset(hk, 0, sizeof *hk);
    hk->known = VFR_HK_MATCH;                    /* FTP has no host key to check */
    r = (ftp_remote *) calloc(1, sizeof *r);
    if (!r) { snprintf(err, (size_t) errsz, "no memory"); return VFR_FAIL; }
    r->base.ops = &ops;
    vf_strlcpy(r->host, host, sizeof r->host);
    r->fd = vfr_dial(host, port, err, errsz);
    if (r->fd < 0) { free(r); return VFR_FAIL; }
    while ((code = reply(r)) == 120) ;           /* "ready in a moment" */
    if (code != 220) {
        char m[256];
        vf_ftp_reply_msg(&r->rep, m, sizeof m);
        snprintf(err, (size_t) errsz, "%s", code ? m : "no FTP greeting");
        close(r->fd);
        free(r);
        return VFR_FAIL;
    }
    snprintf(r->base.banner, sizeof r->base.banner, "220 %.*s", (int) strcspn(r->rep.text, "\n"), r->rep.text);
    *out = &r->base;
    return 1;
}

static unsigned int ftp_trust(vf_remote *rr) { (void) rr; return 1; }

static unsigned int ftp_methods(vf_remote *rr, const char *user, char *out, int size)
{
    (void) rr; (void) user;
    vf_strlcpy(out, "password", (size_t) size);
    return 1;
}

static unsigned int ftp_login_password(vf_remote *rr, const char *user, const char *password)
{
    ftp_remote *r = (ftp_remote *) rr;
    char dir[VF_SPEC_MAX];
    int code = cmd(r, "USER %s", user);
    if (code == 331 || code == 332) code = cmd(r, "PASS %s", password);
    if (code != 230 && code != 202) {
        fail_rep(r, "");
        return code / 100 == 5 ? VFR_AUTH : VFR_FAIL;
    }
    /* a VMS server: SYST says so, and PWD gives a VMS spec (N8) */
    cmd(r, "SYST");
    if (r->rep.code == 215 || r->rep.code == 200) {
        if (strstr(r->rep.text, "VMS")) r->base.vms_paths = 1;
    }
    if (cmd(r, "PWD") == 257 && vf_ftp_pwd(r->rep.text, dir, sizeof dir)) {
        if (strchr(dir, '[') && strchr(dir, ']')) r->base.vms_paths = 1;
        else if (dir[0] == '/') r->base.vms_paths = 0;
        vf_strlcpy(r->cwd, dir, sizeof r->cwd);
    }
    return 1;
}

static unsigned int ftp_login_key(vf_remote *rr, const char *user, const char *keyfile, const char *passphrase)
{
    (void) user; (void) keyfile; (void) passphrase;
    failf((ftp_remote *) rr, "FTP has no key login");
    return VFR_AUTH;
}

/* "." the login directory; a VMS relative spec is from the directory last listed */
static unsigned int ftp_realpath(vf_remote *rr, const char *path, char *out, int size)
{
    ftp_remote *r = (ftp_remote *) rr;
    char dir[VF_SPEC_MAX];
    if (strcmp(path, ".")) {
        int absolute = r->base.vms_paths ? strchr(path, ':') != 0 : path[0] == '/';
        if (!absolute && r->listed[0] && !(go(r, r->listed) & 1)) return VFR_FAIL;
        if (cmd(r, "CWD %s", path) / 100 != 2) { r->cwd[0] = '\0'; return fail_rep(r, path); }
    }
    if (cmd(r, "PWD") != 257 || !vf_ftp_pwd(r->rep.text, dir, sizeof dir)) return fail_rep(r, "PWD");
    vf_strlcpy(r->cwd, dir, sizeof r->cwd);
    vf_strlcpy(out, dir, (size_t) size);
    return 1;
}

/* ---------- listing ---------- */

static unsigned int ftp_list(vf_remote *rr, const char *dir, vfr_entry_cb cb, void *ctx)
{
    ftp_remote *r = (ftp_remote *) rr;
    vf_ftp_list l;
    vf_entry e;
    vf_when now;
    char data[16384], line[1024];
    int fd, have = 0, stop = 0, n;
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    vf_strlcpy(r->listed, dir, sizeof r->listed);
    if ((fd = start(r, 'A', "LIST", "")) < 0) {
        /* a VMS server answers 550 to a listing of nothing; LIST gives "Total of 0" */
        return VFR_FAIL;
    }
    vfp_now(&now);
    vf_ftp_list_init(&l, now.y, now.m);
    for (;;) {
        char *nl;
        while ((nl = (char *) memchr(data, '\n', (size_t) have)) != 0) {
            int len = (int) (nl - data), keep = len < (int) sizeof line - 1 ? len : (int) sizeof line - 1;
            memcpy(line, data, (size_t) keep);
            line[keep] = '\0';
            if (keep && line[keep - 1] == '\r') line[keep - 1] = '\0';
            memmove(data, nl + 1, (size_t) (have - len - 1));
            have -= len + 1;
            if (!stop && vf_ftp_list_line(&l, line, &e)) stop = cb(ctx, &e);
            while (!stop && vf_ftp_list_more(&l, &e)) stop = cb(ctx, &e);
        }
        if (have == (int) sizeof data) have = 0;
        if (!wait_in(fd)) { close(fd); reply(r); return failf(r, "LIST: the server stopped sending"); }
        n = (int) recv(fd, data + have, sizeof data - (size_t) have, 0);
        if (n <= 0) break;
        have += n;
    }
    if (have && !stop) {                          /* a last line with no LF */
        data[have < (int) sizeof data ? have : (int) sizeof data - 1] = '\0';
        if (vf_ftp_list_line(&l, data, &e)) stop = cb(ctx, &e);
    }
    if (!stop && vf_ftp_list_end(&l, &e)) stop = cb(ctx, &e);
    close(fd);
    if (!(finish(r, "LIST") & 1)) return VFR_FAIL;
    return stop ? VFP_STOPPED : 1;
}

static unsigned int ftp_stat(vf_remote *rr, const char *path, vf_entry *e)
{
    ftp_remote *r = (ftp_remote *) rr;
    char dir[VF_SPEC_MAX], name[256];
    vf_entry found;
    int fd;
    split(r, path, dir, sizeof dir, name, sizeof name);
    memset(&found, 0, sizeof found);
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    if ((fd = start(r, 'A', "LIST %s", name)) < 0) return VFR_FAIL;
    {
        /* the first entry of "LIST name" */
        vf_ftp_list l;
        char data[4096];
        int have = 0, n;
        vf_when now;
        vfp_now(&now);
        vf_ftp_list_init(&l, now.y, now.m);
        while (wait_in(fd) && (n = (int) recv(fd, data + have, sizeof data - 1 - (size_t) have, 0)) > 0) {
            have += n;
            if (have >= (int) sizeof data - 1) break;
        }
        data[have] = '\0';
        {
            char *p = data, *nl;
            while (*p && !found.name[0]) {
                if ((nl = strchr(p, '\n')) != 0) *nl = '\0';
                if (nl && nl > p && nl[-1] == '\r') nl[-1] = '\0';
                if (vf_ftp_list_line(&l, p, &found)) break;
                if (!nl) break;
                p = nl + 1;
            }
            if (!found.name[0]) vf_ftp_list_end(&l, &found);
        }
    }
    close(fd);
    finish(r, "LIST");
    if (!found.name[0]) return failf(r, "%s: no such file", path);
    *e = found;
    return 1;
}

/* ---------- directories, delete, rename ---------- */

static unsigned int ftp_mkdir(vf_remote *rr, const char *path)
{
    ftp_remote *r = (ftp_remote *) rr;
    char dir[VF_SPEC_MAX], name[256];
    split(r, path, dir, sizeof dir, name, sizeof name);
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    return cmd(r, "MKD %s", name) == 257 ? 1 : fail_rep(r, name);
}

static unsigned int ftp_rmdir(vf_remote *rr, const char *path)
{
    ftp_remote *r = (ftp_remote *) rr;
    char dir[VF_SPEC_MAX], name[256];
    split(r, path, dir, sizeof dir, name, sizeof name);
    if (r->base.vms_paths) {
        /* "SUB.DIR;1" -> "SUB": RMD takes the bare name (N8) */
        char *semi = strrchr(name, ';');
        if (semi) *semi = '\0';
        if (strlen(name) > 4 && !vf_strcasecmp(name + strlen(name) - 4, ".DIR")) name[strlen(name) - 4] = '\0';
    }
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    return cmd(r, "RMD %s", name) / 100 == 2 ? 1 : fail_rep(r, name);
}

static unsigned int ftp_unlink(vf_remote *rr, const char *path)
{
    ftp_remote *r = (ftp_remote *) rr;
    char dir[VF_SPEC_MAX], name[256];
    split(r, path, dir, sizeof dir, name, sizeof name);
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    /* a name with no version: on a VMS server, the highest (DELE needs one, N8) */
    if (r->base.vms_paths && !strchr(name, ';')) vf_strlcat(name, ";0", sizeof name);
    return cmd(r, "DELE %s", name) / 100 == 2 ? 1 : fail_rep(r, name);
}

static unsigned int renam(ftp_remote *r, const char *from, const char *to)
{
    char dir[VF_SPEC_MAX], name[256], tdir[VF_SPEC_MAX], tname[256];
    split(r, from, dir, sizeof dir, name, sizeof name);
    split(r, to, tdir, sizeof tdir, tname, sizeof tname);
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    if (cmd(r, "RNFR %s", name) != 350) return fail_rep(r, name);
    /* the same directory: the name alone; elsewhere the whole path */
    if (!strcmp(dir, tdir)) { if (cmd(r, "RNTO %s", tname) / 100 != 2) return fail_rep(r, tname); }
    else if (cmd(r, "RNTO %s%s%s", tdir, r->base.vms_paths ? "" : "/", tname) / 100 != 2) return fail_rep(r, to);
    return 1;
}

/* is there a file of that name? 1 yes, 0 no, -1 cannot tell */
static int exists(ftp_remote *r, const char *path)
{
    char dir[VF_SPEC_MAX], name[256];
    int code;
    split(r, path, dir, sizeof dir, name, sizeof name);
    if (!(go(r, dir) & 1)) return -1;
    code = cmd(r, "SIZE %s", name);
    if (code == 213) return 1;
    if (code == 550) return 0;
    code = cmd(r, "MDTM %s", name);
    if (code == 213) return 1;
    if (code == 550) return 0;
    return -1;
}

static unsigned int ftp_rename(vf_remote *rr, const char *from, const char *to)
{
    ftp_remote *r = (ftp_remote *) rr;
    /* never replaces: a VMS server makes a new version (N8); elsewhere RNTO would
       replace, so look first */
    if (!r->base.vms_paths && exists(r, to) == 1) return failf(r, "%s: a file of that name is there", to);
    return renam(r, from, to);
}

static unsigned int ftp_replace(vf_remote *rr, const char *from, const char *to)
{
    ftp_remote *r = (ftp_remote *) rr;
    char dir[VF_SPEC_MAX], name[256];
    /* RNTO replaces on Unix servers; one that refuses: delete, then rename */
    if (renam(r, from, to) & 1) return 1;
    split(r, to, dir, sizeof dir, name, sizeof name);
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    if (cmd(r, "DELE %s", name) / 100 != 2) return fail_rep(r, name);
    return renam(r, from, to);
}

/* ---------- files ---------- */

static unsigned int open_file(vf_remote *rr, const char *path, int text, int writing, vfr_file **out)
{
    ftp_remote *r = (ftp_remote *) rr;
    char dir[VF_SPEC_MAX], name[256];
    ftp_file *f;
    int fd;
    split(r, path, dir, sizeof dir, name, sizeof name);
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    if ((fd = start(r, text ? 'A' : 'I', writing ? "STOR %s" : "RETR %s", name)) < 0) return VFR_FAIL;
    f = (ftp_file *) calloc(1, sizeof *f);
    if (!f) { close(fd); reply(r); return failf(r, "no memory"); }
    f->base.r = rr; f->r = r; f->dfd = fd; f->text = text; f->writing = writing;
    *out = &f->base;
    return 1;
}

static unsigned int ftp_open_read(vf_remote *rr, const char *path, int text, vfr_file **f)
{
    return open_file(rr, path, text, 0, f);
}

static unsigned int ftp_open_write(vf_remote *rr, const char *path, int text, vfr_file **f)
{
    return open_file(rr, path, text, 1, f);
}

/* TYPE A: CRLF lines in, LF lines out (a CR not before LF stays) */
static unsigned int ftp_read(vfr_file *ff, char *buf, int size, int *got)
{
    ftp_file *f = (ftp_file *) ff;
    char tmp[32768];
    int n, i, o = 0;
    *got = 0;
    if (size > (int) sizeof tmp) size = (int) sizeof tmp;
    if (!wait_in(f->dfd)) return failf(f->r, "the server stopped sending");
    n = (int) recv(f->dfd, f->text ? tmp : buf, (size_t) (f->text ? size - 1 : size), 0);
    if (n < 0) return failf(f->r, "the data connection failed");
    if (!f->text) { *got = n; return 1; }
    if (n == 0) {                                 /* the end: a CR left over */
        if (f->cr) { buf[0] = '\r'; *got = 1; f->cr = 0; }
        return 1;
    }
    if (f->cr) {
        if (tmp[0] != '\n') buf[o++] = '\r';
        f->cr = 0;
    }
    for (i = 0; i < n; i++) {
        if (tmp[i] == '\r') {
            if (i + 1 == n) { f->cr = 1; break; }
            if (tmp[i + 1] == '\n') continue;
        }
        buf[o++] = tmp[i];
    }
    *got = o;
    if (o == 0 && f->cr) return ftp_read(ff, buf, size, got);   /* a lone CR: read on */
    return 1;
}

/* TYPE A: LF lines in (from the app), CRLF on the wire */
static unsigned int ftp_write(vfr_file *ff, const char *buf, int len)
{
    ftp_file *f = (ftp_file *) ff;
    char out[4096];
    int i, o = 0;
    if (!f->text) return send_all(f->dfd, buf, len) ? 1 : failf(f->r, "the data connection failed");
    for (i = 0; i < len; i++) {
        if (o + 2 > (int) sizeof out) {
            if (!send_all(f->dfd, out, o)) return failf(f->r, "the data connection failed");
            o = 0;
        }
        if (buf[i] == '\n' && !(i > 0 && buf[i - 1] == '\r')) out[o++] = '\r';
        out[o++] = buf[i];
    }
    return o && !send_all(f->dfd, out, o) ? failf(f->r, "the data connection failed") : 1;
}

static unsigned int ftp_close(vfr_file *ff)
{
    ftp_file *f = (ftp_file *) ff;
    ftp_remote *r = f->r;
    int writing = f->writing;
    close(f->dfd);
    free(f);
    /* a Get stopped early: the server may say 426 or 451; that is the cancel, not news */
    return finish(r, writing ? "STOR" : "RETR");
}

static void ftp_keepalive(vf_remote *rr)
{
    cmd((ftp_remote *) rr, "NOOP");
}

static void ftp_disconnect(vf_remote *rr)
{
    ftp_remote *r = (ftp_remote *) rr;
    if (!r) return;
    if (r->fd >= 0) {
        cmd(r, "QUIT");
        close(r->fd);
    }
    free(r);
}

static const vfr_ops ops = {
    ftp_trust, ftp_methods, ftp_login_password, ftp_login_key, ftp_realpath,
    ftp_list, ftp_stat, ftp_mkdir, ftp_rmdir, ftp_unlink, ftp_rename, ftp_replace,
    ftp_open_read, ftp_read, ftp_open_write, ftp_write, ftp_close,
    ftp_keepalive, ftp_disconnect
};
