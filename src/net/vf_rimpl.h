/* vf_rimpl.h - inside the remote layer: what each protocol (vf_sftp.c: SFTP, SCP; vf_ftp.c)
   gives vf_remote.c, which passes every vfr_ call on. Not for the app. */
#ifndef VF_RIMPL_H
#define VF_RIMPL_H

#include "vf_remote.h"

typedef struct {
    unsigned int (*trust)(vf_remote *r);
    unsigned int (*methods)(vf_remote *r, const char *user, char *out, int size);
    unsigned int (*login_password)(vf_remote *r, const char *user, const char *password);
    unsigned int (*login_key)(vf_remote *r, const char *user, const char *keyfile, const char *passphrase);
    unsigned int (*realpath)(vf_remote *r, const char *path, char *out, int size);
    unsigned int (*list)(vf_remote *r, const char *dir, vfr_entry_cb cb, void *ctx);
    unsigned int (*stat)(vf_remote *r, const char *path, vf_entry *e);
    unsigned int (*mkdir)(vf_remote *r, const char *path);
    unsigned int (*rmdir)(vf_remote *r, const char *path);
    unsigned int (*unlink)(vf_remote *r, const char *path);
    unsigned int (*rename)(vf_remote *r, const char *from, const char *to);
    unsigned int (*replace)(vf_remote *r, const char *from, const char *to);
    unsigned int (*chmod)(vf_remote *r, const char *path, unsigned int mode);
    unsigned int (*open_read)(vf_remote *r, const char *path, int text, vfr_file **f);
    unsigned int (*read)(vfr_file *f, char *buf, int size, int *got);
    unsigned int (*open_write)(vf_remote *r, const char *path, int text, unsigned long long size, vfr_file **f);
    unsigned int (*write)(vfr_file *f, const char *buf, int len);
    unsigned int (*close)(vfr_file *f);
    void (*keepalive)(vf_remote *r);
    void (*disconnect)(vf_remote *r);
} vfr_ops;

/* The start of every protocol's connection and open file. */
struct vf_remote {
    const vfr_ops *ops;
    char err[300];
    char banner[128];        /* "SSH-2.0-OpenSSH_9.9", "220 node FTP Server ..." */
    int secure;              /* a password sent now is encrypted */
    int vms_paths;           /* paths are VMS specs (a VMS FTP server, N8) */
    int need_size;           /* open_write must have the exact size (SCP) */
};
struct vfr_file { vf_remote *r; };

/* A TCP connection (in vf_sftp.c): the socket, or -1 and err says why. */
int vfr_dial(const char *host, int port, char *err, int errsz);

unsigned int vfr_sftp_connect(const char *host, int port, vf_remote **r, vfr_hostkey *hk,
                              char *err, int errsz);
unsigned int vfr_scp_connect(const char *host, int port, vf_remote **r, vfr_hostkey *hk,
                             char *err, int errsz);
unsigned int vfr_ftp_connect(const char *host, int port, vf_remote **r, vfr_hostkey *hk,
                             char *err, int errsz);

#endif
