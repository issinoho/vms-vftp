/* vf_stub.h - test controls for the Linux stubs (local disk and remote server). */
#ifndef VF_STUB_H
#define VF_STUB_H

void stub_reset(void);                   /* the sample local disk and server, nothing trusted */
void stub_remote_reset(void);            /* the server alone */
void stub_set_size(int rows, int cols);
const char *stub_log(void);              /* every vfp_log line so far */
/* Local files: content of the highest version (or exact spec with ;n), 0 if none. */
const char *stub_local(const char *spec, int *len, int *text, int *versions);
void stub_add_local(const char *dir, const char *name, int text, const char *data, int len);
/* Remote files */
const char *stub_remote(const char *path, int *len);
int stub_remote_like(const char *prefix);  /* 1 if any remote path starts with prefix */
void stub_add_remote(const char *path, const char *data, int len);
void stub_add_remote_dir(const char *path);
void stub_add_remote_link(const char *path);   /* a link to a directory (SFTP lists it so) */
void stub_trust(int on);                 /* the server's key in known_hosts or not */
void stub_change_key(int on);            /* the server's key differs from known_hosts */
void stub_ftps(int on);                  /* FTP takes AUTH TLS, its certificate not yet known */
void stub_cancel_after(int reads);       /* F10 arrives after that many polls */

#endif
