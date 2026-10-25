/* vf_platform.h - the whole OS surface of VFTP's local side (VLMF D3). No VMS
   types. src/plat/vms/ implements it with SMG$ (keys), a terminal channel (the
   screen) and RMS (files, as VTREE does); src/plat/stub/ fakes it for the Linux
   tests with an in-memory disk. The remote side is vf_remote.h.

   Status values are VMS condition values: odd = success. */
#ifndef VF_PLATFORM_H
#define VF_PLATFORM_H

#include "vf_types.h"
#include "vf_screen.h"
#include "vf_pane.h"

#define VFP_OK 1u
#define VFP_STOPPED 0x1000u     /* a callback asked to stop (even = failure) */

typedef struct {
    char user[40];
    char node[16];
} vfp_session;

/* Take the terminal. On failure nothing has been changed. */
unsigned int vfp_start(vfp_session *s);
void vfp_stop(void);

/* Screen and keys */
void vfp_screen_size(int *rows, int *cols);
void vfp_draw(const vf_screen *s);         /* show the model (only what changed) */
void vfp_repaint(void);                    /* the next draw sends everything */
int  vfp_key(int timeout);                 /* seconds; 0 = only look; VK_TIMEOUT if none */
int  vfp_broadcast(char *buf, int size);   /* a trapped message, 0 when none */
void vfp_flush_keys(void);                 /* typeahead must never answer a question */

void vfp_now(vf_when *w);
void vfp_message(unsigned int status, char *buf, int size);
/* Seconds since some fixed moment, with hundredths; for transfer rates. */
double vfp_clock(void);

/* ---------- local files ---------- */

/* The normalised directory of spec ("DKA0:[IAIN]"), or of SYS$DISK:[] when spec
   is empty. ods5: whether its disk is ODS-5. Fails if it doesn't exist. */
unsigned int vfp_locate(const char *spec, char *dir, int size, int *ods5);
/* Every entry of dir (all versions, or the highest only), with attributes. The
   callback returns 0 to go on, nonzero to stop. */
typedef int (*vfp_entry_cb)(void *ctx, const vf_entry *e);
unsigned int vfp_list(const char *dir, int all_versions, vfp_entry_cb cb, void *ctx);
unsigned int vfp_mkdir(const char *dir);                    /* "DKA0:[IAIN.NEW]" */
/* Delete exactly one file version ("DKA0:[IAIN]X.TXT;3"); a directory file only
   if it is empty. */
unsigned int vfp_delete(const char *spec);
unsigned int vfp_rename(const char *from, const char *to);

/* Reading a file for an upload or a view. *is_text on entry: -1 decide (D6:
   variable or VFC records with carriage control are text), 0 binary, 1 text; on
   return what was chosen. Text is read a record at a time, binary in bytes.
   *bytes: an estimate for progress (the used blocks * 512 for text). */
typedef struct vfp_file vfp_file;
unsigned int vfp_open_read(const char *spec, int *is_text, unsigned long long *bytes, vfp_file **f);
/* Text: the next record (without line end) in buf, *got its length (cut to
   size). Binary: up to size bytes. At the end: VFP_EOF. */
#define VFP_EOF 0x1001u
unsigned int vfp_read(vfp_file *f, char *buf, int size, int *got);

/* Writing a download: always a new version of spec (which has no version).
   Text: variable-length records with carriage control, one vfp_write per record.
   Binary: the bytes; fixed 512-byte records if expected (the size the server
   listed) is a multiple of 512, otherwise undefined records with the exact
   length (D6). result: the new file's full name. */
unsigned int vfp_open_write(const char *spec, int text, unsigned long long expected,
                            vfp_file **f, char *result, int rsize);
unsigned int vfp_write(vfp_file *f, const char *buf, int len);
/* Close; for a file being written, keep = 0 deletes it (a cancelled or failed
   download leaves nothing behind). */
unsigned int vfp_close(vfp_file *f, int keep);

/* ---------- SSH's files ---------- */

/* A file in the user's SSH directory, as VSI's ssh keeps it (NOTES N3):
   vfp_ssh_file("known_hosts") -> "USER$ROOT:[IAIN.SSH]KNOWN_HOSTS." */
void vfp_ssh_file(const char *name, char *out, int size);
int  vfp_exists(const char *spec);
unsigned int vfp_append_line(const char *spec, const char *line);

/* Append a line to VFTP's log (VFTP$LOG, default SYS$LOGIN:VFTP.LOG). Never a
   password. */
void vfp_log(const char *line);
/* A logical name's value (or environment variable), "" if none. */
void vfp_getenv(const char *name, char *out, int size);

#endif
