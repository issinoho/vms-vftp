/* vf_app.c - VFTP's state, the keys, the Open form, the panes and the
   operations: transfers (Get, Put), delete, rename, make directory, view.
   A download never replaces a file (D8); an upload that would replace one,
   and every delete, is shown and asked first (default No). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "vf_app.h"
#include "vf_util.h"
#include "vf_path.h"

#define CHUNK 32768
#define PART ".vftp-part"            /* a replacing Put's temporary name: name.vftp-part */

/* ---------- messages and text screens ---------- */

void vf_set_msg(vf_app *a, int err, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->msg, sizeof a->msg, fmt, ap);
    va_end(ap);
    a->msg_err = err;
}

static void text_free(vf_app *a)
{
    int i;
    for (i = 0; i < a->ntext; i++) free(a->text[i]);
    free(a->text);
    a->text = 0; a->ntext = 0; a->ttop = 0;
}

static void text_addn(vf_app *a, const char *s, int len)
{
    char **t;
    if ((a->ntext & 255) == 0) {
        t = (char **) realloc(a->text, (size_t) (a->ntext + 256) * sizeof *t);
        if (!t) return;
        a->text = t;
    }
    if ((a->text[a->ntext] = (char *) malloc((size_t) len + 1)) != 0) {
        memcpy(a->text[a->ntext], s, (size_t) len);
        a->text[a->ntext][len] = '\0';
        a->ntext++;
    }
}

static void text_add(vf_app *a, const char *fmt, ...)
{
    char line[1200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    text_addn(a, line, (int) strlen(line));
}

static void status_text(unsigned int st, char *buf, int size) { vfp_message(st, buf, size); }

const char *vf_xmode_name(int x) { return x == X_ASCII ? "ASCII" : x == X_BINARY ? "BINARY" : "AUTO"; }

static void logf_(const char *fmt, ...)
{
    char line[1400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    vfp_log(line);
}

/* ---------- reading the panes ---------- */

static int add_local(void *ctx, const vf_entry *e) { return !vf_pane_add((vf_pane *) ctx, e); }

static unsigned int read_local(vf_app *a)
{
    vf_pane *p = &a->pane[PANE_LOCAL];
    vf_entry par;
    char up[VF_SPEC_MAX];
    unsigned int st;
    vf_pane_clear(p);
    if (vf_lparent(p->dir, up, sizeof up)) {
        memset(&par, 0, sizeof par);
        par.is_parent = par.is_dir = 1;
        vf_strlcpy(par.name, "[-]", sizeof par.name);
        vf_pane_add(p, &par);
    }
    st = vfp_list(p->dir, a->all_versions, add_local, p);
    p->status = st;
    vf_pane_rebuild(p);
    return st;
}

static unsigned int read_remote(vf_app *a)
{
    vf_pane *p = &a->pane[PANE_REMOTE];
    vf_entry par;
    char up[VF_SPEC_MAX];
    unsigned int st;
    vf_pane_clear(p);
    if (!a->rem) { vf_pane_rebuild(p); return 1; }
    if (vf_rparent(p->dir, up, sizeof up)) {
        memset(&par, 0, sizeof par);
        par.is_parent = par.is_dir = 1;
        vf_strlcpy(par.name, "..", sizeof par.name);
        vf_pane_add(p, &par);
    }
    st = vfr_list(a->rem, p->dir, add_local, p);
    p->status = st;
    vf_pane_rebuild(p);
    return st;
}

static void reread(vf_app *a, int which)
{
    if (which == PANE_LOCAL) read_local(a); else read_remote(a);
}

/* ---------- start ---------- */

static void form_defaults(vf_app *a)
{
    char key[256];
    static const char *keys[] = { "id_ed25519", "id_ecdsa", "id_rsa" };
    int i;
    memset(a->fv, 0, sizeof a->fv);
    vf_strlcpy(a->fv[FF_PORT], "22", VF_FIELD);
    vf_strlcpy(a->fv[FF_USER], a->sess.user, VF_FIELD);
    for (i = 0; a->fv[FF_USER][i]; i++)
        if (a->fv[FF_USER][i] >= 'A' && a->fv[FF_USER][i] <= 'Z') a->fv[FF_USER][i] += 'a' - 'A';
    for (i = 0; i < 3; i++) {
        vfp_ssh_file(keys[i], key, sizeof key);
        if (vfp_exists(key)) { vf_strlcpy(a->fv[FF_KEY], key, VF_FIELD); break; }
    }
}

/* "user@host:port" or "host" from the command line into the form. */
static void parse_target(vf_app *a, const char *t)
{
    char b[512], *at, *colon;
    vf_strlcpy(b, t, sizeof b);
    at = strrchr(b, '@');
    if (at) { *at = '\0'; vf_strlcpy(a->fv[FF_USER], b, VF_FIELD); memmove(b, at + 1, strlen(at + 1) + 1); }
    colon = strrchr(b, ':');
    if (colon && !strchr(colon, ']')) {
        *colon = '\0';
        if (colon[1] >= '0' && colon[1] <= '9') vf_strlcpy(a->fv[FF_PORT], colon + 1, VF_FIELD);
        else if (colon[1]) vf_strlcpy(a->fv[FF_DIR], colon + 1, VF_FIELD);
    }
    vf_strlcpy(a->fv[FF_HOST], b, VF_FIELD);
}

static void form_open(vf_app *a);

int vf_app_init(vf_app *a, const char *target, int rows, int cols)
{
    vf_when w;
    char dir[VF_SPEC_MAX], t[512];
    unsigned int st;
    vfp_session keep = a->sess;
    memset(a, 0, sizeof *a);
    a->sess = keep;
    if (!vf_screen_init(&a->scr, rows, cols)) return 0;
    a->rows = rows; a->cols = cols;
    vf_pane_init(&a->pane[PANE_LOCAL], 1);
    vf_pane_init(&a->pane[PANE_REMOTE], 0);
    vfp_now(&w);
    a->year = w.y;
    vfp_getenv("VFTP$TEXT_TYPES", a->text_types, sizeof a->text_types);
    st = vfp_locate("", dir, sizeof dir, &a->ods5);
    if (st & 1) vf_strlcpy(a->pane[PANE_LOCAL].dir, dir, VF_SPEC_MAX);
    else { vfp_message(st, t, sizeof t); vf_set_msg(a, 1, "Current directory: %s", t); }
    read_local(a);
    form_defaults(a);
    if (target && *target) parse_target(a, target);
    form_open(a);
    if (!target || !*target) a->fcur = FF_HOST;
    else if (!a->fv[FF_USER][0]) a->fcur = FF_USER;
    else a->fcur = FF_PASSWORD;
    vf_edit_init(&a->fedit, a->fv[a->fcur]);
    return 1;
}

void vf_app_free(vf_app *a)
{
    if (a->rem) vfr_disconnect(a->rem);
    a->rem = 0;
    text_free(a);
    vf_pane_free(&a->pane[0]);
    vf_pane_free(&a->pane[1]);
    vf_screen_free(&a->scr);
}

/* ---------- the Open form and logging in ---------- */

static const char *labels[FF_COUNT] = { "Host", "Port", "User", "Password", "Key file",
                                        "Key passphrase", "Remote directory" };
static const char *hints[FF_COUNT] = {
    "The server's name or address.",
    "The SSH port, normally 22.",
    "Your user name on the server.",
    "Leave empty to log in with the key file. Never stored (D9).",
    "A private key in OpenSSH format; empty: password only.",
    "Only if the key file is protected by a passphrase.",
    "Where to start; empty: your login directory.",
};
const char *vf_field_label(int i) { return labels[i]; }
const char *vf_field_hint(int i) { return hints[i]; }

static void wipe(char *s, size_t n) { volatile char *p = s; while (n--) *p++ = 0; }

static void form_open(vf_app *a)
{
    a->mode = M_FORM;
    a->form_err[0] = '\0';
    vf_edit_init(&a->fedit, a->fv[a->fcur]);
}

static void form_store(vf_app *a) { vf_strlcpy(a->fv[a->fcur], a->fedit.buf, VF_FIELD); }
static void form_load(vf_app *a) { vf_edit_init(&a->fedit, a->fv[a->fcur]); }

static void disconnect(vf_app *a, const char *why)
{
    if (a->rem) {
        vfr_disconnect(a->rem);
        logf_("DISCONNECT %s@%s (%s)", a->user, a->host, why);
    }
    a->rem = 0;
    a->pane[PANE_REMOTE].dir[0] = '\0';
    vf_pane_clear(&a->pane[PANE_REMOTE]);
    vf_pane_rebuild(&a->pane[PANE_REMOTE]);
    if (a->act == PANE_REMOTE) a->act = PANE_LOCAL;
}

static void form_fail(vf_app *a, int field, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->form_err, sizeof a->form_err, fmt, ap);
    va_end(ap);
    a->mode = M_FORM;
    a->fcur = field;
    form_load(a);
}

static void login(vf_app *a)
{
    char methods[256], dir[VF_SPEC_MAX];
    unsigned int st = VFR_AUTH;
    const char *how = "";
    methods[0] = '\0';
    vfr_methods(a->rem, a->user, methods, sizeof methods);
    if (a->fv[FF_PASSWORD][0]) {
        how = "password";
        st = vfr_login_password(a->rem, a->user, a->fv[FF_PASSWORD]);
    } else if (a->fv[FF_KEY][0]) {
        how = "key";
        st = vfr_login_key(a->rem, a->user, a->fv[FF_KEY], a->fv[FF_PHRASE]);
    } else {
        disconnect(a, "no credentials");
        form_fail(a, FF_PASSWORD, "Give a password or a key file (the server offers %s)", methods);
        return;
    }
    wipe(a->fv[FF_PASSWORD], VF_FIELD);
    wipe(a->fv[FF_PHRASE], VF_FIELD);
    wipe(a->fedit.buf, sizeof a->fedit.buf);
    if (!(st & 1)) {
        char e[256];
        vf_strlcpy(e, vfr_error(a->rem), sizeof e);
        logf_("LOGIN %s@%s:%d by %s FAILED: %s", a->user, a->host, a->port, how, e);
        disconnect(a, "login failed");
        if (st == VFR_AUTH)
            form_fail(a, *how == 'p' ? FF_PASSWORD : FF_KEY, "Login refused: %s (the server offers %s)",
                      e, methods);
        else
            form_fail(a, FF_HOST, "Logged in, but the server gave no SFTP: %s", e);
        return;
    }
    logf_("LOGIN %s@%s:%d by %s", a->user, a->host, a->port, how);
    st = vfr_realpath(a->rem, a->fv[FF_DIR][0] ? a->fv[FF_DIR] : ".", dir, sizeof dir);
    if (!(st & 1)) {
        vf_set_msg(a, 1, "Directory %s: %s", a->fv[FF_DIR], vfr_error(a->rem));
        st = vfr_realpath(a->rem, ".", dir, sizeof dir);
        if (!(st & 1)) vf_strlcpy(dir, "/", sizeof dir);
    } else vf_set_msg(a, 0, "Logged in to %s as %s", a->host, a->user);
    vf_strlcpy(a->pane[PANE_REMOTE].dir, dir, VF_SPEC_MAX);
    a->pane[PANE_REMOTE].cur = a->pane[PANE_REMOTE].top = 0;
    read_remote(a);
    a->act = PANE_REMOTE;
    a->mode = M_MAIN;
    a->last_alive = vfp_clock();
}

static void hostkey_ask(vf_app *a)
{
    text_free(a);
    vf_strlcpy(a->text_title, "A server VFTP has not seen before", sizeof a->text_title);
    text_add(a, "The authenticity of %s (port %d) can't be established:", a->host, a->port);
    text_add(a, "%s (%s)", a->hk.known == VFR_HK_NOFILE ? "There is no known_hosts file yet"
                                                         : "Its key is not in known_hosts", a->hk.file);
    text_add(a, "");
    text_add(a, "  Key type      %s", a->hk.type);
    text_add(a, "  Fingerprint   %s", a->hk.fingerprint);
    text_add(a, "");
    text_add(a, "Check the fingerprint with the server's owner (on the server:");
    text_add(a, "ssh-keygen -l -f its host key file) before you answer Y.");
    text_add(a, "Y adds the key to known_hosts, as ssh does, and logs in.");
    a->mode = M_CONFIRM;
    a->prompt = P_ASK;
    a->ask = ASK_HOSTKEY;
    vf_strlcpy(a->prompt_label, "Trust this server and connect? (Y/N) [N]: ", sizeof a->prompt_label);
    vfp_flush_keys();
}

static void connect_now(vf_app *a)
{
    char err[256];
    unsigned int st;
    int port;
    form_store(a);
    vf_trim(a->fv[FF_HOST]); vf_trim(a->fv[FF_USER]); vf_trim(a->fv[FF_PORT]);
    if (!a->fv[FF_HOST][0]) { form_fail(a, FF_HOST, "Which server?"); return; }
    if (!a->fv[FF_USER][0]) { form_fail(a, FF_USER, "Which user?"); return; }
    port = atoi(a->fv[FF_PORT]);
    if (port <= 0 || port > 65535) { form_fail(a, FF_PORT, "A port is a number from 1 to 65535"); return; }
    if (a->rem) disconnect(a, "new connection");
    vf_strlcpy(a->host, a->fv[FF_HOST], sizeof a->host);
    vf_strlcpy(a->user, a->fv[FF_USER], sizeof a->user);
    a->port = port;
    st = vfr_connect(VFR_SFTP, a->host, port, &a->rem, &a->hk, err, sizeof err);
    if (!(st & 1)) { form_fail(a, FF_HOST, "Cannot connect: %s", err); a->rem = 0; return; }
    vf_strlcpy(a->banner, vfr_banner(a->rem), sizeof a->banner);
    if (a->hk.known == VFR_HK_MISMATCH) {
        logf_("HOSTKEY MISMATCH %s:%d %s %s", a->host, port, a->hk.type, a->hk.fingerprint);
        disconnect(a, "host key changed");
        form_fail(a, FF_HOST, "REFUSED: %s's key %s is not the one in known_hosts: it was "
                  "changed, or someone is in between.", a->host, a->hk.fingerprint);
        return;
    }
    if (a->hk.known != VFR_HK_MATCH) { hostkey_ask(a); return; }
    login(a);
}

/* ---------- the files an operation works on ---------- */

/* The tagged entries of pane, or the current one; files only unless dirs.
   Returns how many; skipped counts directories left out. */
static int collect(vf_app *a, int which, int dirs, int *skipped)
{
    vf_pane *p = &a->pane[which];
    int i;
    a->nops = 0; a->ops_pane = which; *skipped = 0;
    for (i = 0; i < p->nvis && a->nops < VF_MAXOPS; i++) {
        vf_entry *e = &p->e[p->vis[i]];
        if (!e->tagged) continue;
        if (e->is_dir && !dirs) { (*skipped)++; continue; }
        a->ops[a->nops++] = p->vis[i];
    }
    if (!a->nops && !*skipped) {
        vf_entry *e = vf_pane_cur(p);
        if (e && !e->is_parent) {
            if (e->is_dir && !dirs) (*skipped)++;
            else a->ops[a->nops++] = p->vis[p->cur];
        }
    }
    return a->nops;
}

/* ---------- transfers ---------- */

static void progress(vf_app *a, int force)
{
    double now = vfp_clock();
    int k;
    if (!force && now - a->xfer_drawn < 0.25) return;
    a->xfer_drawn = now;
    vf_app_draw(a);
    vfp_draw(&a->scr);
    while ((k = vfp_key(0)) != VK_TIMEOUT) {
        if (k == VK_F10 || k == VK_CTRL_Z || k == 3) a->cancel = 1;
        else if (k == VK_CTRL_W) vfp_repaint();
    }
}

static unsigned int write_rec(void *ctx, const char *rec, int len)
{
    return vfp_write((vfp_file *) ctx, rec, len);
}

static int want_text(vf_app *a, const char *name)
{
    if (a->xmode == X_ASCII) return 1;
    if (a->xmode == X_BINARY) return 0;
    return vf_text_type(name, a->text_types[0] ? a->text_types : 0);
}

/* Download one remote entry into the local directory. 1 on success. */
static int get_one(vf_app *a, const vf_entry *e, char *err, int esz)
{
    char rpath[VF_SPEC_MAX], lname[300], lspec[VF_SPEC_MAX], result[VF_SPEC_MAX];
    static char buf[CHUNK];
    vfr_file *rf;
    vfp_file *lf;
    unsigned int st;
    int got, text;
    vf_rjoin(a->pane[PANE_REMOTE].dir, e->name, rpath, sizeof rpath);
    vf_local_name(e->name, a->ods5, lname, sizeof lname);
    vf_lfile(a->pane[PANE_LOCAL].dir, lname, lspec, sizeof lspec);
    text = want_text(a, e->name);
    a->xfer_text = text;
    vf_strlcpy(a->xfer_name, e->name, sizeof a->xfer_name);
    a->xfer_done = 0; a->xfer_total = e->size;
    st = vfr_open_read(a->rem, rpath, &rf);
    if (!(st & 1)) { snprintf(err, (size_t) esz, "%s: %s", e->name, vfr_error(a->rem)); return 0; }
    st = vfp_open_write(lspec, text, e->size, &lf, result, sizeof result);
    if (!(st & 1)) {
        char m[200];
        vfr_close(rf);
        status_text(st, m, sizeof m);
        snprintf(err, (size_t) esz, "%s: %s", lspec, m);
        return 0;
    }
    vf_lines_init(&a->lines);
    progress(a, 1);
    for (;;) {
        st = vfr_read(rf, buf, sizeof buf, &got);
        if (!(st & 1)) { snprintf(err, (size_t) esz, "%s: %s", e->name, vfr_error(a->rem)); break; }
        if (got == 0) {
            if (text) st = vf_lines_end(&a->lines, write_rec, lf);
            break;
        }
        st = text ? vf_lines_feed(&a->lines, buf, got, write_rec, lf) : vfp_write(lf, buf, got);
        if (!(st & 1)) {
            char m[200];
            status_text(st, m, sizeof m);
            snprintf(err, (size_t) esz, "%s: %s", result, m);
            break;
        }
        a->xfer_done += (unsigned long long) got;
        a->batch_done += (unsigned long long) got;
        progress(a, 0);
        if (a->cancel) { snprintf(err, (size_t) esz, "cancelled"); st = 0; break; }
    }
    vfr_close(rf);
    if (!(st & 1)) {
        vfp_close(lf, 0);
        logf_("GET %s -> %s FAILED: %s", rpath, lspec, err);
        return 0;
    }
    st = vfp_close(lf, 1);
    if (!(st & 1)) {
        char m[200];
        status_text(st, m, sizeof m);
        snprintf(err, (size_t) esz, "%s: %s", result, m);
        return 0;
    }
    logf_("GET %s -> %s %s %llu bytes", rpath, result, text ? "ASCII" : "BINARY", a->xfer_done);
    if (a->lines.split) logf_("  %d records longer than %d were split", a->lines.split, VF_REC_MAX);
    return 1;
}

/* Upload one local entry into the remote directory. 1 on success. A file that
   replaces one (a server without versions; the user said Y) is written under a
   temporary name and renamed over the old one at the end, so a cancel or a failure
   leaves the old file as it was. */
static int put_one(vf_app *a, const vf_entry *e, char *err, int esz)
{
    char lspec[VF_SPEC_MAX], rname[300], rpath[VF_SPEC_MAX], wpath[VF_SPEC_MAX];
    const vf_entry *old;
    int replacing;
    static char buf[CHUNK + 1], out[CHUNK + 2];
    vfr_file *rf;
    vfp_file *lf;
    unsigned int st;
    int got, text = a->xmode == X_ASCII ? 1 : a->xmode == X_BINARY ? 0 : -1, olen = 0;
    unsigned long long est;
    vf_lfile(a->pane[PANE_LOCAL].dir, e->name, lspec, sizeof lspec);
    vf_remote_name(e->name, rname, sizeof rname);
    vf_rjoin(a->pane[PANE_REMOTE].dir, rname, rpath, sizeof rpath);
    old = vf_pane_find(&a->pane[PANE_REMOTE], rname);
    replacing = old && !old->is_parent && !old->is_dir;
    if (replacing) snprintf(wpath, sizeof wpath, "%s" PART, rpath);
    else vf_strlcpy(wpath, rpath, sizeof wpath);
    vf_strlcpy(a->xfer_name, e->name, sizeof a->xfer_name);
    st = vfp_open_read(lspec, &text, &est, &lf);
    if (!(st & 1)) {
        char m[200];
        status_text(st, m, sizeof m);
        snprintf(err, (size_t) esz, "%s: %s", e->name, m);
        return 0;
    }
    a->xfer_text = text;
    a->xfer_done = 0; a->xfer_total = est;
    st = vfr_open_write(a->rem, wpath, &rf);
    if (!(st & 1)) {
        vfp_close(lf, 1);
        snprintf(err, (size_t) esz, "%s: %s", wpath, vfr_error(a->rem));
        return 0;
    }
    progress(a, 1);
    for (;;) {
        st = vfp_read(lf, buf, CHUNK, &got);
        if (st == VFP_EOF) { st = 1; break; }
        if (!(st & 1)) {
            char m[200];
            status_text(st, m, sizeof m);
            snprintf(err, (size_t) esz, "%s: %s", e->name, m);
            break;
        }
        if (text) {
            if (olen + got + 1 > CHUNK) {
                if (!((st = vfr_write(rf, out, olen)) & 1)) break;
                olen = 0;
            }
            memcpy(out + olen, buf, (size_t) got);
            olen += got;
            out[olen++] = '\n';
            a->xfer_done += (unsigned long long) got + 1;
            a->batch_done += (unsigned long long) got + 1;
        } else {
            if (!((st = vfr_write(rf, buf, got)) & 1)) break;
            a->xfer_done += (unsigned long long) got;
            a->batch_done += (unsigned long long) got;
        }
        progress(a, 0);
        if (a->cancel) { snprintf(err, (size_t) esz, "cancelled"); st = 0; break; }
    }
    if ((st & 1) && olen) st = vfr_write(rf, out, olen);
    if (!(st & 1) && !err[0]) snprintf(err, (size_t) esz, "%s: %s", wpath, vfr_error(a->rem));
    vfp_close(lf, 1);
    {
        unsigned int cs = vfr_close(rf);
        if ((st & 1) && !(cs & 1)) { st = cs; snprintf(err, (size_t) esz, "%s: %s", wpath, vfr_error(a->rem)); }
    }
    if (!(st & 1)) {
        logf_("PUT %s -> %s FAILED: %s", lspec, wpath, err);
        /* no half file left: the temporary one always, a new one when cancelled */
        if (replacing || strcmp(err, "cancelled") == 0) vfr_unlink(a->rem, wpath);
        return 0;
    }
    if (replacing && !(vfr_replace(a->rem, wpath, rpath) & 1)) {
        snprintf(err, (size_t) esz, "%s: %s (the new copy is %s" PART ")", rpath, vfr_error(a->rem), rname);
        logf_("PUT %s -> %s FAILED: %s", lspec, rpath, err);
        return 0;
    }
    logf_("PUT %s -> %s %s %llu bytes", lspec, rpath, text ? "ASCII" : "BINARY", a->xfer_done);
    return 1;
}

static void run_transfer(vf_app *a)
{
    vf_pane *src = &a->pane[a->ops_pane];
    int i, ok = 0, failed = 0;
    char err[512], first[600];
    double t0 = vfp_clock(), secs;
    first[0] = '\0';
    a->xfer_on = 1; a->xfer_get = a->ops_get; a->xfer_n = a->nops;
    a->cancel = 0; a->batch_done = 0; a->xfer_t0 = t0; a->xfer_drawn = 0;
    for (i = 0; i < a->nops && !a->cancel; i++) {
        vf_entry e = src->e[a->ops[i]];
        a->xfer_i = i + 1;
        err[0] = '\0';
        if (a->ops_get ? get_one(a, &e, err, sizeof err) : put_one(a, &e, err, sizeof err)) {
            ok++;
            src->e[a->ops[i]].tagged = 0;
        } else {
            failed++;
            if (!first[0]) vf_strlcpy(first, err, sizeof first);
        }
    }
    a->xfer_on = 0;
    secs = vfp_clock() - t0;
    reread(a, a->ops_get ? PANE_LOCAL : PANE_REMOTE);
    if (a->ops_get) vf_pane_rebuild(&a->pane[PANE_REMOTE]); else vf_pane_rebuild(&a->pane[PANE_LOCAL]);
    if (a->cancel)
        vf_set_msg(a, 1, "Cancelled after %d file%s; the file being moved was not kept", ok, ok == 1 ? "" : "s");
    else if (failed)
        vf_set_msg(a, 1, "%d copied, %d failed: %s", ok, failed, first);
    else {
        char b[16], r[16];
        vf_fmt_bytes(a->batch_done, b, sizeof b);
        vf_fmt_bytes(secs > 0.01 ? (unsigned long long) ((double) a->batch_done / secs) : a->batch_done, r, sizeof r);
        vf_set_msg(a, 0, "%s %d file%s, %s bytes in %.1f s (%s/s)", a->ops_get ? "Got" : "Put",
                   ok, ok == 1 ? "" : "s", b, secs, r);
    }
}

static void transfer(vf_app *a, int get)
{
    int skipped, i, replace = 0;
    char rname[300];
    if (!a->rem) { vf_set_msg(a, 1, "Not connected: O opens a connection"); return; }
    collect(a, get ? PANE_REMOTE : PANE_LOCAL, 0, &skipped);
    a->ops_get = get;
    if (!a->nops) {
        vf_set_msg(a, 1, skipped ? "Directories are not copied yet (Phase 3): tag files"
                                 : "No file here to %s", get ? "get" : "put");
        return;
    }
    if (!get) {
        /* would an upload replace a remote file? ask first */
        text_free(a);
        for (i = 0; i < a->nops; i++) {
            vf_entry *e = &a->pane[PANE_LOCAL].e[a->ops[i]], *r;
            vf_remote_name(e->name, rname, sizeof rname);
            if ((r = vf_pane_find(&a->pane[PANE_REMOTE], rname)) != 0 && !r->is_parent) {
                if (!replace++) {
                    text_add(a, "These files are on %s already and would be replaced:", a->host);
                    text_add(a, "");
                }
                text_add(a, "  %-40s (%s)", rname, e->name);
            }
        }
        if (replace) {
            text_add(a, "");
            text_add(a, "%d file%s to put in all; Y replaces them, any other key puts nothing.",
                     a->nops, a->nops == 1 ? "" : "s");
            snprintf(a->text_title, sizeof a->text_title, "Put into %s", a->pane[PANE_REMOTE].dir);
            a->mode = M_CONFIRM; a->prompt = P_ASK; a->ask = ASK_REPLACE;
            vf_strlcpy(a->prompt_label, "Replace them? (Y/N) [N]: ", sizeof a->prompt_label);
            vfp_flush_keys();
            return;
        }
    }
    run_transfer(a);
    if (skipped && !a->msg_err) {
        char m[256];
        vf_strlcpy(m, a->msg, sizeof m);
        vf_set_msg(a, 0, "%s; %d director%s left out", m, skipped, skipped == 1 ? "y" : "ies");
    }
}

/* ---------- delete, rename, make directory ---------- */

static void delete_start(vf_app *a)
{
    vf_pane *p = &a->pane[a->act];
    int skipped, i;
    if (a->act == PANE_REMOTE && !a->rem) return;
    collect(a, a->act, 1, &skipped);
    if (!a->nops) { vf_set_msg(a, 1, "Nothing to delete here"); return; }
    text_free(a);
    snprintf(a->text_title, sizeof a->text_title, "Delete from %s%s",
             a->act == PANE_LOCAL ? "" : "the server: ", p->dir);
    if (a->nops == 1) text_add(a, "This will be deleted:");
    else text_add(a, "These %d entries will be deleted:", a->nops);
    text_add(a, "");
    for (i = 0; i < a->nops; i++) {
        const vf_entry *e = &p->e[a->ops[i]];
        text_add(a, "  %s%s", e->name, e->is_dir ? "   (directory: only if it is empty)" : "");
    }
    text_add(a, "");
    text_add(a, a->act == PANE_LOCAL ? "Each named version only; other versions stay."
                                     : "There is no undo on the server.");
    a->mode = M_CONFIRM; a->prompt = P_ASK; a->ask = ASK_DELETE;
    vf_strlcpy(a->prompt_label, "Delete them? (Y/N) [N]: ", sizeof a->prompt_label);
    vfp_flush_keys();
}

static void run_delete(vf_app *a)
{
    vf_pane *p = &a->pane[a->ops_pane];
    int i, ok = 0, failed = 0;
    char path[VF_SPEC_MAX], first[400], m[200];
    unsigned int st;
    first[0] = '\0';
    for (i = 0; i < a->nops; i++) {
        const vf_entry *e = &p->e[a->ops[i]];
        if (a->ops_pane == PANE_LOCAL) {
            vf_lfile(p->dir, e->name, path, sizeof path);
            st = vfp_delete(path);
            if (!(st & 1)) status_text(st, m, sizeof m);
        } else {
            vf_rjoin(p->dir, e->name, path, sizeof path);
            st = e->is_dir ? vfr_rmdir(a->rem, path) : vfr_unlink(a->rem, path);
            if (!(st & 1)) vf_strlcpy(m, vfr_error(a->rem), sizeof m);
        }
        if (st & 1) { ok++; logf_("DELETE %s", path); }
        else {
            failed++;
            logf_("DELETE %s FAILED: %s", path, m);
            if (!first[0]) snprintf(first, sizeof first, "%s: %s", e->name, m);
        }
    }
    reread(a, a->ops_pane);
    if (failed) vf_set_msg(a, 1, "%d deleted, %d not: %s", ok, failed, first);
    else vf_set_msg(a, 0, "%d deleted", ok);
}

static void rename_done(vf_app *a, const char *to)
{
    vf_pane *p = &a->pane[a->act];
    vf_entry *e = vf_pane_cur(p);
    char from[VF_SPEC_MAX], dest[VF_SPEC_MAX], m[200];
    unsigned int st;
    if (!e || e->is_parent || !to[0] || !strcmp(to, e->name)) return;
    if (a->act == PANE_LOCAL) {
        vf_lfile(p->dir, e->name, from, sizeof from);
        if (strchr(to, ':') || strchr(to, '[')) vf_strlcpy(dest, to, sizeof dest);
        else vf_lfile(p->dir, to, dest, sizeof dest);
        st = vfp_rename(from, dest);
        if (!(st & 1)) status_text(st, m, sizeof m);
    } else {
        vf_rjoin(p->dir, e->name, from, sizeof from);
        vf_rjoin(p->dir, to, dest, sizeof dest);
        st = vfr_rename(a->rem, from, dest);
        if (!(st & 1)) vf_strlcpy(m, vfr_error(a->rem), sizeof m);
    }
    if (!(st & 1)) { vf_set_msg(a, 1, "Not renamed: %s", m); logf_("RENAME %s -> %s FAILED: %s", from, dest, m); return; }
    logf_("RENAME %s -> %s", from, dest);
    reread(a, a->act);
    vf_pane_goto(p, to);
    vf_set_msg(a, 0, "Renamed to %s", to);
}

static void mkdir_done(vf_app *a, const char *name)
{
    vf_pane *p = &a->pane[a->act];
    char path[VF_SPEC_MAX], m[200], shown[300];
    unsigned int st;
    if (!name[0]) return;
    if (a->act == PANE_LOCAL) {
        vf_lsubdir(p->dir, name, path, sizeof path);
        st = vfp_mkdir(path);
        if (!(st & 1)) status_text(st, m, sizeof m);
        snprintf(shown, sizeof shown, "%s.DIR;1", name);
        vf_upper(shown);
    } else {
        vf_rjoin(p->dir, name, path, sizeof path);
        st = vfr_mkdir(a->rem, path);
        if (!(st & 1)) vf_strlcpy(m, vfr_error(a->rem), sizeof m);
        vf_strlcpy(shown, name, sizeof shown);
    }
    if (!(st & 1)) { vf_set_msg(a, 1, "Not made: %s", m); return; }
    logf_("MKDIR %s", path);
    reread(a, a->act);
    vf_pane_goto(p, shown);
    vf_set_msg(a, 0, "Made %s", path);
}

/* ---------- moving around ---------- */

static void enter(vf_app *a)
{
    vf_pane *p = &a->pane[a->act];
    vf_entry *e = vf_pane_cur(p);
    char dir[VF_SPEC_MAX], was[256];
    unsigned int st;
    if (!e) return;
    was[0] = '\0';
    if (e->is_parent) {
        /* coming back up: the cursor goes to the directory we left */
        if (a->act == PANE_LOCAL) {
            const char *lb = strrchr(p->dir, '.'), *rb = strrchr(p->dir, ']');
            if (!lb || lb > rb) lb = strchr(p->dir, '[');
            if (lb && rb && rb > lb) snprintf(was, sizeof was, "%.*s.DIR;1", (int) (rb - lb - 1), lb + 1);
            vf_lparent(p->dir, dir, sizeof dir);
        } else {
            vf_strlcpy(was, vf_rbase(p->dir), sizeof was);
            vf_rparent(p->dir, dir, sizeof dir);
        }
    } else if (e->is_dir || (e->is_link && a->act == PANE_REMOTE)) {
        char name[256];
        if (a->act == PANE_LOCAL) { vf_ldirname(e->name, name, sizeof name); vf_lsubdir(p->dir, name, dir, sizeof dir); }
        else vf_rjoin(p->dir, e->name, dir, sizeof dir);
    } else return;
    {
        char old[VF_SPEC_MAX];
        vf_strlcpy(old, p->dir, sizeof old);
        vf_strlcpy(p->dir, dir, VF_SPEC_MAX);
        p->cur = p->top = 0;
        st = a->act == PANE_LOCAL ? read_local(a) : read_remote(a);
        if (!(st & 1)) {
            char m[200];
            if (a->act == PANE_LOCAL) status_text(st, m, sizeof m);
            else vf_strlcpy(m, vfr_error(a->rem), sizeof m);
            if (e->is_link && !e->is_dir && a->act == PANE_REMOTE) vf_set_msg(a, 1, "%s is not a directory", e->name);
            else vf_set_msg(a, 1, "%s: %s", dir, m);
            vf_strlcpy(p->dir, old, VF_SPEC_MAX);
            reread(a, a->act);
            return;
        }
    }
    if (was[0]) vf_pane_goto(p, was);
}

static void jump(vf_app *a, const char *to)
{
    vf_pane *p = &a->pane[a->act];
    char dir[VF_SPEC_MAX], old[VF_SPEC_MAX];
    unsigned int st;
    int ods5;
    if (!to[0]) return;
    vf_strlcpy(old, p->dir, sizeof old);
    if (a->act == PANE_LOCAL) {
        st = vfp_locate(to, dir, sizeof dir, &ods5);
        if (!(st & 1)) { char m[200]; status_text(st, m, sizeof m); vf_set_msg(a, 1, "%s: %s", to, m); return; }
        a->ods5 = ods5;
    } else {
        char path[VF_SPEC_MAX];
        vf_rjoin(p->dir, to, path, sizeof path);
        st = vfr_realpath(a->rem, path, dir, sizeof dir);
        if (!(st & 1)) { vf_set_msg(a, 1, "%s: %s", to, vfr_error(a->rem)); return; }
    }
    vf_strlcpy(p->dir, dir, VF_SPEC_MAX);
    p->cur = p->top = 0;
    st = a->act == PANE_LOCAL ? read_local(a) : read_remote(a);
    if (!(st & 1)) {
        vf_set_msg(a, 1, "%s: cannot be read", dir);
        vf_strlcpy(p->dir, old, VF_SPEC_MAX);
        reread(a, a->act);
    }
}

/* ---------- view and info ---------- */

static unsigned int view_rec(void *ctx, const char *rec, int len)
{
    vf_app *a = (vf_app *) ctx;
    if (a->ntext < 100000) text_addn(a, rec, len > 1000 ? 1000 : len);
    return 1;
}

static void view(vf_app *a)
{
    vf_pane *p = &a->pane[a->act];
    vf_entry *e = vf_pane_cur(p);
    static char buf[CHUNK];
    unsigned int st;
    int got, binary = 0;
    if (!e || e->is_parent || e->is_dir) return;
    text_free(a);
    if (a->act == PANE_LOCAL) {
        char spec[VF_SPEC_MAX];
        vfp_file *f;
        unsigned long long est;
        int text = -1;
        vf_lfile(p->dir, e->name, spec, sizeof spec);
        st = vfp_open_read(spec, &text, &est, &f);
        if (!(st & 1)) { char m[200]; status_text(st, m, sizeof m); vf_set_msg(a, 1, "%s: %s", e->name, m); return; }
        if (!text) { vfp_close(f, 1); vf_set_msg(a, 1, "%s is not a text file (its records are not variable with carriage control)", e->name); return; }
        while (((st = vfp_read(f, buf, sizeof buf, &got)) & 1) && a->ntext < 100000) text_addn(a, buf, got > 1000 ? 1000 : got);
        vfp_close(f, 1);
        snprintf(a->text_title, sizeof a->text_title, "%s%s", p->dir, e->name);
    } else {
        char path[VF_SPEC_MAX];
        vfr_file *f;
        unsigned long long total = 0;
        vf_rjoin(p->dir, e->name, path, sizeof path);
        st = vfr_open_read(a->rem, path, &f);
        if (!(st & 1)) { vf_set_msg(a, 1, "%s: %s", e->name, vfr_error(a->rem)); return; }
        vf_lines_init(&a->lines);
        while (((st = vfr_read(f, buf, sizeof buf, &got)) & 1) && got > 0 && total < 4000000ULL) {
            if (memchr(buf, 0, (size_t) got)) { binary = 1; break; }
            vf_lines_feed(&a->lines, buf, got, view_rec, a);
            total += (unsigned long long) got;
        }
        if (!binary) vf_lines_end(&a->lines, view_rec, a);
        vfr_close(f);
        if (binary) { text_free(a); vf_set_msg(a, 1, "%s holds binary data; Get it to look at it", e->name); return; }
        snprintf(a->text_title, sizeof a->text_title, "%s:%s", a->host, path);
    }
    a->back_mode = M_MAIN;
    a->mode = M_VIEW;
}

static void info(vf_app *a)
{
    vf_pane *p = &a->pane[a->act];
    vf_entry *e = vf_pane_cur(p);
    char b[64], when[40];
    if (!e || e->is_parent) return;
    text_free(a);
    vf_strlcpy(a->text_title, e->name, sizeof a->text_title);
    if (a->act == PANE_LOCAL) {
        text_add(a, "File        %s%s", p->dir, e->name);
        text_add(a, "Size        %u blocks", e->blocks);
        text_add(a, "Records     %s", e->is_dir ? "directory" : e->is_text ? "text (variable or VFC, carriage control)" : "binary (fixed, stream or undefined)");
        text_add(a, "Protection  %s", e->prot);
        text_add(a, "Owner       %s", e->owner);
    } else {
        char path[VF_SPEC_MAX];
        vf_rjoin(p->dir, e->name, path, sizeof path);
        text_add(a, "Path        %s:%s", a->host, path);
        if (!e->has_size) text_add(a, "Size        unknown");
        else if (e->size < 100000ULL) text_add(a, "Size        %llu bytes", e->size);
        else text_add(a, "Size        %llu bytes (%s)", e->size, vf_fmt_bytes(e->size, when, sizeof when));
        text_add(a, "Type        %s", e->is_link ? "symbolic link" : e->is_dir ? "directory" : "file");
        text_add(a, "Permissions %s (%04o)", vf_fmt_perms(e->perms, e->is_dir, e->is_link, b, sizeof b), e->perms & 07777);
        text_add(a, "Owner       %s", e->owner);
        text_add(a, "Get as      %s", e->is_dir ? "-" : want_text(a, e->name) ? "text (variable-length records)"
                                                    : e->size % 512 == 0 ? "binary, fixed 512-byte records"
                                                                         : "binary, undefined records");
    }
    if (e->has_time) text_add(a, "Modified    %s", vf_fmt_when(&e->mtime, when, sizeof when));
    a->back_mode = M_MAIN;
    a->mode = M_INFO;
}

static void help_build(vf_app *a)
{
    text_free(a);
    vf_strlcpy(a->text_title, "VFTP " VF_VERSION " help", sizeof a->text_title);
    text_add(a, "MOVING");
    text_add(a, "  Up/Down, Prev/Next Screen    move; GOLD Up/Down: first/last");
    text_add(a, "  Tab                          the other pane (local / remote)");
    text_add(a, "  Return, Select, Right        open the directory; on a file: View");
    text_add(a, "  Left, -                      up one directory");
    text_add(a, "  J                            jump to a directory (a VMS spec or a path)");
    text_add(a, "CONNECTION");
    text_add(a, "  O                            open a connection (SFTP); GOLD O closes it");
    text_add(a, "FILES");
    text_add(a, "  G  Get                       download the tagged (or current) remote files");
    text_add(a, "  P  Put                       upload the tagged (or current) local files");
    text_add(a, "  C, F5  Copy                  to the other pane (Get or Put)");
    text_add(a, "  D, Remove  Delete            asks first; local: the versions named only");
    text_add(a, "  R  Rename    M  Make directory    V  View    A  Attributes");
    text_add(a, "  T, Space  Tag/untag          GOLD T all, GOLD U none, * by name (wildcard)");
    text_add(a, "  X  Transfer mode             AUTO, ASCII, BINARY");
    text_add(a, "  S  Sort   F, Find  Filter    GOLD V every version / highest only (local)");
    text_add(a, "  GOLD W, Ctrl/W               read both panes again; redraw");
    text_add(a, "  F10, Ctrl/Z                  exit (a transfer: F10, Ctrl/Z, Ctrl/C cancel)");
    text_add(a, "TRANSFER MODE (X)");
    text_add(a, "  AUTO  Put: text if the file has variable or VFC records with carriage");
    text_add(a, "        control, each record sent as a line. Get: text if the name's type");
    text_add(a, "        is in VFTP$TEXT_TYPES (or the built-in list: .TXT .C .COM .HTML ...).");
    text_add(a, "  Text arrives as variable-length records; binary as fixed 512-byte records");
    text_add(a, "  when its size is a multiple of 512, otherwise undefined records.");
    text_add(a, "  A download is always a new version: nothing local is ever replaced.");
    text_add(a, "SAFETY");
    text_add(a, "  Unknown servers show their key's fingerprint and ask (known_hosts as ssh).");
    text_add(a, "  A changed server key is refused. Passwords are never stored.");
    text_add(a, "  Each operation is logged in VFTP$LOG (default SYS$LOGIN:VFTP.LOG).");
    a->back_mode = a->mode == M_FORM ? M_FORM : M_MAIN;
    a->mode = M_HELP;
}

/* ---------- prompts and keys ---------- */

static void start_prompt(vf_app *a, int kind, const char *label, const char *initial)
{
    a->prompt = kind;
    vf_strlcpy(a->prompt_label, label, sizeof a->prompt_label);
    vf_edit_init(&a->pedit, initial);
}

static void ask_answer(vf_app *a, int key)
{
    int yes = key == 'Y' || key == 'y', ask = a->ask;
    a->prompt = P_NONE; a->ask = 0;
    a->mode = M_MAIN;
    text_free(a);
    switch (ask) {
    case ASK_HOSTKEY:
        if (!yes) { disconnect(a, "host key not trusted"); form_fail(a, FF_HOST, "Not connected: the server was not trusted"); return; }
        if (!(vfr_trust(a->rem) & 1)) vf_set_msg(a, 1, "Could not add the key to %s: %s", a->hk.file, vfr_error(a->rem));
        else logf_("HOSTKEY ADDED %s:%d %s %s to %s", a->host, a->port, a->hk.type, a->hk.fingerprint, a->hk.file);
        login(a);
        return;
    case ASK_DELETE:
        if (yes) run_delete(a); else vf_set_msg(a, 0, "Not deleted: nothing was changed");
        return;
    case ASK_REPLACE:
        if (yes) run_transfer(a); else vf_set_msg(a, 0, "Nothing was put");
        return;
    default: return;
    }
}

static void prompt_key(vf_app *a, int key)
{
    int r, kind = a->prompt;
    char text[VF_SPEC_MAX];
    if (kind == P_ASK) { ask_answer(a, key); return; }
    r = vf_edit_key(&a->pedit, key);
    if (r == VF_EDIT_MORE) return;
    a->prompt = P_NONE;
    if (r == VF_EDIT_CANCEL) return;
    vf_strlcpy(text, a->pedit.buf, sizeof text);
    vf_trim(text);
    switch (kind) {
    case P_FILTER: {
        vf_pane *p = &a->pane[a->act];
        int files, dirs;
        unsigned long long by;
        unsigned int bl;
        vf_strlcpy(p->filter, text[0] ? text : "*", sizeof p->filter);
        p->cur = p->top = 0;
        vf_pane_rebuild(p);
        vf_pane_totals(p, &files, &dirs, &by, &bl);
        vf_set_msg(a, 0, "%d file%s match %s", files, files == 1 ? "" : "s", p->filter);
        return;
    }
    case P_TAG: {
        int n = vf_pane_tag_match(&a->pane[a->act], text[0] ? text : "*", 1);
        vf_set_msg(a, 0, "%d file%s tagged", n, n == 1 ? "" : "s");
        return;
    }
    case P_RENAME: rename_done(a, text); return;
    case P_MKDIR: mkdir_done(a, text); return;
    case P_JUMP: jump(a, text); return;
    default: return;
    }
}

static void form_key(vf_app *a, int key)
{
    switch (key) {
    case VK_UP: form_store(a); if (a->fcur > 0) a->fcur--; form_load(a); return;
    case VK_DOWN: case VK_TAB: form_store(a); if (a->fcur < FF_COUNT - 1) a->fcur++; form_load(a); return;
    case VK_RETURN:
        form_store(a);
        if (a->fcur < FF_COUNT - 1) { a->fcur++; form_load(a); }
        else connect_now(a);
        return;
    case VK_DO: case VK_ENTER: case VK_PF4: connect_now(a); return;
    case VK_F10: case VK_CTRL_Z:
        form_store(a);
        wipe(a->fv[FF_PASSWORD], VF_FIELD);
        wipe(a->fv[FF_PHRASE], VF_FIELD);
        a->mode = M_MAIN;
        a->form_err[0] = '\0';
        return;
    case VK_HELP: case VK_PF2: form_store(a); help_build(a); return;
    default: vf_edit_key(&a->fedit, key); a->form_err[0] = '\0';
    }
}

static void main_key(vf_app *a, int key)
{
    vf_pane *p = &a->pane[a->act];
    vf_entry *e = vf_pane_cur(p);
    int page = a->rows - 9 > 3 ? a->rows - 9 : 3;
    if (a->gold) {
        a->gold = 0;
        switch (key) {
        case VK_UP: p->cur = 0; return;
        case VK_DOWN: p->cur = p->nvis ? p->nvis - 1 : 0; return;
        case 'o': case 'O':
            if (a->rem) { disconnect(a, "closed by the user"); vf_set_msg(a, 0, "Disconnected from %s", a->host); }
            return;
        case 't': case 'T': { int n = vf_pane_tag_match(p, "*", 1); vf_set_msg(a, 0, "%d tagged", n); return; }
        case 'u': case 'U': vf_pane_tag_match(p, "*", 0); vf_set_msg(a, 0, "Nothing tagged"); return;
        case 'v': case 'V':
            a->all_versions = !a->all_versions;
            read_local(a);
            vf_set_msg(a, 0, a->all_versions ? "Local pane: every version" : "Local pane: highest versions only");
            return;
        case 'w': case 'W': read_local(a); read_remote(a); vf_set_msg(a, 0, "Read again"); return;
        default: return;
        }
    }
    switch (key) {
    case VK_PF1: a->gold = 1; return;
    case VK_TAB:
        if (a->rem || a->act == PANE_REMOTE) a->act = !a->act;
        else vf_set_msg(a, 0, "Not connected: O opens a connection");
        return;
    case VK_UP: if (p->cur > 0) p->cur--; return;
    case VK_DOWN: if (p->cur < p->nvis - 1) p->cur++; return;
    case VK_PREV: p->cur -= page; if (p->cur < 0) p->cur = 0; return;
    case VK_NEXT: p->cur += page; if (p->cur >= p->nvis) p->cur = p->nvis ? p->nvis - 1 : 0; return;
    case VK_RETURN: case VK_SELECT: case VK_RIGHT:
        if (e && !e->is_dir && !e->is_parent && !e->is_link && key != VK_RIGHT) view(a);
        else enter(a);
        return;
    case VK_LEFT: case '-':
        if (p->nvis && p->e[p->vis[0]].is_parent) { p->cur = 0; enter(a); }
        return;
    case ' ': case 't': case 'T':
        if (e && !e->is_parent && !e->is_dir) e->tagged = !e->tagged;
        if (p->cur < p->nvis - 1) p->cur++;
        return;
    case '*': start_prompt(a, P_TAG, "Tag the files named: ", "*"); return;
    case 'o': case 'O': form_open(a); return;
    case 'g': case 'G': transfer(a, 1); return;
    case 'p': case 'P': transfer(a, 0); return;
    case 'c': case 'C': transfer(a, a->act == PANE_REMOTE); return;
    case 'd': case 'D': case VK_REMOVE: delete_start(a); return;
    case 'r': case 'R':
        if (e && !e->is_parent && (a->act == PANE_LOCAL || a->rem)) start_prompt(a, P_RENAME, "New name: ", e->name);
        return;
    case 'm': case 'M':
        if (a->act == PANE_LOCAL || a->rem) start_prompt(a, P_MKDIR, "New directory: ", "");
        return;
    case 'j': case 'J':
        if (a->act == PANE_LOCAL || a->rem) start_prompt(a, P_JUMP, "Go to directory: ", p->dir);
        return;
    case 'v': case 'V': view(a); return;
    case 'a': case 'A': case 'i': case 'I': info(a); return;
    case 'x': case 'X':
        a->xmode = (a->xmode + 1) % 3;
        vf_set_msg(a, 0, "Transfer mode %s", vf_xmode_name(a->xmode));
        return;
    case 's': case 'S': {
        static const char *names[] = { "name", "type", "size", "date" };
        p->sort = (p->sort + 1) % VF_SORTS;
        vf_pane_rebuild(p);
        vf_set_msg(a, 0, "Sorted by %s", names[p->sort]);
        return;
    }
    case 'f': case 'F': case VK_FIND: start_prompt(a, P_FILTER, "Show the files named: ", p->filter); return;
    case VK_HELP: case VK_PF2: case '?': help_build(a); return;
    case VK_F10: case VK_CTRL_Z: case 'q': case 'Q': a->quit = 1; return;
    default: return;
    }
}

static void text_key(vf_app *a, int key)
{
    int page = a->rows - 6;
    switch (key) {
    case VK_UP: if (a->ttop > 0) a->ttop--; return;
    case VK_DOWN: if (a->ttop < a->ntext - 1) a->ttop++; return;
    case VK_PREV: a->ttop -= page; if (a->ttop < 0) a->ttop = 0; return;
    case VK_NEXT: if (a->ttop + page < a->ntext) a->ttop += page; return;
    default:
        a->mode = a->back_mode;
        if (a->mode == M_FORM) form_load(a);
    }
}

void vf_app_key(vf_app *a, int key)
{
    if (key == VK_CTRL_W) { vfp_repaint(); return; }
    if (key != VK_TIMEOUT && a->prompt == P_NONE && a->mode != M_FORM) a->msg[0] = '\0';
    if (a->prompt != P_NONE) {
        if (a->mode == M_CONFIRM && a->prompt == P_ASK &&
            (key == VK_UP || key == VK_DOWN || key == VK_PREV || key == VK_NEXT)) {
            text_key(a, key);
            a->mode = M_CONFIRM;
            return;
        }
        prompt_key(a, key);
        return;
    }
    switch (a->mode) {
    case M_MAIN: main_key(a, key); break;
    case M_FORM: form_key(a, key); break;
    default: text_key(a, key); break;
    }
}

void vf_app_run(vf_app *a)
{
    char b[256];
    while (!a->quit) {
        int key;
        vf_app_draw(a);
        vfp_draw(&a->scr);
        key = vfp_key(30);
        if (vfp_broadcast(b, sizeof b)) vf_set_msg(a, 0, "%s", b);
        if (a->rem && vfp_clock() - a->last_alive > 30.0) {
            vfr_keepalive(a->rem);
            a->last_alive = vfp_clock();
        }
        if (key == VK_TIMEOUT) continue;
        vf_app_key(a, key);
    }
}
