/* vf_remote.c - each vfr_ call passed to the protocol of its connection (vf_rimpl.h). */
#include <stdio.h>
#include "vf_rimpl.h"

unsigned int vfr_connect(int proto, const char *host, int port, vf_remote **r, vfr_hostkey *hk,
                         char *err, int errsz)
{
    if (proto == VFR_FTP) return vfr_ftp_connect(host, port, r, hk, err, errsz);
    return vfr_sftp_connect(host, port, r, hk, err, errsz);
}

int vfr_secure(vf_remote *r) { return r && r->secure; }
int vfr_vms_paths(vf_remote *r) { return r && r->vms_paths; }
const char *vfr_error(vf_remote *r) { return r ? r->err : ""; }
const char *vfr_banner(vf_remote *r) { return r ? r->banner : ""; }

unsigned int vfr_trust(vf_remote *r) { return r->ops->trust(r); }
unsigned int vfr_methods(vf_remote *r, const char *user, char *out, int size)
{ return r->ops->methods(r, user, out, size); }
unsigned int vfr_login_password(vf_remote *r, const char *user, const char *password)
{ return r->ops->login_password(r, user, password); }
unsigned int vfr_login_key(vf_remote *r, const char *user, const char *keyfile, const char *passphrase)
{ return r->ops->login_key(r, user, keyfile, passphrase); }
unsigned int vfr_realpath(vf_remote *r, const char *path, char *out, int size)
{ return r->ops->realpath(r, path, out, size); }
unsigned int vfr_list(vf_remote *r, const char *dir, vfr_entry_cb cb, void *ctx)
{ return r->ops->list(r, dir, cb, ctx); }
unsigned int vfr_stat(vf_remote *r, const char *path, vf_entry *e) { return r->ops->stat(r, path, e); }
unsigned int vfr_mkdir(vf_remote *r, const char *path) { return r->ops->mkdir(r, path); }
unsigned int vfr_rmdir(vf_remote *r, const char *path) { return r->ops->rmdir(r, path); }
unsigned int vfr_unlink(vf_remote *r, const char *path) { return r->ops->unlink(r, path); }
unsigned int vfr_rename(vf_remote *r, const char *from, const char *to) { return r->ops->rename(r, from, to); }
unsigned int vfr_replace(vf_remote *r, const char *from, const char *to) { return r->ops->replace(r, from, to); }
unsigned int vfr_chmod(vf_remote *r, const char *path, unsigned int mode) { return r->ops->chmod(r, path, mode); }
unsigned int vfr_open_read(vf_remote *r, const char *path, int text, vfr_file **f)
{ return r->ops->open_read(r, path, text, f); }
unsigned int vfr_read(vfr_file *f, char *buf, int size, int *got) { return f->r->ops->read(f, buf, size, got); }
unsigned int vfr_open_write(vf_remote *r, const char *path, int text, vfr_file **f)
{ return r->ops->open_write(r, path, text, f); }
unsigned int vfr_write(vfr_file *f, const char *buf, int len) { return f->r->ops->write(f, buf, len); }
unsigned int vfr_close(vfr_file *f) { return f->r->ops->close(f); }
void vfr_keepalive(vf_remote *r) { if (r) r->ops->keepalive(r); }
void vfr_disconnect(vf_remote *r) { if (r) r->ops->disconnect(r); }
