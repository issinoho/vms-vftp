/* vf_remote.h - the remote side: one connection to an SFTP server (later FTP
   and SCP, PLAN Phases 2-3). No libssh2 or socket types: src/net/vf_sftp.c
   implements it with libssh2 (D3), src/plat/stub/vf_remote_stub.c with an
   in-memory server for the Linux tests.

   Every call returns 1 on success and an even value on failure; vfr_error()
   then says why. */
#ifndef VF_REMOTE_H
#define VF_REMOTE_H

#include "vf_pane.h"

enum { VFR_SFTP = 1 };

/* What known_hosts says about the server's key (D7). */
enum { VFR_HK_MATCH = 1, VFR_HK_NOTFOUND, VFR_HK_MISMATCH, VFR_HK_NOFILE };

typedef struct {
    char type[32];           /* "ecdsa-sha2-nistp256" */
    char fingerprint[64];    /* "SHA256:p2QAMXNIC1..." as ssh shows it */
    int known;               /* VFR_HK_... */
    char file[256];          /* the known_hosts consulted */
} vfr_hostkey;

typedef struct vf_remote vf_remote;
typedef struct vfr_file vfr_file;

#define VFR_FAIL 0x2000u     /* a generic failure (even) */
#define VFR_AUTH 0x2002u     /* the server refused the login */

/* Connect and do the key exchange; *hk says whether the server is known.
   On failure *r is 0 and err says why. */
unsigned int vfr_connect(int proto, const char *host, int port, vf_remote **r, vfr_hostkey *hk,
                         char *err, int errsz);
/* Add the server's key to known_hosts (after the user agreed). */
unsigned int vfr_trust(vf_remote *r);
/* The login methods the server offers this user ("publickey,password,..."). */
unsigned int vfr_methods(vf_remote *r, const char *user, char *out, int size);
unsigned int vfr_login_password(vf_remote *r, const char *user, const char *password);
unsigned int vfr_login_key(vf_remote *r, const char *user, const char *keyfile, const char *passphrase);
/* After login: the directory a path means ("." = the login directory). */
unsigned int vfr_realpath(vf_remote *r, const char *path, char *out, int size);

typedef int (*vfr_entry_cb)(void *ctx, const vf_entry *e);
unsigned int vfr_list(vf_remote *r, const char *dir, vfr_entry_cb cb, void *ctx);
unsigned int vfr_stat(vf_remote *r, const char *path, vf_entry *e);
unsigned int vfr_mkdir(vf_remote *r, const char *path);
unsigned int vfr_rmdir(vf_remote *r, const char *path);
unsigned int vfr_unlink(vf_remote *r, const char *path);
unsigned int vfr_rename(vf_remote *r, const char *from, const char *to);   /* never replaces */
unsigned int vfr_replace(vf_remote *r, const char *from, const char *to);  /* from takes to's place */

unsigned int vfr_open_read(vf_remote *r, const char *path, vfr_file **f);
/* Up to size bytes; *got 0 at the end. */
unsigned int vfr_read(vfr_file *f, char *buf, int size, int *got);
/* Create or replace path (the app asks before replacing). */
unsigned int vfr_open_write(vf_remote *r, const char *path, vfr_file **f);
unsigned int vfr_write(vfr_file *f, const char *buf, int len);
unsigned int vfr_close(vfr_file *f);

/* Keep an idle connection open (call every half minute or so). */
void vfr_keepalive(vf_remote *r);
void vfr_disconnect(vf_remote *r);
const char *vfr_error(vf_remote *r);
const char *vfr_banner(vf_remote *r);     /* e.g. "SSH-2.0-OpenSSH_9.9", "" if none */

#endif
