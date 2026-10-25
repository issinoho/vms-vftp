/* vf_text.h - a text download's bytes as records: split at LF, a CR before the
   LF dropped, a last line without LF kept. No I/O. */
#ifndef VF_TEXT_H
#define VF_TEXT_H

#define VF_REC_MAX 32767         /* RMS's longest variable-length record */

typedef unsigned int (*vf_rec_fn)(void *ctx, const char *rec, int len);

typedef struct {
    char buf[VF_REC_MAX];
    int len;
    int split;                   /* records cut because they were too long */
} vf_lines;

void vf_lines_init(vf_lines *l);
/* Feed bytes; each complete record goes to fn. Returns fn's first failure (even),
   or 1. */
unsigned int vf_lines_feed(vf_lines *l, const char *bytes, int n, vf_rec_fn fn, void *ctx);
unsigned int vf_lines_end(vf_lines *l, vf_rec_fn fn, void *ctx);

#endif
