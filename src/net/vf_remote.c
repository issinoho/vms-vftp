/* vf_remote.c - each vfr_ call passed to the protocol of its connection (vf_rimpl.h). */
#include <stdio.h>
#include "vf_rimpl.h"

unsigned int vfr_connect(int proto, const char *host, int port, vf_remote **r, vfr_hostkey *hk,
                         char *err, int errsz)
{
    if (proto == VFR_FTP) return vfr_ftp_connect(host, port, r, hk, err, errsz);
    if (proto == VFR_SCP) return vfr_scp_connect(host, port, r, hk, err, errsz);
    return vfr_sftp_connect(host, port, r, hk, err, errsz);
}

int vfr_secure(vf_remote *r) { return r && r->secure; }
int vfr_vms_paths(vf_remote *r) { return r && r->vms_paths; }
int vfr_needs_size(vf_remote *r) { return r && r->need_size; }
int vfr_can_resume(vf_remote *r) { return r && r->resume; }
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
{ return r->ops->open_read(r, path, text, 0, f); }
unsigned int vfr_open_read_at(vf_remote *r, const char *path, unsigned long long offset, vfr_file **f)
{
    if (offset && !r->resume) { snprintf(r->err, sizeof r->err, "this server cannot start part way"); return VFR_FAIL; }
    return r->ops->open_read(r, path, 0, offset, f);
}
unsigned int vfr_read(vfr_file *f, char *buf, int size, int *got) { return f->r->ops->read(f, buf, size, got); }
unsigned int vfr_open_write(vf_remote *r, const char *path, int text, unsigned long long size, vfr_file **f)
{ return r->ops->open_write(r, path, text, size, 0, f); }
unsigned int vfr_open_write_at(vf_remote *r, const char *path, unsigned long long offset, vfr_file **f)
{
    if (offset && !r->resume) { snprintf(r->err, sizeof r->err, "this server cannot start part way"); return VFR_FAIL; }
    return r->ops->open_write(r, path, 0, 0, offset, f);
}
unsigned int vfr_write(vfr_file *f, const char *buf, int len) { return f->r->ops->write(f, buf, len); }
unsigned int vfr_close(vfr_file *f) { return f->r->ops->close(f); }
void vfr_keepalive(vf_remote *r) { if (r) r->ops->keepalive(r); }
void vfr_disconnect(vf_remote *r) { if (r) r->ops->disconnect(r); }
