/* vf_app.h - VFTP's state and behaviour. Portable: it reaches the OS only through
   vf_platform.h and the remote side only through vf_remote.h, so it runs
   unchanged on VMS and against the Linux stubs. */
#ifndef VF_APP_H
#define VF_APP_H

#include "vf_types.h"
#include "vf_edit.h"
#include "vf_screen.h"
#include "vf_pane.h"
#include "vf_text.h"
#include "vf_platform.h"
#include "vf_remote.h"

enum { M_MAIN, M_HELP, M_VIEW, M_INFO, M_FORM, M_CONFIRM };
enum { P_NONE, P_FILTER, P_RENAME, P_MKDIR, P_TAG, P_JUMP, P_ASK };
enum { ASK_DELETE = 1, ASK_REPLACE, ASK_HOSTKEY, ASK_CLEARPW };
enum { X_AUTO, X_ASCII, X_BINARY };
enum { PANE_LOCAL, PANE_REMOTE };

/* The Open form's fields */
enum { FF_PROTO, FF_HOST, FF_PORT, FF_USER, FF_PASSWORD, FF_KEY, FF_PHRASE, FF_DIR, FF_COUNT };

#define VF_FIELD 256
#define VF_MAXOPS 4096
#define VF_MAXSTEPS 100000   /* files and directories in one transfer */
#define VF_MAXDEPTH 32       /* directory levels below the one tagged */

/* One step of a transfer (D13): a directory to make, or a file to copy. The
   steps are found before anything is copied, so the confirm screen can name every
   file that would be replaced, at any depth. */
typedef struct {
    int is_dir;
    int exists;              /* directory: there already; file: replaces one */
    int parent;              /* the step that makes its directory; -1: the pane's own */
    int top;                 /* the entry of ops it comes from */
    int done, failed;
    unsigned long long size;
    char *name;              /* the path below the pane, remote names: "data/big.log" */
    char *src, *dst;         /* full paths; a directory's dst is what mkdir takes */
} vf_step;

typedef struct {
    vfp_session sess;
    vf_screen scr;
    int rows, cols;
    int mode, back_mode;
    int gold, quit;

    vf_pane pane[2];          /* PANE_LOCAL, PANE_REMOTE */
    int act;                  /* the pane the keys work on */
    int all_versions;         /* local pane: every version, not only the highest */
    int ods5;                 /* the local directory's disk */
    int xmode;                /* X_AUTO, X_ASCII, X_BINARY (D6) */
    char text_types[512];     /* VFTP$TEXT_TYPES, "" = built in */
    int year;                 /* this year, for short dates */

    /* the connection */
    vf_remote *rem;
    int proto;                /* VFR_SFTP, VFR_FTP */
    int rvms;                 /* the server's paths are VMS specs (vfr_vms_paths) */
    int clear_ok;             /* the user agreed to send this password in clear (D10) */
    char host[256], user[64], banner[128];
    int port;
    vfr_hostkey hk;
    double last_alive;

    char msg[256]; int msg_err;

    /* Help, View, Info and Confirm text */
    char **text; int ntext, ttop;
    char text_title[256];

    /* the Open form */
    char fv[FF_COUNT][VF_FIELD];
    int fcur;
    vf_edit fedit;
    char form_err[256];

    /* the prompt line */
    int prompt;
    char prompt_label[128];
    vf_edit pedit;
    int ask;                  /* ASK_... while prompt == P_ASK */

    /* the files an operation works on (indexes into a pane's entries) */
    int ops[VF_MAXOPS], nops, ops_pane, ops_get;
    vf_step *steps; int nsteps, capsteps;
    int scan_links, scan_deep;   /* directories the scan left out: links, too deep */
    double scan_drawn;

    /* a transfer in progress (the strip shows it) */
    int xfer_on, xfer_get, xfer_i, xfer_n, xfer_text, xfer_dirs;
    char xfer_name[256];
    unsigned long long xfer_done, xfer_total, batch_done, batch_total;
    double xfer_t0, xfer_drawn;
    int cancel;
    vf_lines lines;
} vf_app;

int  vf_app_init(vf_app *a, const char *target, int rows, int cols);
void vf_app_free(vf_app *a);
void vf_app_draw(vf_app *a);
void vf_app_key(vf_app *a, int key);
void vf_app_run(vf_app *a);

/* shared by vf_app.c and vf_draw.c */
void vf_set_msg(vf_app *a, int err, const char *fmt, ...);
const char *vf_field_label(int i);
const char *vf_field_hint(int i);
const char *vf_xmode_name(int x);

#endif
