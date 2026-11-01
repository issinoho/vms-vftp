/* vf_sites.h - the sites file (D9, D14): saved connections, never a password.
   Pure: the app reads the file a line at a time into vf_sites_line and writes it
   with vf_sites_write. The file, one block per site:

       ! a comment ("#" too)
       [work]
       protocol = SFTP
       host = build.example.com
       port = 22
       user = iain
       key = USER$ROOT:[IAIN.SSH]ID_ED25519.
       remote = /home/iain
       local = DKA0:[IAIN.WORK]

   A line VFTP cannot read (an unknown key, a password, a key outside a block)
   is left out and remembered in bad: the app then saves nothing over the file. */
#ifndef VF_SITES_H
#define VF_SITES_H

#define VF_SITE_NAME 64
#define VF_SITE_FIELD 256

typedef struct {
    char name[VF_SITE_NAME];
    char proto[8];           /* "SFTP" or "FTP" */
    char host[VF_SITE_FIELD], port[8], user[64], key[VF_SITE_FIELD];
    char remote[VF_SITE_FIELD], local[VF_SITE_FIELD];
} vf_site;

typedef struct {
    vf_site *s; int n, cap;
    int line;                /* lines read so far */
    int bad;                 /* lines left out */
    char why[160];           /* the first: "line 7: passwords are never kept (D9)" */
    int cur;                 /* the block being read, -1 none */
} vf_sites;

void vf_sites_init(vf_sites *t);
void vf_sites_free(vf_sites *t);
/* One line of the file, without its line end. */
void vf_sites_line(vf_sites *t, const char *line);
int  vf_sites_find(const vf_sites *t, const char *name);   /* case-blind; -1 if none */
/* Add s, or replace the site of that name. 0: no memory, or not a usable name. */
int  vf_sites_put(vf_sites *t, const vf_site *s);
void vf_sites_remove(vf_sites *t, int i);
void vf_sites_sort(vf_sites *t);                           /* by name */
/* A usable name: 1 to 63 characters, no "[" or "]", not blank. */
int  vf_site_name_ok(const char *name);
/* The whole file, a line at a time (sorted by name); stops at the first even
   status cb returns, and returns it. */
typedef unsigned int (*vf_sites_cb)(void *ctx, const char *line, int len);
unsigned int vf_sites_write(const vf_sites *t, vf_sites_cb cb, void *ctx);

#endif
