/* vf_pane.h - one pane's files: the entries read from a directory (local or
   remote), the order they are shown in, the filter, the tags and the cursor.
   Pure: the platform and the remote layer fill it, the app draws it. */
#ifndef VF_PANE_H
#define VF_PANE_H

#include <stddef.h>
#include "vf_types.h"

typedef struct {
    char name[256];          /* as listed: local "NOTES.TXT;1", remote "notes.txt" */
    int is_dir;
    int is_link;             /* remote symbolic link (followed when entered) */
    int is_parent;           /* the ".." entry at the top */
    unsigned long long size; /* bytes (remote; local: allocated blocks * 512 is not used) */
    unsigned int blocks;     /* local: used blocks */
    int has_size, has_time;
    vf_when mtime;           /* local time */
    unsigned int perms;      /* remote: Unix mode bits (0 if unknown) */
    char prot[24];           /* local: "(RWED,RWED,RE,)" */
    char owner[32];          /* user name, or a UIC */
    int is_text;             /* local: variable or VFC records with carriage control */
    int tagged;
} vf_entry;

enum { VF_SORT_NAME, VF_SORT_TYPE, VF_SORT_SIZE, VF_SORT_DATE, VF_SORTS };

typedef struct {
    int local;               /* 1: this machine (VMS specs), 0: the remote side */
    int vms;                 /* VMS files: versions, blocks, protection (local, or a VMS
                                FTP server, N8); set to local by vf_pane_init */
    char dir[VF_SPEC_MAX];   /* "DKA0:[IAIN]" or "/home/iain"; "" = nothing read */
    vf_entry *e; int n, cap;
    int *vis; int nvis;      /* shown, in order: indexes into e */
    int cur, top;
    int sort;
    char filter[64];         /* wildcard ("*" or "" = all) */
    unsigned int status;     /* of the last read; odd = success */
} vf_pane;

void vf_pane_init(vf_pane *p, int local);
void vf_pane_free(vf_pane *p);
void vf_pane_clear(vf_pane *p);                      /* entries only */
int  vf_pane_add(vf_pane *p, const vf_entry *e);    /* 0 on no memory */
/* Filter and sort into vis (directories first, ".." always first); the cursor
   stays on the entry of the same name when there is one. */
void vf_pane_rebuild(vf_pane *p);
vf_entry *vf_pane_cur(vf_pane *p);                  /* 0 when empty */
vf_entry *vf_pane_find(vf_pane *p, const char *name);   /* by name, case-blind locally */
void vf_pane_goto(vf_pane *p, const char *name);    /* cursor to that entry, if shown */
/* Tags (never on ".."). Returns the number of entries changed. */
int  vf_pane_tag_match(vf_pane *p, const char *pattern, int on);
void vf_pane_tagged(const vf_pane *p, int *count, unsigned long long *bytes, unsigned int *blocks);
/* Totals of the shown files (not directories). */
void vf_pane_totals(const vf_pane *p, int *files, int *dirs, unsigned long long *bytes,
                    unsigned int *blocks);

/* One row of the pane, width columns, without the tag mark. wide: dates with the
   time and the year, protection or permissions, owner. year: this year (short
   dates leave it out). */
void vf_pane_row(const vf_pane *p, const vf_entry *e, int width, int year, char *buf, size_t size);

/* "1234", "12K", "3.4M", "1.2G" - at most 5 characters. */
char *vf_fmt_bytes(unsigned long long n, char *buf, size_t size);
/* "drwxr-x---" from Unix mode bits. */
char *vf_fmt_perms(unsigned int mode, int is_dir, int is_link, char *buf, size_t size);
/* New permissions from what the user typed: octal ("644", "2775"), in full
   ("rw-r--r--"), or as chmod's symbolic changes ("go-w", "u+x,g=r", "a+X"),
   which work from mode (known: mode is what the server listed; if not, only the
   first two forms). 1 and *out; 0 and why. */
int vf_perms_apply(const char *spec, unsigned int mode, int is_dir, int known, unsigned int *out,
                   char *why, size_t wsz);
/* Compare two times: <0, 0, >0. */
int  vf_when_cmp(const vf_when *a, const vf_when *b);

#endif
