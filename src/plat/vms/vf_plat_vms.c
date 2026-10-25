/* vf_plat_vms.c - vf_platform.h on OpenVMS (x86-64 and IA64).
   SMG$ owns the keyboard and traps broadcasts; the screen is written through a
   channel to TT: by vf_ansi so that it can be in colour (VTREE D11). Local files
   go through RMS, as in VTREE: $SEARCH with NAML for listings, ACP IO$_ACCESS for
   attributes, record and block I/O for transfers (D6). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <rms.h>
#include <starlet.h>
#include <descrip.h>
#include <iodef.h>
#include <ssdef.h>
#include <fibdef.h>
#include <atrdef.h>
#include <jpidef.h>
#include <syidef.h>
#include <dvidef.h>
#include <smgdef.h>
#include <smgmsg.h>
#include <smg$routines.h>
#include <lib$routines.h>
#include "vf_platform.h"
#include "vf_util.h"
#include "vf_ansi.h"

typedef struct { unsigned short buflen, code; void *buf; unsigned short *retlen; } ile3;
typedef struct { unsigned short size, type; void *addr; } atr_item;

static void ile(ile3 *it, unsigned short code, void *buf, unsigned short len, unsigned short *rl)
{
    it->buflen = len; it->code = code; it->buf = buf; it->retlen = rl;
}

static void dsc(struct dsc$descriptor_s *d, const char *s, unsigned short len)
{
    d->dsc$w_length = len;
    d->dsc$b_dtype = DSC$K_DTYPE_T;
    d->dsc$b_class = DSC$K_CLASS_S;
    d->dsc$a_pointer = (char *) s;
}

static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ') s[--n] = '\0';
}

static void strip_underscore(char *s) { if (s[0] == '_') memmove(s, s + 1, strlen(s)); }

/* ---------- session and terminal ---------- */

static unsigned int pb, kb;
static unsigned short ttchan;
static int scr_rows, scr_cols, started, colour = 1;
static vf_cell *shown;
static int shown_rows, shown_cols;

static void tt_write(void *ctx, const char *bytes, int len)
{
    unsigned short iosb[4];
    (void) ctx;
    while (len > 0) {
        int n = len > 2048 ? 2048 : len;
        sys$qiow(0, ttchan, IO$_WRITEVBLK | IO$M_NOFORMAT, (void *) iosb, 0, 0,
                 (void *) bytes, n, 0, 0, 0, 0);
        bytes += n; len -= n;
    }
}

static struct { unsigned int link; void (*handler)(void); unsigned int argc; unsigned int *status;
                unsigned int st; } exit_block;

static void on_exit(void) { vfp_stop(); }

unsigned int vfp_start(vfp_session *s)
{
    unsigned int st, one = 1;
    char user[13] = "", node[16] = "";
    unsigned short ul = 0, nl = 0;
    ile3 it[2];
    int rows, cols;
    struct dsc$descriptor_s ttd;
    const char *mode;

    memset(s, 0, sizeof *s);
    ile(&it[0], JPI$_USERNAME, user, 12, &ul);
    memset(&it[1], 0, sizeof it[1]);
    sys$getjpiw(0, 0, 0, it, 0, 0, 0);
    user[ul] = '\0'; rtrim(user);
    vf_strlcpy(s->user, user, sizeof s->user);
    ile(&it[0], SYI$_NODENAME, node, 15, &nl);
    memset(&it[1], 0, sizeof it[1]);
    sys$getsyiw(0, 0, 0, it, 0, 0, 0);
    node[nl] = '\0';
    vf_strlcpy(s->node, node, sizeof s->node);

    dsc(&ttd, "TT:", 3);
    st = sys$assign(&ttd, &ttchan, 0, 0);
    if (!(st & 1)) return st;
    st = smg$create_pasteboard(&pb, 0, &rows, &cols, 0, 0, 0);
    if (!(st & 1)) { sys$dassgn(ttchan); return st; }
    st = smg$create_virtual_keyboard(&kb, 0, 0, 0, 0);
    if (!(st & 1)) { smg$delete_pasteboard(&pb, &one); sys$dassgn(ttchan); return st; }
    smg$set_broadcast_trapping(&pb, 0, 0);
    mode = getenv("VFTP$DISPLAY");                 /* MONO for a terminal without colour */
    colour = !(mode && (mode[0] == 'M' || mode[0] == 'm'));
    scr_rows = rows; scr_cols = cols;
    started = 1;
    vf_ansi_begin(colour, tt_write, 0);

    exit_block.handler = on_exit;
    exit_block.argc = 1;
    exit_block.status = &exit_block.st;
    sys$dclexh((void *) &exit_block);
    return SS$_NORMAL;
}

void vfp_stop(void)
{
    unsigned int one = 1;
    if (!started) return;
    started = 0;
    vf_ansi_end(tt_write, 0);
    smg$disable_broadcast_trapping(&pb);
    smg$delete_pasteboard(&pb, &one);
    smg$delete_virtual_keyboard(&kb);
    sys$dassgn(ttchan);
    free(shown); shown = 0;
}

void vfp_screen_size(int *rows, int *cols) { *rows = scr_rows; *cols = scr_cols; }

void vfp_draw(const vf_screen *s)
{
    if (!started) return;
    if (!shown || shown_rows != s->rows || shown_cols != s->cols) {
        free(shown);
        shown = (vf_cell *) malloc((size_t) s->rows * (size_t) s->cols * sizeof *shown);
        shown_rows = s->rows; shown_cols = s->cols;
        vf_ansi_render(s, 0, colour, tt_write, 0);
        if (shown) memcpy(shown, s->cell, (size_t) s->rows * (size_t) s->cols * sizeof *shown);
        return;
    }
    vf_ansi_render(s, shown, colour, tt_write, 0);
}

void vfp_repaint(void) { free(shown); shown = 0; }

int vfp_key(int timeout)
{
    unsigned short term = 0;
    unsigned int st;
    if (!started) return VK_TIMEOUT;
    st = smg$read_keystroke(&kb, &term, 0, &timeout, 0, 0, 0);
    if (st == SS$_TIMEOUT) return VK_TIMEOUT;
    if (st == SMG$_EOF) return VK_CTRL_Z;
    if (!(st & 1)) return VK_TIMEOUT;
    switch (term) {
    case SMG$K_TRM_UP: return VK_UP;
    case SMG$K_TRM_DOWN: return VK_DOWN;
    case SMG$K_TRM_LEFT: return VK_LEFT;
    case SMG$K_TRM_RIGHT: return VK_RIGHT;
    case SMG$K_TRM_PF1: return VK_PF1;
    case SMG$K_TRM_PF2: return VK_PF2;
    case SMG$K_TRM_PF3: return VK_PF3;
    case SMG$K_TRM_PF4: return VK_PF4;
    case SMG$K_TRM_FIND: return VK_FIND;
    case SMG$K_TRM_INSERT_HERE: return VK_INSERT;
    case SMG$K_TRM_REMOVE: return VK_REMOVE;
    case SMG$K_TRM_SELECT: return VK_SELECT;
    case SMG$K_TRM_PREV_SCREEN: return VK_PREV;
    case SMG$K_TRM_NEXT_SCREEN: return VK_NEXT;
    case SMG$K_TRM_HELP: return VK_HELP;
    case SMG$K_TRM_DO: return VK_DO;
    case SMG$K_TRM_F10: return VK_F10;
    case SMG$K_TRM_ENTER: return VK_ENTER;
    case SMG$K_TRM_MINUS: return VK_KP_MINUS;
    case SMG$K_TRM_COMMA: return VK_KP_COMMA;
    case SMG$K_TRM_PERIOD: return VK_KP_PERIOD;
    default: break;
    }
    if (term >= SMG$K_TRM_KP0 && term <= SMG$K_TRM_KP9) return VK_KP0 + (term - SMG$K_TRM_KP0);
    if (term < 256) return term;
    return VK_OTHER;
}

int vfp_broadcast(char *buf, int size)
{
    struct dsc$descriptor_s d;
    unsigned short len = 0;
    unsigned int st;
    int i;
    if (!started) return 0;
    dsc(&d, buf, (unsigned short) (size - 1));
    st = smg$get_broadcast_message(&pb, &d, &len, 0);
    if (st != SS$_NORMAL || len == 0) return 0;
    buf[len] = '\0';
    for (i = 0; i < len; i++) if ((unsigned char) buf[i] < 32) buf[i] = ' ';
    return 1;
}

void vfp_flush_keys(void)
{
    int n = 0;
    while (started && n++ < 512 && vfp_key(0) != VK_TIMEOUT) ;
}

void vfp_now(vf_when *w)
{
    time_t t = time(0);
    struct tm *tm = localtime(&t);
    w->y = tm->tm_year + 1900; w->m = tm->tm_mon + 1; w->d = tm->tm_mday;
    w->hh = tm->tm_hour; w->mm = tm->tm_min;
}

double vfp_clock(void)
{
    unsigned int t[2];                     /* 100 ns units since 1858 */
    sys$gettim((void *) t);
    return ((double) t[1] * 4294967296.0 + (double) t[0]) / 1e7;
}

void vfp_message(unsigned int st, char *buf, int size)
{
    struct dsc$descriptor_s d;
    unsigned short len = 0;
    if (st == VFP_STOPPED) { vf_strlcpy(buf, "stopped", (size_t) size); return; }
    if (st == VFP_EOF) { vf_strlcpy(buf, "end of file", (size_t) size); return; }
    if (st == 0x1004u) { vf_strlcpy(buf, "not one exact file version: refused", (size_t) size); return; }
    dsc(&d, buf, (unsigned short) (size - 1));
    if (!(sys$getmsg(st, &len, &d, 15, 0) & 1)) { snprintf(buf, (size_t) size, "%%X%08X", st); return; }
    buf[len] = '\0';
}

/* ---------- names, owners, times ---------- */

static void naml_setup(struct FAB *fab, struct NAML *naml, const char *spec, char *esa, char *rsa)
{
    *fab = cc$rms_fab;
    *naml = cc$rms_naml;
    fab->fab$l_fna = (char *) -1;
    fab->fab$b_fns = 0;
    fab->fab$l_naml = naml;
    naml->naml$l_long_filename = (char *) spec;
    naml->naml$l_long_filename_size = (unsigned int) strlen(spec);
    naml->naml$l_long_expand = esa;
    naml->naml$l_long_expand_alloc = NAML$C_MAXRSS;
    if (rsa) {
        naml->naml$l_long_result = rsa;
        naml->naml$l_long_result_alloc = NAML$C_MAXRSS;
    }
}

static void uic_name(unsigned int uic, char *buf, int size)
{
    static unsigned int cache_uic[16]; static char cache_name[16][40]; static int ncache, next;
    struct dsc$descriptor_s d;
    char name[64];
    unsigned short len = 0;
    int i;
    for (i = 0; i < ncache; i++)
        if (cache_uic[i] == uic) { vf_strlcpy(buf, cache_name[i], (size_t) size); return; }
    dsc(&d, name, sizeof name - 1);
    if (sys$idtoasc(uic, &len, &d, 0, 0, 0) & 1) name[len] = '\0';
    else snprintf(name, sizeof name, "%o,%o", uic >> 16, uic & 0xFFFF);
    i = ncache < 16 ? ncache++ : (next++ % 16);
    cache_uic[i] = uic;
    vf_strlcpy(cache_name[i], name, sizeof cache_name[i]);
    vf_strlcpy(buf, name, (size_t) size);
}

static void fmt_prot(unsigned short prot, char *buf, size_t size)
{
    char out[48];
    int c, o = 0;
    out[o++] = '(';
    for (c = 0; c < 4; c++) {
        unsigned int deny = (prot >> (4 * c)) & 0xF;
        if (c) out[o++] = ',';
        if (!(deny & 1)) out[o++] = 'R';
        if (!(deny & 2)) out[o++] = 'W';
        if (!(deny & 4)) out[o++] = 'E';
        if (!(deny & 8)) out[o++] = 'D';
    }
    out[o++] = ')';
    out[o] = '\0';
    vf_strlcpy(buf, out, size);
}

static void vms_when(const unsigned int t[2], vf_when *w)
{
    unsigned short n[7];
    memset(w, 0, sizeof *w);
    if (!t[0] && !t[1]) return;
    if (!(sys$numtim(n, (void *) t) & 1)) return;
    w->y = n[0]; w->m = n[1]; w->d = n[2]; w->hh = n[3]; w->mm = n[4];
}

/* "[A.][B]" -> "[A.B]", "<A>" -> "[A]", "[000000.A]" -> "[A]" (VTREE's normalising). */
static void dir_normalise(char *dir)
{
    char *p, *o;
    for (p = o = dir; *p; ) {
        if (*p == '^' && p[1]) { *o++ = *p++; *o++ = *p++; continue; }
        if (*p == '<') { *o++ = '['; p++; continue; }
        if (*p == '>') { *o++ = ']'; p++; continue; }
        if (p[0] == '.' && (p[1] == ']' || p[1] == '>') && (p[2] == '[' || p[2] == '<')) {
            *o++ = '.'; p += 3; continue;
        }
        *o++ = *p++;
    }
    *o = '\0';
    if (strncmp(dir, "[000000.", 8) == 0) memmove(dir + 1, dir + 8, strlen(dir + 8) + 1);
}

/* ---------- where am I ---------- */

static int dev_ods5(const char *dev)
{
    struct dsc$descriptor_s d;
    unsigned int acp = 0;
    unsigned short w, iosb[4];
    ile3 it[2];
    dsc(&d, dev, (unsigned short) strlen(dev));
    ile(&it[0], DVI$_ACPTYPE, &acp, 4, &w);
    memset(&it[1], 0, sizeof it[1]);
    if (!(sys$getdviw(0, 0, &d, it, (void *) iosb, 0, 0, 0, 0) & 1)) return 0;
    return acp == DVI$C_ACP_F11V5;
}

unsigned int vfp_locate(const char *spec, char *dir, int size, int *ods5)
{
    struct FAB fab;
    struct NAML naml;
    static char esa[NAML$C_MAXRSS + 1];
    char dev[256], d[VF_SPEC_MAX];
    unsigned int st;
    naml_setup(&fab, &naml, spec && *spec ? spec : "[]", esa, 0);
    naml.naml$b_nop |= NAML$M_NOCONCEAL;               /* the physical device (VTREE N6) */
    st = sys$parse(&fab, 0, 0);
    if (!(st & 1)) return st;
    snprintf(dev, sizeof dev, "%.*s", (int) naml.naml$l_long_dev_size, naml.naml$l_long_dev);
    strip_underscore(dev);
    snprintf(d, sizeof d, "%.*s", (int) naml.naml$l_long_dir_size, naml.naml$l_long_dir);
    dir_normalise(d);
    naml.naml$b_nop |= NAML$M_SYNCHK;
    sys$parse(&fab, 0, 0);
    *ods5 = dev_ods5(dev);
    snprintf(dir, (size_t) size, "%s%s", dev, d);
    return SS$_NORMAL;
}

/* ---------- listing ---------- */

static unsigned int read_attrs(unsigned short chan, const unsigned short fid[3], vf_entry *e,
                               unsigned int *uchar)
{
    struct fibdef fib;
    struct dsc$descriptor_s fibd;
    unsigned short iosb[4], uic_w[2] = { 0, 0 }, fpro = 0;
    unsigned char fat[ATR$S_RECATTR];
    unsigned int st, rdt[2] = { 0, 0 };
    atr_item atr[6];
    memset(&fib, 0, sizeof fib);
    memcpy(&fib.fib$w_fid, fid, 6);
    dsc(&fibd, (char *) &fib, sizeof fib);
    atr[0].size = 4; atr[0].type = ATR$C_UCHAR; atr[0].addr = uchar;
    atr[1].size = ATR$S_RECATTR; atr[1].type = ATR$C_RECATTR; atr[1].addr = fat;
    atr[2].size = 4; atr[2].type = ATR$C_UIC; atr[2].addr = uic_w;
    atr[3].size = 2; atr[3].type = ATR$C_FPRO; atr[3].addr = &fpro;
    atr[4].size = 8; atr[4].type = ATR$C_REVDATE; atr[4].addr = rdt;
    atr[5].size = 0; atr[5].type = 0; atr[5].addr = 0;
    st = sys$qiow(0, chan, IO$_ACCESS, (void *) iosb, 0, 0, &fibd, 0, 0, 0, atr, 0);
    if (st & 1) st = iosb[0];
    if (!(st & 1)) return st;
    {
        const unsigned short *w = (const unsigned short *) fat;
        unsigned int ef = ((unsigned int) w[4] << 16) | w[5], ffb = w[6];
        unsigned int rfm = fat[0] & 0x0F, rat = fat[1];
        unsigned int blocks = (ffb == 0 && ef > 0) ? ef - 1 : ef;   /* as DIRECTORY counts */
        e->blocks = blocks;
        e->size = ef > 0 ? (unsigned long long) (ef - 1) * 512 + ffb : 0;
        e->has_size = 1;
        /* variable or VFC records with carriage control: text (D6) */
        e->is_text = (rfm == FAB$C_VAR || rfm == FAB$C_VFC) && (rat & (FAB$M_CR | FAB$M_FTN | FAB$M_PRN));
    }
    {
        char name[48];
        uic_name(((unsigned int) uic_w[1] << 16) | uic_w[0], name, sizeof name);
        snprintf(e->owner, sizeof e->owner, "[%s]", name);
    }
    fmt_prot(fpro, e->prot, sizeof e->prot);
    vms_when(rdt, &e->mtime);
    e->has_time = e->mtime.y != 0;
    return st;
}

unsigned int vfp_list(const char *dir, int all_versions, vfp_entry_cb cb, void *ctx)
{
    struct FAB fab;
    struct NAML naml;
    static char esa[NAML$C_MAXRSS + 1], rsa[NAML$C_MAXRSS + 1];
    char spec[VF_SPEC_MAX + 16];
    unsigned short chan = 0;
    unsigned int st, ret = SS$_NORMAL;

    snprintf(spec, sizeof spec, "%s*.*;%s", dir, all_versions ? "*" : "0");
    naml_setup(&fab, &naml, spec, esa, rsa);
    st = sys$parse(&fab, 0, 0);
    if (!(st & 1)) return st;
    while ((st = sys$search(&fab, 0, 0)) & 1) {
        vf_entry e;
        unsigned int uchar = 0, ver;
        unsigned short fid[3];
        char type[256], v[8];
        unsigned int vl = naml.naml$l_long_ver_size;
        memset(&e, 0, sizeof e);
        if (vl > 7) vl = 7;
        memcpy(v, naml.naml$l_long_ver, vl);
        v[vl] = '\0';
        ver = (unsigned int) atoi(v[0] == ';' || v[0] == '.' ? v + 1 : v);
        snprintf(type, sizeof type, "%.*s", (int) naml.naml$l_long_type_size, naml.naml$l_long_type);
        snprintf(e.name, sizeof e.name, "%.*s%s;%u", (int) naml.naml$l_long_name_size,
                 naml.naml$l_long_name, type, ver);
        memcpy(fid, naml.naml$w_fid, 6);
        if (memcmp(fid, naml.naml$w_did, 6) == 0) continue;    /* [000000]000000.DIR */
        if (!chan) {
            struct dsc$descriptor_s dv;
            dsc(&dv, (char *) &naml.naml$t_dvi[1], naml.naml$t_dvi[0]);
            if (!((st = sys$assign(&dv, &chan, 0, 0)) & 1)) { ret = st; break; }
        }
        if (read_attrs(chan, fid, &e, &uchar) & 1)
            e.is_dir = !vf_strcasecmp(type, ".DIR") && ver == 1 && (uchar & (1u << 13));
        else
            e.is_dir = !vf_strcasecmp(type, ".DIR") && ver == 1;
        if (cb(ctx, &e)) { ret = VFP_STOPPED; break; }
    }
    if (ret == SS$_NORMAL && st != RMS$_NMF && st != RMS$_FNF) ret = st;
    if (st & 1 || ret == VFP_STOPPED) {
        naml.naml$b_nop |= NAML$M_SYNCHK;
        sys$parse(&fab, 0, 0);
    }
    if (chan) sys$dassgn(chan);
    return ret;
}

unsigned int vfp_mkdir(const char *dir)
{
    struct dsc$descriptor_s d;
    dsc(&d, dir, (unsigned short) strlen(dir));
    return lib$create_dir(&d, 0, 0, 0, 0, 0);
}

/* One exact version, no wildcards (VTREE's rule). */
static int exact_spec(const char *path)
{
    const char *semi = 0, *p;
    for (p = path; *p; p++) {
        if (*p == '^' && p[1]) { p++; continue; }
        if (*p == '*' || *p == '%' || *p == '"') return 0;
        if (*p == ';') semi = p;
        if (*p == '.' && p[1] == '.' && p[2] == '.') return 0;
    }
    if (!semi || !semi[1]) return 0;
    for (p = semi + 1; *p; p++) if (*p < '0' || *p > '9') return 0;
    return atoi(semi + 1) > 0;
}

static unsigned int erase(const char *spec)
{
    static char esa[NAML$C_MAXRSS + 1];
    struct FAB fab;
    struct NAML naml;
    naml_setup(&fab, &naml, spec, esa, 0);
    return sys$erase(&fab, 0, 0);
}

unsigned int vfp_delete(const char *spec)
{
    if (!exact_spec(spec)) return 0x1004u;
    return erase(spec);
}

unsigned int vfp_rename(const char *from, const char *to)
{
    static char oesa[NAML$C_MAXRSS + 1], orsa[NAML$C_MAXRSS + 1];
    static char nesa[NAML$C_MAXRSS + 1], nrsa[NAML$C_MAXRSS + 1];
    struct FAB of, nf;
    struct NAML on, nn;
    naml_setup(&of, &on, from, oesa, orsa);
    naml_setup(&nf, &nn, to, nesa, nrsa);
    return sys$rename(&of, 0, 0, &nf);
}

/* ---------- transfers ---------- */

#define BLOCKS 64

struct vfp_file {
    struct FAB fab;
    struct RAB rab;
    struct NAML naml;
    struct XABFHC fhc;
    char esa[NAML$C_MAXRSS + 1], rsa[NAML$C_MAXRSS + 1];
    char spec[VF_SPEC_MAX];
    int writing, text, binary_fix;
    /* binary: a block buffer and the file's exact length */
    char buf[BLOCKS * 512];
    int blen, bpos;
    unsigned int vbn;
    unsigned long long total, length;
};

unsigned int vfp_open_read(const char *spec, int *is_text, unsigned long long *bytes, vfp_file **out)
{
    vfp_file *f = (vfp_file *) calloc(1, sizeof *f);
    unsigned int st;
    int text;
    if (!f) return SS$_INSFMEM;
    vf_strlcpy(f->spec, spec, sizeof f->spec);
    naml_setup(&f->fab, &f->naml, f->spec, f->esa, f->rsa);
    f->fhc = cc$rms_xabfhc;
    f->fab.fab$l_xab = (void *) &f->fhc;
    f->fab.fab$b_fac = FAB$M_GET | FAB$M_BRO;      /* records or blocks, chosen at connect */
    f->fab.fab$b_shr = FAB$M_SHRGET;
    st = sys$open(&f->fab, 0, 0);
    if (!(st & 1)) { free(f); return st; }
    text = (f->fab.fab$b_rfm == FAB$C_VAR || f->fab.fab$b_rfm == FAB$C_VFC)
           && (f->fab.fab$b_rat & (FAB$M_CR | FAB$M_FTN | FAB$M_PRN));
    if (*is_text >= 0) text = *is_text;
    *is_text = f->text = text;
    f->length = f->fhc.xab$l_ebk > 0 ? (unsigned long long) (f->fhc.xab$l_ebk - 1) * 512 + f->fhc.xab$w_ffb : 0;
    *bytes = f->length;
    f->rab = cc$rms_rab;
    f->rab.rab$l_fab = &f->fab;
    f->rab.rab$b_rac = RAB$C_SEQ;
    f->rab.rab$l_rop = RAB$M_RRL | RAB$M_NLK;
    if (!text) f->rab.rab$l_rop |= RAB$M_BIO;
    f->vbn = 1;
    st = sys$connect(&f->rab, 0, 0);
    if (!(st & 1)) { sys$close(&f->fab, 0, 0); free(f); return st; }
    *out = f;
    return SS$_NORMAL;
}

unsigned int vfp_read(vfp_file *f, char *buf, int size, int *got)
{
    unsigned int st;
    if (f->text) {
        f->rab.rab$l_ubf = buf;
        f->rab.rab$w_usz = (unsigned short) (size > 32767 ? 32767 : size);
        st = sys$get(&f->rab, 0, 0);
        if (st == RMS$_EOF) return VFP_EOF;
        if (st == RMS$_RTB) { *got = size; return SS$_NORMAL; }   /* cut to size */
        if (!(st & 1)) return st;
        *got = f->rab.rab$w_rsz;
        return st;
    }
    /* binary: whole blocks from RMS, handed out up to the end-of-file byte */
    if (f->bpos >= f->blen) {
        f->rab.rab$l_bkt = f->vbn;
        f->rab.rab$l_ubf = f->buf;
        f->rab.rab$w_usz = sizeof f->buf;
        st = sys$read(&f->rab, 0, 0);
        if (st == RMS$_EOF) return VFP_EOF;
        if (!(st & 1)) return st;
        f->blen = f->rab.rab$w_rsz;
        f->bpos = 0;
        f->vbn += (unsigned int) ((f->blen + 511) / 512);
        if (f->total + (unsigned long long) f->blen > f->length && f->length)
            f->blen = (int) (f->length - f->total);
        if (f->blen <= 0) return VFP_EOF;
        f->total += (unsigned long long) f->blen;
    }
    *got = f->blen - f->bpos < size ? f->blen - f->bpos : size;
    memcpy(buf, f->buf + f->bpos, (size_t) *got);
    f->bpos += *got;
    return SS$_NORMAL;
}

unsigned int vfp_open_write(const char *spec, int text, unsigned long long expected,
                            vfp_file **out, char *result, int rsize)
{
    vfp_file *f = (vfp_file *) calloc(1, sizeof *f);
    unsigned int st;
    if (!f) return SS$_INSFMEM;
    vf_strlcpy(f->spec, spec, sizeof f->spec);
    naml_setup(&f->fab, &f->naml, f->spec, f->esa, f->rsa);
    f->writing = 1;
    f->text = text;
    f->fab.fab$b_org = FAB$C_SEQ;
    f->fab.fab$l_fop = FAB$M_MXV;              /* always a new version (D8) */
    f->fab.fab$w_deq = 64;
    f->fab.fab$b_shr = FAB$M_NIL;
    if (text) {
        f->fab.fab$b_fac = FAB$M_PUT;
        f->fab.fab$b_rfm = FAB$C_VAR;
        f->fab.fab$b_rat = FAB$M_CR;
        f->fab.fab$w_mrs = 0;
    } else {
        f->fab.fab$b_fac = FAB$M_PUT | FAB$M_BIO;
        f->fab.fab$b_rfm = FAB$C_FIX;
        f->fab.fab$w_mrs = 512;
        f->fab.fab$b_rat = 0;
        f->fab.fab$l_alq = (unsigned int) (expected / 512 + 1);
        f->binary_fix = expected % 512 == 0;
    }
    st = sys$create(&f->fab, 0, 0);
    if (!(st & 1)) { free(f); return st; }
    f->rsa[f->naml.naml$l_long_result_size] = '\0';
    vf_strlcpy(f->spec, f->rsa, sizeof f->spec);
    vf_strlcpy(result, f->rsa, (size_t) rsize);
    f->rab = cc$rms_rab;
    f->rab.rab$l_fab = &f->fab;
    f->rab.rab$b_rac = RAB$C_SEQ;
    if (!text) f->rab.rab$l_rop = RAB$M_BIO;
    f->vbn = 1;
    st = sys$connect(&f->rab, 0, 0);
    if (!(st & 1)) { sys$close(&f->fab, 0, 0); erase(f->spec); free(f); return st; }
    *out = f;
    return SS$_NORMAL;
}

static unsigned int flush_blocks(vfp_file *f, int all)
{
    int n = all ? (f->blen + 511) / 512 * 512 : f->blen / 512 * 512;
    unsigned int st;
    if (n == 0) return SS$_NORMAL;
    if (n > f->blen) memset(f->buf + f->blen, 0, (size_t) (n - f->blen));
    f->rab.rab$l_bkt = f->vbn;
    f->rab.rab$l_rbf = f->buf;
    f->rab.rab$w_rsz = (unsigned short) n;
    st = sys$write(&f->rab, 0, 0);
    if (!(st & 1)) return st;
    f->vbn += (unsigned int) (n / 512);
    if (n < f->blen) memmove(f->buf, f->buf + n, (size_t) (f->blen - n));
    f->blen = n < f->blen ? f->blen - n : 0;
    return SS$_NORMAL;
}

unsigned int vfp_write(vfp_file *f, const char *buf, int len)
{
    unsigned int st;
    if (f->text) {
        f->rab.rab$l_rbf = (char *) buf;
        f->rab.rab$w_rsz = (unsigned short) (len > 32767 ? 32767 : len);
        return sys$put(&f->rab, 0, 0);
    }
    f->total += (unsigned long long) len;
    while (len > 0) {
        int room = (int) sizeof f->buf - f->blen, n = len < room ? len : room;
        memcpy(f->buf + f->blen, buf, (size_t) n);
        f->blen += n; buf += n; len -= n;
        if (f->blen == (int) sizeof f->buf && !((st = flush_blocks(f, 0)) & 1)) return st;
    }
    return SS$_NORMAL;
}

/* Set a written binary file's end of file to its exact length, and its record
   format: fixed 512 when the length is whole blocks, else undefined (D6). */
static unsigned int set_eof(const char *spec, unsigned long long total, int fix)
{
    static char esa[NAML$C_MAXRSS + 1], rsa[NAML$C_MAXRSS + 1];
    struct FAB fab;
    struct NAML naml;
    struct fibdef fib;
    struct dsc$descriptor_s fibd, dv;
    unsigned char fat[ATR$S_RECATTR];
    unsigned short chan = 0, iosb[4], *w = (unsigned short *) fat;
    unsigned int st, ef = (unsigned int) (total / 512) + 1;
    atr_item atr[2];
    naml_setup(&fab, &naml, spec, esa, rsa);
    st = sys$parse(&fab, 0, 0);
    if (st & 1) st = sys$search(&fab, 0, 0);
    if (!(st & 1)) return st;
    dsc(&dv, (char *) &naml.naml$t_dvi[1], naml.naml$t_dvi[0]);
    st = sys$assign(&dv, &chan, 0, 0);
    if (!(st & 1)) return st;
    memset(&fib, 0, sizeof fib);
    memcpy(&fib.fib$w_fid, naml.naml$w_fid, 6);
    dsc(&fibd, (char *) &fib, sizeof fib);
    atr[0].size = ATR$S_RECATTR; atr[0].type = ATR$C_RECATTR; atr[0].addr = fat;
    atr[1].size = 0; atr[1].type = 0; atr[1].addr = 0;
    st = sys$qiow(0, chan, IO$_ACCESS, (void *) iosb, 0, 0, &fibd, 0, 0, 0, atr, 0);
    if (st & 1) st = iosb[0];
    if (st & 1) {
        fix = fix && total % 512 == 0;
        fat[0] = (unsigned char) ((fat[0] & 0xF0) | (fix ? FAB$C_FIX : FAB$C_UDF));
        fat[1] = 0;
        w[1] = fix ? 512 : 0;                  /* record size */
        w[4] = (unsigned short) (ef >> 16);    /* end-of-file block, high word first */
        w[5] = (unsigned short) (ef & 0xFFFF);
        w[6] = (unsigned short) (total % 512); /* first free byte */
        w[8] = fix ? 512 : 0;                  /* maximum record size */
        st = sys$qiow(0, chan, IO$_MODIFY, (void *) iosb, 0, 0, &fibd, 0, 0, 0, atr, 0);
        if (st & 1) st = iosb[0];
    }
    sys$dassgn(chan);
    naml.naml$b_nop |= NAML$M_SYNCHK;
    sys$parse(&fab, 0, 0);
    return st;
}

unsigned int vfp_close(vfp_file *f, int keep)
{
    unsigned int st = SS$_NORMAL, cs;
    if (f->writing && !f->text && keep) st = flush_blocks(f, 1);
    sys$disconnect(&f->rab, 0, 0);
    cs = sys$close(&f->fab, 0, 0);
    if ((st & 1) && !(cs & 1)) st = cs;
    if (f->writing) {
        if (keep && (st & 1) && !f->text && (f->total % 512 || !f->binary_fix))
            st = set_eof(f->spec, f->total, f->binary_fix);
        if (!keep || !(st & 1)) erase(f->spec);   /* nothing half-written is left */
    }
    free(f);
    return st;
}

/* ---------- SSH's files, the log ---------- */

void vfp_ssh_file(const char *name, char *out, int size)
{
    const char *login = getenv("SYS$LOGIN");
    char dir[VF_SPEC_MAX], *rb;
    vf_strlcpy(dir, login ? login : "SYS$LOGIN:[]", sizeof dir);
    rb = strrchr(dir, ']');
    if (rb && rb > dir && rb[-1] == '.') snprintf(rb, sizeof dir - (size_t) (rb - dir), "SSH]");
    else if (rb) snprintf(rb, sizeof dir - (size_t) (rb - dir), ".SSH]");
    snprintf(out, (size_t) size, "%s%s.", dir, name);
}

int vfp_exists(const char *spec)
{
    static char esa[NAML$C_MAXRSS + 1], rsa[NAML$C_MAXRSS + 1];
    struct FAB fab;
    struct NAML naml;
    unsigned int st;
    naml_setup(&fab, &naml, spec, esa, rsa);
    st = sys$parse(&fab, 0, 0);
    if (st & 1) st = sys$search(&fab, 0, 0);
    naml.naml$b_nop |= NAML$M_SYNCHK;
    sys$parse(&fab, 0, 0);
    return (st & 1) != 0;
}

unsigned int vfp_append_line(const char *spec, const char *line)
{
    FILE *f = fopen(spec, "a");
    if (!f) return SS$_ABORT;
    fprintf(f, "%s\n", line);
    return fclose(f) ? SS$_ABORT : SS$_NORMAL;
}

void vfp_log(const char *line)
{
    const char *spec = getenv("VFTP$LOG");
    FILE *f = fopen(spec && *spec ? spec : "SYS$LOGIN:VFTP.LOG", "a");
    vf_when w;
    char t[32];
    if (!f) return;
    vfp_now(&w);
    fprintf(f, "%s %s\n", vf_fmt_when(&w, t, sizeof t), line);
    fclose(f);
}

void vfp_getenv(const char *name, char *out, int size)
{
    const char *v = getenv(name);
    vf_strlcpy(out, v ? v : "", (size_t) size);
}
