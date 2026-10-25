/* vf_text.c - LF lines to records. No I/O. */
#include <string.h>
#include "vf_text.h"

void vf_lines_init(vf_lines *l) { l->len = 0; l->split = 0; }

static unsigned int emit(vf_lines *l, vf_rec_fn fn, void *ctx)
{
    int n = l->len;
    if (n && l->buf[n - 1] == '\r') n--;
    l->len = 0;
    return fn(ctx, l->buf, n);
}

unsigned int vf_lines_feed(vf_lines *l, const char *bytes, int n, vf_rec_fn fn, void *ctx)
{
    unsigned int st;
    while (n > 0) {
        const char *lf = (const char *) memchr(bytes, '\n', (size_t) n);
        int take = lf ? (int) (lf - bytes) : n;
        if (l->len + take > VF_REC_MAX) {          /* cut a record that is too long */
            int fit = VF_REC_MAX - l->len;
            memcpy(l->buf + l->len, bytes, (size_t) fit);
            l->len += fit;
            l->split++;
            if (!((st = fn(ctx, l->buf, l->len)) & 1)) return st;
            l->len = 0;
            bytes += fit; n -= fit;
            continue;
        }
        memcpy(l->buf + l->len, bytes, (size_t) take);
        l->len += take;
        bytes += take; n -= take;
        if (lf) {
            bytes++; n--;
            if (!((st = emit(l, fn, ctx)) & 1)) return st;
        }
    }
    return 1;
}

unsigned int vf_lines_end(vf_lines *l, vf_rec_fn fn, void *ctx)
{
    if (!l->len) return 1;
    return emit(l, fn, ctx);
}
