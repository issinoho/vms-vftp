/* vf_ftp.c - vf_remote.h over FTP, our own (D4): a control connection from the
   C RTL's sockets, passive data connections (EPSV, else PASV to the control
   connection's host), text in TYPE A and bytes in TYPE I. The protocol's text is
   src/core/vf_ftpcore.c. What a VMS server does is NOTES N8: paths are VMS specs,
   names go in commands unescaped, DELE needs the version, RMD takes the bare
   directory name, nothing is ever replaced. FTPS (explicit, D12): AUTH TLS when
   the server takes it, the certificate checked against VFTP's known_tls file
   (trusted on first use, as SSH host keys), then PROT P so the data connections
   are TLS too; a server that refuses AUTH gets plain FTP and the app asks before a
   password goes (D10). OpenSSL is VSI's SSL3 kit on VMS. Blocking, with a time
   limit on each wait. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include "vf_rimpl.h"
#include "vf_ftpcore.h"
#include "vf_platform.h"
#include "vf_util.h"

#define WAIT_SECS 60

/* a connection: a socket, and TLS on it once AUTH TLS (control) or PROT P (data) */
typedef struct { int fd; SSL *ssl; } conn;

typedef struct {
    vf_remote base;          /* first: a vf_remote * is one of these */
    conn c;                  /* the control connection */
    SSL_CTX *ctx;            /* 0 until AUTH TLS */
    SSL_SESSION *sess;       /* the control connection's, for the data connections */
    int prot;                /* PROT P: data connections in TLS */
    int port;
    char host[256];
    char buf[8192];          /* control connection input not yet read as lines */
    int blen;
    vf_ftp_reply rep;        /* the last reply */
    char cwd[VF_SPEC_MAX];   /* the server's current directory as we last set it, "" unknown */
    char listed[VF_SPEC_MAX];/* the directory last listed (VMS: where relative specs start) */
    int type;                /* 'A', 'I', 0 not sent yet */
    int no_epsv;
    unsigned long long rest; /* the next RETR or STOR starts here (REST, D16); 0 none */
} ftp_remote;

typedef struct {
    vfr_file base;
    ftp_remote *r;
    conn d;
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

/* ---------- connections, plain or TLS ---------- */

/* wait for input: 1, or 0 after WAIT_SECS (TLS may hold some already) */
static int c_wait(conn *c)
{
    fd_set rd;
    struct timeval tv;
    if (c->ssl && SSL_pending(c->ssl) > 0) return 1;
    FD_ZERO(&rd);
    FD_SET(c->fd, &rd);
    tv.tv_sec = WAIT_SECS;
    tv.tv_usec = 0;
    return select(c->fd + 1, &rd, 0, 0, &tv) > 0;
}

/* up to n bytes: how many, 0 at the end, -1 on an error */
static int c_recv(conn *c, char *b, int n)
{
    if (c->ssl) {
        int k = SSL_read(c->ssl, b, n), e;
        if (k > 0) return k;
        e = SSL_get_error(c->ssl, k);
        /* the end: close_notify, or a server that just closes (many do on data) */
        if (e == SSL_ERROR_ZERO_RETURN || (e == SSL_ERROR_SYSCALL && ERR_peek_error() == 0)) return 0;
        return -1;
    }
    return (int) recv(c->fd, b, (size_t) n, 0);
}

static int c_send_all(conn *c, const char *b, int len)
{
    while (len > 0) {
        int n = c->ssl ? SSL_write(c->ssl, b, len) : (int) send(c->fd, b, (size_t) len, 0);
        if (n <= 0) return 0;
        b += n; len -= n;
    }
    return 1;
}

/* close_notify first: a server knows from it that an upload is whole */
static void c_close(conn *c)
{
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); c->ssl = 0; }
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
}

/* TLS on a connected socket: 1, or 0 and err says why */
static int c_tls(ftp_remote *r, conn *c, int data, char *err, int errsz)
{
    unsigned long e;
    c->ssl = SSL_new(r->ctx);
    if (!c->ssl) { snprintf(err, (size_t) errsz, "TLS: no memory"); return 0; }
    SSL_set_fd(c->ssl, c->fd);
    if (data && r->sess) SSL_set_session(c->ssl, r->sess);     /* servers may require reuse */
    if (!data) SSL_set_tlsext_host_name(c->ssl, r->host);
    if (SSL_connect(c->ssl) == 1) return 1;
    e = ERR_get_error();
    snprintf(err, (size_t) errsz, "TLS: %s", e ? ERR_reason_error_string(e) : "the handshake failed");
    SSL_free(c->ssl);
    c->ssl = 0;
    return 0;
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
        if (!c_wait(&r->c)) return 0;
        n = c_recv(&r->c, r->buf + r->blen, (int) (sizeof r->buf - (size_t) r->blen));
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
    if (!c_send_all(&r->c, line, n)) {
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
/* a passive data connection, connected (no TLS yet): 1, or 0 */
static int data_conn(ftp_remote *r, conn *d)
{
    char host[64], err[200];
    int port = 0;
    d->fd = -1; d->ssl = 0;
    if (!r->no_epsv) {
        if (cmd(r, "EPSV") == 229 && vf_ftp_epsv(r->rep.text, &port)) goto dial;
        if (!r->rep.code) { fail_rep(r, "EPSV"); return 0; }
        r->no_epsv = 1;
    }
    if (cmd(r, "PASV") != 227 || !vf_ftp_pasv(r->rep.text, host, sizeof host, &port)) {
        fail_rep(r, "PASV");
        return 0;
    }
dial:
    /* the control connection's host, not PASV's address: it may be one we cannot reach */
    d->fd = vfr_dial(r->host, port, err, sizeof err);
    if (d->fd < 0) { failf(r, "data connection: %s", err); return 0; }
    return 1;
}

/* open a data connection and start a transfer command on it: 1, or 0 */
static int start(ftp_remote *r, conn *d, int type, const char *fmt, const char *arg)
{
    int code;
    char err[200];
    unsigned long long rest = r->rest;  /* for this command only */
    r->rest = 0;
    if (!(set_type(r, type) & 1)) return 0;
    if (r->prot && r->c.ssl) {
        /* the control connection's session, which some servers insist on reusing */
        if (r->sess) SSL_SESSION_free(r->sess);
        r->sess = SSL_get1_session(r->c.ssl);
    }
    if (!data_conn(r, d)) return 0;
    if (rest) {                        /* just before RETR or STOR, as RFC 3659 asks */
        char at[24];
        snprintf(at, sizeof at, "%llu", rest);
        if (cmd(r, "REST %s", at) != 350) {
            c_close(d);
            fail_rep(r, "REST");
            return 0;
        }
    }
    code = cmd(r, fmt, arg);
    if (code != 150 && code != 125) {
        c_close(d);
        fail_rep(r, arg);
        return 0;
    }
    if (r->prot && !c_tls(r, d, 1, err, sizeof err)) {
        c_close(d);
        reply(r);                                   /* the server's 425/426 for it */
        failf(r, "data connection: %s", err);
        return 0;
    }
    return 1;
}

/* after the data connection is closed: the transfer's own end (226) */
static unsigned int finish(ftp_remote *r, const char *what)
{
    int code = reply(r);
    return code / 100 == 2 ? 1 : fail_rep(r, what);
}

/* ---------- the server's certificate (D12) ---------- */

/* "AB:CD:...", the SHA-256 of the certificate, as openssl x509 -fingerprint shows it */
static void cert_fp(X509 *x, char *out, size_t size)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int n = 0, i;
    size_t o = 0;
    out[0] = '\0';
    if (!X509_digest(x, EVP_sha256(), md, &n)) return;
    for (i = 0; i < n && o + 4 < size; i++) o += (size_t) snprintf(out + o, size - o, "%s%02X", i ? ":" : "", md[i]);
}

/* the name a server goes by in known_tls: "host", or "[host]:port" off port 21 */
static void tls_name(ftp_remote *r, char *out, size_t size)
{
    if (r->port == 21) vf_strlcpy(out, r->host, size);
    else snprintf(out, size, "[%s]:%d", r->host, r->port);
}

/* known_tls: "name SHA256-fingerprint" lines */
static int tls_known(const char *file, const char *name, const char *fp)
{
    char line[512];
    int seen = 0;
    FILE *f = fopen(file, "r");
    if (!f) return VFR_HK_NOFILE;
    while (fgets(line, sizeof line, f)) {
        char *sp;
        line[strcspn(line, "\r\n")] = '\0';
        sp = strchr(line, ' ');
        if (!sp) continue;
        *sp = '\0';
        if (strcmp(line, name)) continue;
        if (!strcmp(sp + 1, fp)) { fclose(f); return VFR_HK_MATCH; }
        seen = 1;
    }
    fclose(f);
    return seen ? VFR_HK_MISMATCH : VFR_HK_NOTFOUND;
}

/* AUTH TLS: 1 with TLS on (or the server refused AUTH: plain), 0 if TLS failed */
static int auth_tls(ftp_remote *r, vfr_hostkey *hk, char *err, int errsz)
{
    X509 *x;
    char name[300];
    int code = cmd(r, "AUTH TLS");
    hk->known = VFR_HK_MATCH;                     /* plain FTP: nothing to check */
    if (code != 234) return 1;                    /* 500, 502, 504, 534: plain (both nodes, N8) */
    r->ctx = SSL_CTX_new(TLS_client_method());
    if (!r->ctx) { snprintf(err, (size_t) errsz, "TLS: no memory"); return 0; }
    SSL_CTX_set_min_proto_version(r->ctx, TLS1_2_VERSION);
    SSL_CTX_set_session_cache_mode(r->ctx, SSL_SESS_CACHE_CLIENT);
    /* no CA check: the certificate is pinned on first use, as SSH host keys (D12) */
    SSL_CTX_set_verify(r->ctx, SSL_VERIFY_NONE, 0);
    if (!c_tls(r, &r->c, 0, err, errsz)) return 0;
    r->base.secure = 1;
    x = SSL_get1_peer_certificate(r->c.ssl);
    if (!x) { snprintf(err, (size_t) errsz, "TLS: the server sent no certificate"); return 0; }
    vf_strlcpy(hk->type, "TLS certificate", sizeof hk->type);
    cert_fp(x, hk->fingerprint, sizeof hk->fingerprint);
    {
        char subj[128], iss[128], when[64];
        const ASN1_TIME *until = X509_get0_notAfter(x);
        BIO *b = BIO_new(BIO_s_mem());
        when[0] = '\0';
        X509_NAME_oneline(X509_get_subject_name(x), subj, sizeof subj);
        X509_NAME_oneline(X509_get_issuer_name(x), iss, sizeof iss);
        if (b && until && ASN1_TIME_print(b, until) > 0) {
            int n = BIO_read(b, when, (int) sizeof when - 1);
            when[n > 0 ? n : 0] = '\0';
        }
        if (b) BIO_free(b);
        snprintf(hk->about, sizeof hk->about, "%s, issued by %s, valid until %s",
                 subj, !strcmp(subj, iss) ? "itself" : iss, when);
    }
    X509_free(x);
    vfp_ssh_file("vftp_known_tls", hk->file, sizeof hk->file);
    tls_name(r, name, sizeof name);
    hk->known = tls_known(hk->file, name, hk->fingerprint);
    return 1;
}

/* ---------- connect and log in ---------- */

unsigned int vfr_ftp_connect(const char *host, int port, vf_remote **out, vfr_hostkey *hk,
                             char *err, int errsz)
{
    ftp_remote *r;
    int code;
    *out = 0;
    memset(hk, 0, sizeof *hk);
    r = (ftp_remote *) calloc(1, sizeof *r);
    if (!r) { snprintf(err, (size_t) errsz, "no memory"); return VFR_FAIL; }
    r->base.ops = &ops;
    r->port = port;
    vf_strlcpy(r->host, host, sizeof r->host);
    r->c.fd = vfr_dial(host, port, err, errsz);
    if (r->c.fd < 0) { free(r); return VFR_FAIL; }
    while ((code = reply(r)) == 120) ;           /* "ready in a moment" */
    if (code != 220) {
        char m[256];
        vf_ftp_reply_msg(&r->rep, m, sizeof m);
        snprintf(err, (size_t) errsz, "%s", code ? m : "no FTP greeting");
        c_close(&r->c);
        free(r);
        return VFR_FAIL;
    }
    snprintf(r->base.banner, sizeof r->base.banner, "220 %.*s", (int) strcspn(r->rep.text, "\n"), r->rep.text);
    if (!auth_tls(r, hk, err, errsz)) {
        /* a server that took AUTH TLS and then failed it: nothing more on this connection */
        c_close(&r->c);
        if (r->ctx) SSL_CTX_free(r->ctx);
        free(r);
        return VFR_FAIL;
    }
    *out = &r->base;
    return 1;
}

/* add the certificate to known_tls (the user said Y) */
static unsigned int ftp_trust(vf_remote *rr)
{
    ftp_remote *r = (ftp_remote *) rr;
    X509 *x;
    char file[256], name[300], fp[128], line[512];
    if (!r->c.ssl) return 1;
    x = SSL_get1_peer_certificate(r->c.ssl);
    if (!x) return failf(r, "no certificate");
    cert_fp(x, fp, sizeof fp);
    X509_free(x);
    tls_name(r, name, sizeof name);
    snprintf(line, sizeof line, "%s %s", name, fp);
    vfp_ssh_file("vftp_known_tls", file, sizeof file);
    if (!(vfp_append_line(file, line) & 1)) return failf(r, "%s could not be written", file);
    return 1;
}

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
    /* TLS on the control connection: the data connections too (RFC 4217) */
    if (r->c.ssl) {
        if (cmd(r, "PBSZ 0") / 100 != 2) return fail_rep(r, "PBSZ");
        if (cmd(r, "PROT P") / 100 != 2) return fail_rep(r, "PROT P");
        r->prot = 1;
    }
    /* REST STREAM in FEAT: a binary transfer can start part way (D16); the VMS
       servers have neither (N8) */
    if (cmd(r, "FEAT") == 211 && vf_ftp_feat(r->rep.text, "REST STREAM")) r->base.resume = 1;
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
    conn d;
    int have = 0, stop = 0, n;
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    vf_strlcpy(r->listed, dir, sizeof r->listed);
    if (!start(r, &d, 'A', "LIST", "")) {
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
        if (!c_wait(&d)) { c_close(&d); reply(r); return failf(r, "LIST: the server stopped sending"); }
        n = c_recv(&d, data + have, (int) (sizeof data - (size_t) have));
        if (n <= 0) break;
        have += n;
    }
    if (have && !stop) {                          /* a last line with no LF */
        data[have < (int) sizeof data ? have : (int) sizeof data - 1] = '\0';
        if (vf_ftp_list_line(&l, data, &e)) stop = cb(ctx, &e);
    }
    if (!stop && vf_ftp_list_end(&l, &e)) stop = cb(ctx, &e);
    c_close(&d);
    if (!(finish(r, "LIST") & 1)) return VFR_FAIL;
    return stop ? VFP_STOPPED : 1;
}

static unsigned int ftp_stat(vf_remote *rr, const char *path, vf_entry *e)
{
    ftp_remote *r = (ftp_remote *) rr;
    char dir[VF_SPEC_MAX], name[256];
    vf_entry found;
    conn d;
    split(r, path, dir, sizeof dir, name, sizeof name);
    memset(&found, 0, sizeof found);
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    if (!start(r, &d, 'A', "LIST %s", name)) return VFR_FAIL;
    {
        /* the first entry of "LIST name" */
        vf_ftp_list l;
        char data[4096];
        int have = 0, n;
        vf_when now;
        vfp_now(&now);
        vf_ftp_list_init(&l, now.y, now.m);
        while (c_wait(&d) && (n = c_recv(&d, data + have, (int) (sizeof data - 1 - (size_t) have))) > 0) {
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
    c_close(&d);
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

static unsigned int ftp_chmod(vf_remote *rr, const char *path, unsigned int mode)
{
    ftp_remote *r = (ftp_remote *) rr;
    char dir[VF_SPEC_MAX], name[256];
    split(r, path, dir, sizeof dir, name, sizeof name);
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    return cmd(r, "SITE CHMOD %o %s", mode & 07777, name) / 100 == 2 ? 1 : fail_rep(r, name);
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

static unsigned int open_file(vf_remote *rr, const char *path, int text, int writing,
                              unsigned long long offset, vfr_file **out)
{
    ftp_remote *r = (ftp_remote *) rr;
    char dir[VF_SPEC_MAX], name[256];
    ftp_file *f;
    conn d;
    split(r, path, dir, sizeof dir, name, sizeof name);
    if (!(go(r, dir) & 1)) return VFR_FAIL;
    r->rest = offset;                                   /* start() sends REST (D16) */
    if (!start(r, &d, text ? 'A' : 'I', writing ? "STOR %s" : "RETR %s", name)) return VFR_FAIL;
    f = (ftp_file *) calloc(1, sizeof *f);
    if (!f) { c_close(&d); reply(r); return failf(r, "no memory"); }
    f->base.r = rr; f->r = r; f->d = d; f->text = text; f->writing = writing;
    *out = &f->base;
    return 1;
}

static unsigned int ftp_open_read(vf_remote *rr, const char *path, int text, unsigned long long offset,
                                  vfr_file **f)
{
    return open_file(rr, path, text, 0, offset, f);
}

static unsigned int ftp_open_write(vf_remote *rr, const char *path, int text, unsigned long long size,
                                   unsigned long long offset, vfr_file **f)
{
    (void) size;
    return open_file(rr, path, text, 1, offset, f);
}

/* TYPE A: CRLF lines in, LF lines out (a CR not before LF stays) */
static unsigned int ftp_read(vfr_file *ff, char *buf, int size, int *got)
{
    ftp_file *f = (ftp_file *) ff;
    char tmp[32768];
    int n, i, o = 0;
    *got = 0;
    if (size > (int) sizeof tmp) size = (int) sizeof tmp;
    if (!c_wait(&f->d)) return failf(f->r, "the server stopped sending");
    n = c_recv(&f->d, f->text ? tmp : buf, f->text ? size - 1 : size);
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
    if (!f->text) return c_send_all(&f->d, buf, len) ? 1 : failf(f->r, "the data connection failed");
    for (i = 0; i < len; i++) {
        if (o + 2 > (int) sizeof out) {
            if (!c_send_all(&f->d, out, o)) return failf(f->r, "the data connection failed");
            o = 0;
        }
        if (buf[i] == '\n' && !(i > 0 && buf[i - 1] == '\r')) out[o++] = '\r';
        out[o++] = buf[i];
    }
    return o && !c_send_all(&f->d, out, o) ? failf(f->r, "the data connection failed") : 1;
}

static unsigned int ftp_close(vfr_file *ff)
{
    ftp_file *f = (ftp_file *) ff;
    ftp_remote *r = f->r;
    int writing = f->writing;
    c_close(&f->d);
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
    if (r->c.fd >= 0) {
        cmd(r, "QUIT");
        c_close(&r->c);
    }
    if (r->sess) SSL_SESSION_free(r->sess);
    if (r->ctx) SSL_CTX_free(r->ctx);
    free(r);
}

static const vfr_ops ops = {
    ftp_trust, ftp_methods, ftp_login_password, ftp_login_key, ftp_realpath,
    ftp_list, ftp_stat, ftp_mkdir, ftp_rmdir, ftp_unlink, ftp_rename, ftp_replace, ftp_chmod,
    ftp_open_read, ftp_read, ftp_open_write, ftp_write, ftp_close,
    ftp_keepalive, ftp_disconnect
};
