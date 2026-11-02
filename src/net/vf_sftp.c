/* vf_sftp.c - vf_remote.h through libssh2 (D3): a TCP connection from the C RTL,
   the SSH handshake, the host key checked against VSI ssh's known_hosts (D7),
   password, keyboard-interactive or key login, then SFTP; or SCP (D15), the same
   session with files by scp and the rest by Unix shell commands. Blocking calls. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include "vf_rimpl.h"
#include "vf_platform.h"
#include "vf_util.h"
#include "vf_ftpcore.h"

typedef struct {
    vf_remote base;          /* first: a vf_remote * is one of these */
    int fd;
    LIBSSH2_SESSION *s;
    LIBSSH2_SFTP *sftp;
    char host[256];
    int port;
    char bye[200];           /* the server's reason, if it closed the connection */
    int scp;                 /* SCP (D15): no SFTP; commands and scp */
} sftp_remote;

typedef struct {
    vfr_file base;
    sftp_remote *r;
    LIBSSH2_SFTP_HANDLE *h;
} sftp_file;

static int inited;

/* The server's SSH_MSG_DISCONNECT: libssh2 drops its text unless asked for it.
   VMS sshd says why a login is refused only this way ("You are not authorized
   to login from this source", N17). */
static LIBSSH2_DISCONNECT_FUNC(on_bye)
{
    sftp_remote *r = (sftp_remote *) *abstract;
    int i, n = 0;
    (void) session; (void) reason; (void) language; (void) language_len;
    if (!r || !message) return;
    for (i = 0; i < message_len && n < (int) sizeof r->bye - 1; i++)
        if ((unsigned char) message[i] >= ' ') r->bye[n++] = message[i];
    while (n > 0 && r->bye[n - 1] == ' ') n--;
    r->bye[n] = '\0';
}

static unsigned int fail(sftp_remote *r, const char *what)
{
    char *msg = 0;
    int len = 0, rc = libssh2_session_last_error(r->s, &msg, &len, 0);
    if (rc == LIBSSH2_ERROR_SFTP_PROTOCOL && r->sftp) {
        static const char *names[] = { "OK", "end of file", "no such file", "permission denied",
            "failure", "bad message", "no connection", "connection lost", "operation unsupported",
            "invalid handle", "no such path", "file already exists", "write protected",
            "no media", "no space on the file system", "quota exceeded", "unknown principal",
            "lock conflict", "directory not empty", "not a directory", "invalid file name",
            "link loop" };
        unsigned long code = libssh2_sftp_last_error(r->sftp);
        snprintf(r->base.err, sizeof r->base.err, "%s%s%s", what, *what ? ": " : "",
                 code < sizeof names / sizeof names[0] ? names[code] : "SFTP error");
    } else if (r->bye[0])
        snprintf(r->base.err, sizeof r->base.err, "%s%sthe server closed the connection: %s", what, *what ? ": " : "", r->bye);
    else
        snprintf(r->base.err, sizeof r->base.err, "%s%s%s", what, *what ? ": " : "", msg && *msg ? msg : "SSH error");
    return VFR_FAIL;
}

int vfr_dial(const char *host, int port, char *err, int errsz)
{
    struct addrinfo hints, *res, *ai;
    char ps[16];
    int fd = -1, rc;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(ps, sizeof ps, "%d", port);
    rc = getaddrinfo(host, ps, &hints, &res);
    if (rc != 0) { snprintf(err, (size_t) errsz, "%s: no such host", host); return -1; }
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) snprintf(err, (size_t) errsz, "%s port %d: the connection was refused or timed out", host, port);
    return fd;
}

static void base64(const unsigned char *in, int n, char *out)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i;
    for (i = 0; i + 2 < n; i += 3) {
        *out++ = t[in[i] >> 2];
        *out++ = t[((in[i] & 3) << 4) | (in[i + 1] >> 4)];
        *out++ = t[((in[i + 1] & 15) << 2) | (in[i + 2] >> 6)];
        *out++ = t[in[i + 2] & 63];
    }
    if (n - i == 1) { *out++ = t[in[i] >> 2]; *out++ = t[(in[i] & 3) << 4]; }
    else if (n - i == 2) {
        *out++ = t[in[i] >> 2];
        *out++ = t[((in[i] & 3) << 4) | (in[i + 1] >> 4)];
        *out++ = t[(in[i + 1] & 15) << 2];
    }
    *out = '\0';                 /* no padding, as OpenSSH shows it */
}

static const char *key_name(int type)
{
    switch (type) {
    case LIBSSH2_HOSTKEY_TYPE_RSA: return "ssh-rsa";
    case LIBSSH2_HOSTKEY_TYPE_DSS: return "ssh-dss";
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return "ecdsa-sha2-nistp256";
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return "ecdsa-sha2-nistp384";
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return "ecdsa-sha2-nistp521";
    case LIBSSH2_HOSTKEY_TYPE_ED25519: return "ssh-ed25519";
    default: return "unknown";
    }
}

static int kh_keytype(int type)
{
    switch (type) {
    case LIBSSH2_HOSTKEY_TYPE_RSA: return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
    case LIBSSH2_HOSTKEY_TYPE_DSS: return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
    case LIBSSH2_HOSTKEY_TYPE_ED25519: return LIBSSH2_KNOWNHOST_KEY_ED25519;
    default: return LIBSSH2_KNOWNHOST_KEY_UNKNOWN;
    }
}

static const vfr_ops ops;
static void sftp_disconnect(vf_remote *rr);

unsigned int vfr_sftp_connect(const char *host, int port, vf_remote **out, vfr_hostkey *hk,
                              char *err, int errsz)
{
    sftp_remote *r;
    const char *fp, *key, *banner;
    size_t klen;
    int type;
    *out = 0;
    memset(hk, 0, sizeof *hk);
    if (!inited) {
        if (libssh2_init(0) != 0) { snprintf(err, (size_t) errsz, "libssh2 could not start"); return VFR_FAIL; }
        inited = 1;
    }
    r = (sftp_remote *) calloc(1, sizeof *r);
    if (!r) { snprintf(err, (size_t) errsz, "no memory"); return VFR_FAIL; }
    r->base.ops = &ops;
    r->base.secure = 1;                                 /* SSH: the password is encrypted */
    vf_strlcpy(r->host, host, sizeof r->host);
    r->port = port;
    r->fd = vfr_dial(host, port, err, errsz);
    if (r->fd < 0) { free(r); return VFR_FAIL; }
    r->s = libssh2_session_init_ex(0, 0, 0, r);         /* r is the callbacks' abstract */
    if (!r->s) { close(r->fd); free(r); snprintf(err, (size_t) errsz, "no memory"); return VFR_FAIL; }
    libssh2_session_callback_set2(r->s, LIBSSH2_CALLBACK_DISCONNECT, (libssh2_cb_generic *) on_bye);
    libssh2_session_set_blocking(r->s, 1);
    libssh2_session_set_timeout(r->s, 60000);
    if (libssh2_session_handshake(r->s, r->fd) != 0) {
        fail(r, "SSH handshake");
        vf_strlcpy(err, r->base.err, (size_t) errsz);
        sftp_disconnect(&r->base);
        return VFR_FAIL;
    }
    banner = libssh2_session_banner_get(r->s);
    vf_strlcpy(r->base.banner, banner ? banner : "", sizeof r->base.banner);

    key = libssh2_session_hostkey(r->s, &klen, &type);
    fp = libssh2_hostkey_hash(r->s, LIBSSH2_HOSTKEY_HASH_SHA256);
    vf_strlcpy(hk->type, key_name(type), sizeof hk->type);
    if (fp) {
        char b[48];
        base64((const unsigned char *) fp, 32, b);
        snprintf(hk->fingerprint, sizeof hk->fingerprint, "SHA256:%s", b);
    }
    vfp_ssh_file("known_hosts", hk->file, sizeof hk->file);
    hk->known = VFR_HK_NOFILE;
    if (key && vfp_exists(hk->file)) {
        LIBSSH2_KNOWNHOSTS *kh = libssh2_knownhost_init(r->s);
        if (kh && libssh2_knownhost_readfile(kh, hk->file, LIBSSH2_KNOWNHOST_FILE_OPENSSH) >= 0) {
            int c = libssh2_knownhost_checkp(kh, host, port, key, klen,
                                             LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW, 0);
            hk->known = c == LIBSSH2_KNOWNHOST_CHECK_MATCH ? VFR_HK_MATCH
                      : c == LIBSSH2_KNOWNHOST_CHECK_MISMATCH ? VFR_HK_MISMATCH : VFR_HK_NOTFOUND;
        } else hk->known = VFR_HK_NOTFOUND;
        if (kh) libssh2_knownhost_free(kh);
    }
    *out = &r->base;
    return 1;
}

static unsigned int sftp_trust(vf_remote *rr)
{
    sftp_remote *r = (sftp_remote *) rr;
    LIBSSH2_KNOWNHOSTS *kh;
    struct libssh2_knownhost *entry = 0;
    const char *key;
    size_t klen, outlen = 0;
    int type;
    char name[300], line[4096], file[256];
    unsigned int st;
    key = libssh2_session_hostkey(r->s, &klen, &type);
    if (!key) return fail(r, "host key");
    kh = libssh2_knownhost_init(r->s);
    if (!kh) return fail(r, "known_hosts");
    /* as ssh writes it: "host" on port 22, "[host]:port" otherwise */
    if (r->port == 22) vf_strlcpy(name, r->host, sizeof name);
    else snprintf(name, sizeof name, "[%s]:%d", r->host, r->port);
    if (libssh2_knownhost_addc(kh, name, 0, key, klen, 0, 0,
                               LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | kh_keytype(type),
                               &entry) != 0
        || libssh2_knownhost_writeline(kh, entry, line, sizeof line, &outlen, LIBSSH2_KNOWNHOST_FILE_OPENSSH) != 0) {
        libssh2_knownhost_free(kh);
        return fail(r, "known_hosts");
    }
    libssh2_knownhost_free(kh);
    while (outlen && (line[outlen - 1] == '\n' || line[outlen - 1] == '\r')) outlen--;
    line[outlen] = '\0';
    vfp_ssh_file("known_hosts", file, sizeof file);
    st = vfp_append_line(file, line);
    if (!(st & 1)) { snprintf(r->base.err, sizeof r->base.err, "%s could not be written", file); return VFR_FAIL; }
    return 1;
}

static unsigned int sftp_methods(vf_remote *rr, const char *user, char *out, int size)
{
    sftp_remote *r = (sftp_remote *) rr;
    char *m = libssh2_userauth_list(r->s, user, (unsigned int) strlen(user));
    vf_strlcpy(out, m ? m : "", (size_t) size);
    return m || libssh2_userauth_authenticated(r->s) ? 1 : fail(r, "login methods");
}

static const char *kbd_password;

static void kbd_answer(const char *name, int name_len, const char *instruction, int instruction_len,
                       int num_prompts, const LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts,
                       LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses, void **abstract)
{
    int i;
    (void) name; (void) name_len; (void) instruction; (void) instruction_len; (void) prompts; (void) abstract;
    for (i = 0; i < num_prompts; i++) {
        size_t n = strlen(kbd_password);
        responses[i].text = (char *) malloc(n + 1);       /* libssh2 frees it */
        if (responses[i].text) memcpy(responses[i].text, kbd_password, n + 1);
        responses[i].length = (unsigned int) n;
    }
}

static unsigned int start_scp(sftp_remote *r);

static unsigned int start_sftp(sftp_remote *r)
{
    libssh2_keepalive_config(r->s, 1, 30);
    if (r->scp) return start_scp(r);
    r->sftp = libssh2_sftp_init(r->s);
    if (!r->sftp) return fail(r, "SFTP");
    return 1;
}

static unsigned int sftp_login_password(vf_remote *rr, const char *user, const char *password)
{
    sftp_remote *r = (sftp_remote *) rr;
    char *m = libssh2_userauth_list(r->s, user, (unsigned int) strlen(user));
    int rc = -1;
    if (m && strstr(m, "password"))
        rc = libssh2_userauth_password(r->s, user, password);
    if (rc != 0 && !r->bye[0] && m && strstr(m, "keyboard-interactive")) {
        /* VMS sshd asks for the password this way too */
        kbd_password = password;
        rc = libssh2_userauth_keyboard_interactive(r->s, user, kbd_answer);
        kbd_password = 0;
    }
    if (rc != 0) { fail(r, ""); if (!m) vf_strlcpy(r->base.err, "the server offers no password login", sizeof r->base.err); return VFR_AUTH; }
    return start_sftp(r);
}

static unsigned int sftp_login_key(vf_remote *rr, const char *user, const char *keyfile, const char *passphrase)
{
    sftp_remote *r = (sftp_remote *) rr;
    if (libssh2_userauth_publickey_fromfile(r->s, user, 0, keyfile,
                                            passphrase && *passphrase ? passphrase : 0) != 0) {
        fail(r, "");
        return VFR_AUTH;
    }
    return start_sftp(r);
}

static unsigned int sftp_realpath(vf_remote *rr, const char *path, char *out, int size)
{
    sftp_remote *r = (sftp_remote *) rr;
    int n = libssh2_sftp_realpath(r->sftp, path, out, (unsigned int) size - 1);
    LIBSSH2_SFTP_ATTRIBUTES at;
    if (n < 0) return fail(r, path);
    out[n] = '\0';
    if (libssh2_sftp_stat(r->sftp, out, &at) != 0) return fail(r, out);
    if ((at.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && !LIBSSH2_SFTP_S_ISDIR(at.permissions)) {
        snprintf(r->base.err, sizeof r->base.err, "%s is not a directory", out);
        return VFR_FAIL;
    }
    return 1;
}

static void fill(const char *name, const LIBSSH2_SFTP_ATTRIBUTES *at, const char *longentry, vf_entry *e)
{
    memset(e, 0, sizeof *e);
    vf_strlcpy(e->name, name, sizeof e->name);
    if (at->flags & LIBSSH2_SFTP_ATTR_SIZE) { e->size = at->filesize; e->has_size = 1; }
    if (at->flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
        e->perms = (unsigned int) at->permissions & 07777;
        e->is_dir = LIBSSH2_SFTP_S_ISDIR(at->permissions) != 0;
        e->is_link = LIBSSH2_SFTP_S_ISLNK(at->permissions) != 0;
    }
    if (at->flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
        time_t t = (time_t) at->mtime;
        struct tm *tm = localtime(&t);
        if (tm) {
            e->mtime.y = tm->tm_year + 1900; e->mtime.m = tm->tm_mon + 1; e->mtime.d = tm->tm_mday;
            e->mtime.hh = tm->tm_hour; e->mtime.mm = tm->tm_min;
            e->has_time = 1;
        }
    }
    /* the owner's name is only in the "ls -l" line: its third field */
    if (longentry && *longentry) {
        char t[512], *p;
        int i;
        vf_strlcpy(t, longentry, sizeof t);
        p = strtok(t, " ");
        for (i = 0; p && i < 2; i++) p = strtok(0, " ");
        if (p) vf_strlcpy(e->owner, p, sizeof e->owner);
    }
}

static unsigned int sftp_list(vf_remote *rr, const char *dir, vfr_entry_cb cb, void *ctx)
{
    sftp_remote *r = (sftp_remote *) rr;
    LIBSSH2_SFTP_HANDLE *h = libssh2_sftp_opendir(r->sftp, dir);
    LIBSSH2_SFTP_ATTRIBUTES at;
    char name[512], longentry[1024];
    int n;
    if (!h) return fail(r, dir);
    while ((n = libssh2_sftp_readdir_ex(h, name, sizeof name, longentry, sizeof longentry, &at)) > 0) {
        vf_entry e;
        name[n] = '\0';
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        fill(name, &at, longentry, &e);
        if (e.is_link) {
            /* where does it lead? a directory link can be entered */
            char path[1024];
            LIBSSH2_SFTP_ATTRIBUTES t;
            snprintf(path, sizeof path, "%s%s%s", dir, dir[strlen(dir) - 1] == '/' ? "" : "/", name);
            if (libssh2_sftp_stat(r->sftp, path, &t) == 0 && (t.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS))
                e.is_dir = LIBSSH2_SFTP_S_ISDIR(t.permissions) != 0;
        }
        if (cb(ctx, &e)) { libssh2_sftp_closedir(h); return VFP_STOPPED; }
    }
    libssh2_sftp_closedir(h);
    if (n < 0) return fail(r, dir);
    return 1;
}

static unsigned int sftp_stat(vf_remote *rr, const char *path, vf_entry *e)
{
    sftp_remote *r = (sftp_remote *) rr;
    LIBSSH2_SFTP_ATTRIBUTES at;
    const char *base = strrchr(path, '/');
    if (libssh2_sftp_stat(r->sftp, path, &at) != 0) return fail(r, path);
    fill(base ? base + 1 : path, &at, 0, e);
    return 1;
}

static unsigned int sftp_mkdir(vf_remote *rr, const char *path)
{
    sftp_remote *r = (sftp_remote *) rr;
    return libssh2_sftp_mkdir(r->sftp, path, 0755) == 0 ? 1 : fail(r, path);
}

static unsigned int sftp_rmdir(vf_remote *rr, const char *path)
{
    sftp_remote *r = (sftp_remote *) rr;
    return libssh2_sftp_rmdir(r->sftp, path) == 0 ? 1 : fail(r, path);
}

static unsigned int sftp_unlink(vf_remote *rr, const char *path)
{
    sftp_remote *r = (sftp_remote *) rr;
    return libssh2_sftp_unlink(r->sftp, path) == 0 ? 1 : fail(r, path);
}

static unsigned int sftp_rename(vf_remote *rr, const char *from, const char *to)
{
    sftp_remote *r = (sftp_remote *) rr;
    /* no OVERWRITE: renaming onto an existing name fails rather than replacing it */
    return libssh2_sftp_rename_ex(r->sftp, from, (unsigned int) strlen(from), to, (unsigned int) strlen(to),
                                  LIBSSH2_SFTP_RENAME_ATOMIC | LIBSSH2_SFTP_RENAME_NATIVE) == 0 ? 1 : fail(r, from);
}

/* OpenSSH's posix-rename replaces in one step. A server without it (SFTP v3 rename
   never replaces): delete the old file, then rename. */
static unsigned int sftp_replace(vf_remote *rr, const char *from, const char *to)
{
    sftp_remote *r = (sftp_remote *) rr;
    unsigned int fl = (unsigned int) strlen(from), tl = (unsigned int) strlen(to);
    if (libssh2_sftp_posix_rename_ex(r->sftp, from, fl, to, tl) == 0) return 1;
    if (libssh2_sftp_unlink_ex(r->sftp, to, tl) != 0) return fail(r, to);
    return libssh2_sftp_rename_ex(r->sftp, from, fl, to, tl, LIBSSH2_SFTP_RENAME_ATOMIC | LIBSSH2_SFTP_RENAME_NATIVE) == 0
           ? 1 : fail(r, from);
}

static unsigned int sftp_chmod(vf_remote *rr, const char *path, unsigned int mode)
{
    sftp_remote *r = (sftp_remote *) rr;
    LIBSSH2_SFTP_ATTRIBUTES at;
    memset(&at, 0, sizeof at);
    at.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;          /* only these: the rest stays */
    at.permissions = mode & 07777;
    return libssh2_sftp_setstat(r->sftp, path, &at) == 0 ? 1 : fail(r, path);
}

static unsigned int sftp_open_read(vf_remote *rr, const char *path, int text, vfr_file **out)
{
    sftp_remote *r = (sftp_remote *) rr;
    sftp_file *f;
    LIBSSH2_SFTP_HANDLE *h = libssh2_sftp_open(r->sftp, path, LIBSSH2_FXF_READ, 0);
    (void) text;                                        /* SFTP moves bytes (N4) */
    if (!h) return fail(r, path);
    f = (sftp_file *) calloc(1, sizeof *f);
    if (!f) { libssh2_sftp_close(h); snprintf(r->base.err, sizeof r->base.err, "no memory"); return VFR_FAIL; }
    f->base.r = rr; f->r = r; f->h = h;
    *out = &f->base;
    return 1;
}

static unsigned int sftp_read(vfr_file *ff, char *buf, int size, int *got)
{
    sftp_file *f = (sftp_file *) ff;
    ssize_t n = libssh2_sftp_read(f->h, buf, (size_t) size);
    if (n < 0) { *got = 0; return fail(f->r, "read"); }
    *got = (int) n;
    return 1;
}

static unsigned int sftp_open_write(vf_remote *rr, const char *path, int text, unsigned long long size, vfr_file **out)
{
    sftp_remote *r = (sftp_remote *) rr;
    sftp_file *f;
    LIBSSH2_SFTP_HANDLE *h = libssh2_sftp_open(r->sftp, path,
                                               LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                                               LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
                                               LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    (void) text; (void) size;
    if (!h) return fail(r, path);
    f = (sftp_file *) calloc(1, sizeof *f);
    if (!f) { libssh2_sftp_close(h); snprintf(r->base.err, sizeof r->base.err, "no memory"); return VFR_FAIL; }
    f->base.r = rr; f->r = r; f->h = h;
    *out = &f->base;
    return 1;
}

static unsigned int sftp_write(vfr_file *ff, const char *buf, int len)
{
    sftp_file *f = (sftp_file *) ff;
    while (len > 0) {
        ssize_t n = libssh2_sftp_write(f->h, buf, (size_t) len);
        if (n < 0) return fail(f->r, "write");
        buf += n; len -= (int) n;
    }
    return 1;
}

static unsigned int sftp_close(vfr_file *ff)
{
    sftp_file *f = (sftp_file *) ff;
    int rc = libssh2_sftp_close(f->h);
    sftp_remote *r = f->r;
    free(f);
    return rc == 0 ? 1 : fail(r, "close");
}

static void sftp_keepalive(vf_remote *rr)
{
    sftp_remote *r = (sftp_remote *) rr;
    int next = 0;
    if (r && r->s) libssh2_keepalive_send(r->s, &next);
}

static void sftp_disconnect(vf_remote *rr)
{
    sftp_remote *r = (sftp_remote *) rr;
    if (!r) return;
    if (r->sftp) libssh2_sftp_shutdown(r->sftp);
    if (r->s) {
        libssh2_session_disconnect(r->s, "VFTP closed the connection");
        libssh2_session_free(r->s);
    }
    if (r->fd >= 0) close(r->fd);
    free(r);
}

static const vfr_ops ops = {
    sftp_trust, sftp_methods, sftp_login_password, sftp_login_key, sftp_realpath,
    sftp_list, sftp_stat, sftp_mkdir, sftp_rmdir, sftp_unlink, sftp_rename, sftp_replace, sftp_chmod,
    sftp_open_read, sftp_read, sftp_open_write, sftp_write, sftp_close,
    sftp_keepalive, sftp_disconnect
};

/* ---------- SCP (D15): files by scp, the rest by Unix shell commands ---------- */

typedef int (*line_fn)(void *ctx, const char *line);

/* Run script on the server as "sh -c '<script>'" (any login shell takes that),
   each line of its output to line(), which may stop it by returning nonzero.
   1 when it exits 0; VFP_STOPPED when line() stopped it; else err holds its
   first line of errors, or its exit status. */
static unsigned int run(sftp_remote *r, const char *script, line_fn line, void *ctx)
{
    LIBSSH2_CHANNEL *ch;
    static char buf[16384];
    char cur[2048], ebuf[512];
    size_t ql = strlen(script) * 4 + 16;
    char *cmd = (char *) malloc(ql + 8);
    int cl = 0, el = 0, stop = 0, code, i;
    ssize_t got;
    if (!cmd) { vf_strlcpy(r->base.err, "no memory", sizeof r->base.err); return VFR_FAIL; }
    memcpy(cmd, "sh -c ", 6);
    vf_shquote(script, cmd + 6, ql);
    ch = libssh2_channel_open_session(r->s);
    if (!ch) { free(cmd); return fail(r, "a command channel"); }
    if (libssh2_channel_exec(ch, cmd) != 0) { free(cmd); fail(r, "a command"); libssh2_channel_free(ch); return VFR_FAIL; }
    free(cmd);
    while (!stop && (got = libssh2_channel_read(ch, buf, sizeof buf)) != 0) {
        if (got < 0) { fail(r, "a command's output"); libssh2_channel_free(ch); return VFR_FAIL; }
        for (i = 0; i < (int) got && !stop; i++) {
            if (buf[i] == '\n') {
                cur[cl] = '\0';
                if (line) stop = line(ctx, cur);
                cl = 0;
            } else if (buf[i] != '\r' && cl < (int) sizeof cur - 1) cur[cl++] = buf[i];
        }
    }
    if (!stop && cl && line) { cur[cl] = '\0'; stop = line(ctx, cur); }
    while (!stop && el < (int) sizeof ebuf - 1
           && (got = libssh2_channel_read_stderr(ch, ebuf + el, sizeof ebuf - 1 - (size_t) el)) > 0)
        el += (int) got;
    ebuf[el] = '\0';
    libssh2_channel_close(ch);
    if (!stop) libssh2_channel_wait_closed(ch);
    code = libssh2_channel_get_exit_status(ch);
    libssh2_channel_free(ch);
    if (stop) return VFP_STOPPED;
    if (code != 0 || r->bye[0]) {
        char *nl = strchr(ebuf, '\n');
        if (nl) *nl = '\0';
        if (r->bye[0]) fail(r, "");
        else if (ebuf[0]) vf_strlcpy(r->base.err, ebuf, sizeof r->base.err);
        else snprintf(r->base.err, sizeof r->base.err, "the command ended with status %d", code);
        return VFR_FAIL;
    }
    return 1;
}

/* A script from tmpl with each %1 and %2 replaced by a1 and a2 quoted for sh;
   malloc'd, 0 if there is no memory. */
static char *script(const char *tmpl, const char *a1, const char *a2)
{
    size_t n = strlen(tmpl) + 1, q1 = a1 ? strlen(a1) * 4 + 3 : 0, q2 = a2 ? strlen(a2) * 4 + 3 : 0;
    const char *t;
    char *out, *o;
    for (t = tmpl; *t; t++) if (t[0] == '%' && (t[1] == '1' || t[1] == '2')) n += t[1] == '1' ? q1 : q2;
    out = (char *) malloc(n);
    if (!out) return 0;
    for (o = out, t = tmpl; *t; t++) {
        if (t[0] == '%' && (t[1] == '1' || t[1] == '2')) {
            vf_shquote(t[1] == '1' ? a1 : a2, o, t[1] == '1' ? q1 : q2);
            o += strlen(o);
            t++;
        } else *o++ = *t;
    }
    *o = '\0';
    return out;
}

static unsigned int run2(sftp_remote *r, const char *tmpl, const char *a1, const char *a2, line_fn line, void *ctx)
{
    char *sc = script(tmpl, a1, a2);
    unsigned int st;
    if (!sc) { vf_strlcpy(r->base.err, "no memory", sizeof r->base.err); return VFR_FAIL; }
    st = run(r, sc, line, ctx);
    free(sc);
    return st;
}

static int first_line(void *ctx, const char *line)
{
    char *out = (char *) ctx;
    if (!out[0]) vf_strlcpy(out, line, 1024);
    return 0;
}

/* After the login: is there a Unix shell? VMS sshd's DCL has no sh (N17). */
static unsigned int start_scp(sftp_remote *r)
{
    char got[1024];
    unsigned int st;
    got[0] = '\0';
    st = run(r, "printf 'VFTP-%s\\n' UNIX", first_line, got);
    if (!(st & 1) || strcmp(got, "VFTP-UNIX")) {
        snprintf(r->base.err, sizeof r->base.err, "this server has no Unix shell, which SCP needs to list "
                 "and manage files: use SFTP");
        return VFR_FAIL;
    }
    return 1;
}

static unsigned int scp_realpath(vf_remote *rr, const char *path, char *out, int size)
{
    sftp_remote *r = (sftp_remote *) rr;
    char got[1024];
    unsigned int st;
    got[0] = '\0';
    st = run2(r, "cd -- %1 && pwd -P", path, 0, first_line, got);
    if (!(st & 1)) return st;
    if (got[0] != '/') { snprintf(r->base.err, sizeof r->base.err, "%s: no directory from pwd", path); return VFR_FAIL; }
    vf_strlcpy(out, got, (size_t) size);
    return 1;
}

/* "ls -la" lines, then after a marker the links that lead to directories */
#define DIRLINKS "//VFTP-DIRLINKS"
typedef struct {
    vf_ftp_list l;
    vf_entry *e;
    int n, cap, links, full;
} lsctx;

static int ls_add(lsctx *c, const vf_entry *e)
{
    if (c->n == c->cap) {
        int cap = c->cap ? c->cap * 2 : 64;
        vf_entry *ne = (vf_entry *) realloc(c->e, (size_t) cap * sizeof *ne);
        if (!ne) { c->full = 1; return 1; }
        c->e = ne; c->cap = cap;
    }
    c->e[c->n++] = *e;
    return 0;
}

static int ls_line(void *ctx, const char *line)
{
    lsctx *c = (lsctx *) ctx;
    vf_entry e;
    int i;
    if (c->links) {
        for (i = 0; i < c->n; i++) if (c->e[i].is_link && !strcmp(c->e[i].name, line)) c->e[i].is_dir = 1;
        return 0;
    }
    if (!strcmp(line, DIRLINKS)) {
        if (vf_ftp_list_end(&c->l, &e)) ls_add(c, &e);
        c->links = 1;
        return 0;
    }
    if (vf_ftp_list_line(&c->l, line, &e) && ls_add(c, &e)) return 1;
    while (vf_ftp_list_more(&c->l, &e)) if (ls_add(c, &e)) return 1;
    return 0;
}

static unsigned int scp_list(vf_remote *rr, const char *dir, vfr_entry_cb cb, void *ctx)
{
    sftp_remote *r = (sftp_remote *) rr;
    lsctx c;
    vf_when now;
    unsigned int st;
    int i;
    memset(&c, 0, sizeof c);
    vfp_now(&now);
    vf_ftp_list_init(&c.l, now.y, now.m);
    st = run2(r, "cd -- %1 || exit 1; LC_ALL=C ls -la || exit 1; echo '" DIRLINKS "'; "
                 "for f in .* *; do [ -L \"$f\" ] && [ -d \"$f\" ] && printf '%s\\n' \"$f\"; done; exit 0",
              dir, 0, ls_line, &c);
    if (c.full) { free(c.e); vf_strlcpy(r->base.err, "no memory for the listing", sizeof r->base.err); return VFR_FAIL; }
    if (!(st & 1)) { free(c.e); return st; }
    for (i = 0; i < c.n; i++) if (cb(ctx, &c.e[i])) { free(c.e); return VFP_STOPPED; }
    free(c.e);
    return 1;
}

typedef struct { vf_ftp_list l; vf_entry e; int got, dir; } statctx;

static int stat_line(void *ctx, const char *line)
{
    statctx *c = (statctx *) ctx;
    if (!strcmp(line, "//VFTP-DIR")) c->dir = 1;
    else if (!c->got) c->got = vf_ftp_list_line(&c->l, line, &c->e);
    return 0;
}

static unsigned int scp_stat(vf_remote *rr, const char *path, vf_entry *e)
{
    sftp_remote *r = (sftp_remote *) rr;
    statctx c;
    vf_when now;
    unsigned int st;
    const char *base = strrchr(path, '/');
    memset(&c, 0, sizeof c);
    vfp_now(&now);
    vf_ftp_list_init(&c.l, now.y, now.m);
    st = run2(r, "LC_ALL=C ls -ld -- %1 || exit 1; [ -d %1 ] && echo //VFTP-DIR; exit 0", path, 0, stat_line, &c);
    if (!(st & 1)) return st;
    if (!c.got) { snprintf(r->base.err, sizeof r->base.err, "%s: ls -ld gave nothing VFTP can read", path); return VFR_FAIL; }
    *e = c.e;
    vf_strlcpy(e->name, base && base[1] ? base + 1 : path, sizeof e->name);
    if (c.dir) e->is_dir = 1;
    return 1;
}

static unsigned int scp_mkdir(vf_remote *rr, const char *path)
{ return run2((sftp_remote *) rr, "mkdir -- %1", path, 0, 0, 0); }
static unsigned int scp_rmdir(vf_remote *rr, const char *path)
{ return run2((sftp_remote *) rr, "rmdir -- %1", path, 0, 0, 0); }
static unsigned int scp_unlink(vf_remote *rr, const char *path)
{ return run2((sftp_remote *) rr, "rm -- %1", path, 0, 0, 0); }

static unsigned int scp_rename(vf_remote *rr, const char *from, const char *to)
{
    /* never replaces: as SFTP's rename */
    return run2((sftp_remote *) rr, "if [ -e %2 ] || [ -L %2 ]; then echo 'there is one already: '%2 >&2; "
                "exit 1; fi; mv -- %1 %2", from, to, 0, 0);
}

static unsigned int scp_replace(vf_remote *rr, const char *from, const char *to)
{ return run2((sftp_remote *) rr, "mv -f -- %1 %2", from, to, 0, 0); }

static unsigned int scp_chmod(vf_remote *rr, const char *path, unsigned int mode)
{
    char tmpl[32];
    snprintf(tmpl, sizeof tmpl, "chmod %o -- %%1", mode & 07777);
    return run2((sftp_remote *) rr, tmpl, path, 0, 0, 0);
}

typedef struct {
    vfr_file base;
    sftp_remote *r;
    LIBSSH2_CHANNEL *ch;
    unsigned long long size, done;
    int writing;
} scp_file;

static unsigned int scp_open_read(vf_remote *rr, const char *path, int text, vfr_file **out)
{
    sftp_remote *r = (sftp_remote *) rr;
    libssh2_struct_stat sb;
    scp_file *f;
    LIBSSH2_CHANNEL *ch;
    (void) text;                                        /* bytes, as SFTP */
    memset(&sb, 0, sizeof sb);
    ch = libssh2_scp_recv2(r->s, path, &sb);
    if (!ch) return fail(r, path);
    f = (scp_file *) calloc(1, sizeof *f);
    if (!f) { libssh2_channel_free(ch); vf_strlcpy(r->base.err, "no memory", sizeof r->base.err); return VFR_FAIL; }
    f->base.r = rr; f->r = r; f->ch = ch;
    f->size = (unsigned long long) sb.st_size;
    *out = &f->base;
    return 1;
}

static unsigned int scp_read(vfr_file *ff, char *buf, int size, int *got)
{
    scp_file *f = (scp_file *) ff;
    unsigned long long left = f->size - f->done;
    ssize_t n;
    *got = 0;
    if (!left) return 1;
    if ((unsigned long long) size > left) size = (int) left;
    n = libssh2_channel_read(f->ch, buf, (size_t) size);
    if (n < 0) return fail(f->r, "read");
    if (n == 0) { snprintf(f->r->base.err, sizeof f->r->base.err, "the file ended after %llu of %llu bytes", f->done, f->size); return VFR_FAIL; }
    f->done += (unsigned long long) n;
    *got = (int) n;
    return 1;
}

static unsigned int scp_open_write(vf_remote *rr, const char *path, int text, unsigned long long size, vfr_file **out)
{
    sftp_remote *r = (sftp_remote *) rr;
    scp_file *f;
    LIBSSH2_CHANNEL *ch;
    (void) text;
    ch = libssh2_scp_send64(r->s, path, 0644, (libssh2_int64_t) size, 0, 0);
    if (!ch) return fail(r, path);
    f = (scp_file *) calloc(1, sizeof *f);
    if (!f) { libssh2_channel_free(ch); vf_strlcpy(r->base.err, "no memory", sizeof r->base.err); return VFR_FAIL; }
    f->base.r = rr; f->r = r; f->ch = ch; f->writing = 1;
    f->size = size;
    *out = &f->base;
    return 1;
}

static unsigned int scp_write(vfr_file *ff, const char *buf, int len)
{
    scp_file *f = (scp_file *) ff;
    if (f->done + (unsigned long long) len > f->size) {
        snprintf(f->r->base.err, sizeof f->r->base.err, "more than the %llu bytes announced", f->size);
        return VFR_FAIL;
    }
    while (len > 0) {
        ssize_t n = libssh2_channel_write(f->ch, buf, (size_t) len);
        if (n < 0) return fail(f->r, "write");
        buf += n; len -= (int) n;
        f->done += (unsigned long long) n;
    }
    return 1;
}

static unsigned int scp_close(vfr_file *ff)
{
    scp_file *f = (scp_file *) ff;
    sftp_remote *r = f->r;
    unsigned int st = 1;
    if (f->writing) {
        if (f->done != f->size) {
            /* cut short (cancelled, or failed): the server keeps what it got; the app deletes it */
            snprintf(r->base.err, sizeof r->base.err, "%llu of %llu bytes sent", f->done, f->size);
            st = VFR_FAIL;
        } else if (libssh2_channel_send_eof(f->ch) != 0 || libssh2_channel_wait_eof(f->ch) != 0) {
            st = fail(r, "close");
        }
        if (st & 1) libssh2_channel_wait_closed(f->ch);
        if ((st & 1) && libssh2_channel_get_exit_status(f->ch) != 0) {
            snprintf(r->base.err, sizeof r->base.err, "the server's scp ended with status %d",
                     libssh2_channel_get_exit_status(f->ch));
            st = VFR_FAIL;
        }
    }
    libssh2_channel_free(f->ch);
    free(f);
    return st;
}

static const vfr_ops scp_ops = {
    sftp_trust, sftp_methods, sftp_login_password, sftp_login_key, scp_realpath,
    scp_list, scp_stat, scp_mkdir, scp_rmdir, scp_unlink, scp_rename, scp_replace, scp_chmod,
    scp_open_read, scp_read, scp_open_write, scp_write, scp_close,
    sftp_keepalive, sftp_disconnect
};

unsigned int vfr_scp_connect(const char *host, int port, vf_remote **out, vfr_hostkey *hk,
                             char *err, int errsz)
{
    unsigned int st = vfr_sftp_connect(host, port, out, hk, err, errsz);
    if (st & 1) {
        sftp_remote *r = (sftp_remote *) *out;
        r->scp = 1;
        r->base.ops = &scp_ops;
        r->base.need_size = 1;
    }
    return st;
}
