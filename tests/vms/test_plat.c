/* test_plat.c - vf_plat_vms.c's files on the node, without a terminal: what a
   download writes and an upload reads (D6), in [.LIVE] (made by test_plat.com).
   Prints "PLAT: n checks, m failed". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vf_platform.h"
#include "vf_util.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static char dir[512];

static int write_file(const char *name, int text, const char *data, int len, unsigned long long expected,
                      int keep, char *result)
{
    char spec[600];
    vfp_file *f;
    unsigned int st;
    snprintf(spec, sizeof spec, "%s%s", dir, name);
    st = vfp_open_write(spec, text, expected, &f, result, 512);
    if (!(st & 1)) { printf("open_write %s: %X\n", spec, st); return 0; }
    if (text) {
        const char *p = data, *e = data + len;
        while (p < e) {
            const char *lf = memchr(p, '\n', (size_t) (e - p));
            int n = lf ? (int) (lf - p) : (int) (e - p);
            vfp_write(f, p, n);
            p += n + (lf ? 1 : 0);
        }
    } else {
        int i;
        for (i = 0; i < len; i += 300) vfp_write(f, data + i, len - i < 300 ? len - i : 300);
    }
    st = vfp_close(f, keep);
    if (!(st & 1)) { printf("close %s: %X\n", spec, st); return 0; }
    return 1;
}

/* Read a file back as an upload would: records as LF lines, or bytes. */
static int read_file(const char *spec, int want_text, char *out, int size, int *is_text)
{
    vfp_file *f;
    unsigned long long est;
    unsigned int st;
    int got, n = 0, t = want_text;
    st = vfp_open_read(spec, &t, &est, &f);
    if (!(st & 1)) { printf("open_read %s: %X\n", spec, st); return -1; }
    *is_text = t;
    while (n < size - 2 && ((st = vfp_read(f, out + n, size - n - 2, &got)) & 1)) {
        n += got;
        if (t) out[n++] = '\n';
    }
    vfp_close(f, 1);
    if (st != VFP_EOF) { printf("read %s: %X\n", spec, st); return -1; }
    return n;
}

static int count;
static int lister(void *ctx, const vf_entry *e)
{
    (void) ctx;
    printf("  %-22s %5u blocks %8llu bytes %s %s %s\n", e->name, e->blocks, e->size,
           e->is_dir ? "dir" : e->is_text ? "text" : "binary", e->prot, e->owner);
    count++;
    return 0;
}

int main(void)
{
    static char bin[3000], back[8000];
    char r1[512], r2[512], r3[512], r4[512], kh[512];
    int i, n, t, ods5;
    unsigned int st;
    for (i = 0; i < (int) sizeof bin; i++) bin[i] = (char) (i * 13 + 7);

    st = vfp_locate("[.LIVE]", dir, sizeof dir, &ods5);
    CHECK(st & 1);
    printf("dir %s ods5 %d\n", dir, ods5);

    /* text: records, a new version each time */
    CHECK(write_file("T.TXT", 1, "first\n\nthird line\n", 18, 0, 1, r1));
    CHECK(write_file("T.TXT", 1, "again\n", 6, 0, 1, r2));
    printf("text %s, %s\n", r1, r2);
    CHECK(strstr(r1, ";1") != 0 && strstr(r2, ";2") != 0);
    n = read_file(r1, -1, back, sizeof back, &t);
    CHECK(t == 1 && n == 18 && !memcmp(back, "first\n\nthird line\n", 18));

    /* binary, not whole blocks: undefined records, exact length */
    CHECK(write_file("B.BIN", 0, bin, 1000, 1000, 1, r3));
    n = read_file(r3, -1, back, sizeof back, &t);
    CHECK(t == 0 && n == 1000 && !memcmp(back, bin, 1000));
    /* binary, whole blocks: fixed 512 */
    CHECK(write_file("F.BIN", 0, bin, 2560, 2560, 1, r4));
    n = read_file(r4, -1, back, sizeof back, &t);
    CHECK(t == 0 && n == 2560 && !memcmp(back, bin, 2560));
    /* a server that listed the wrong size: still exact */
    CHECK(write_file("W.BIN", 0, bin, 2999, 1024, 1, r4));
    n = read_file(r4, -1, back, sizeof back, &t);
    CHECK(t == 0 && n == 2999 && !memcmp(back, bin, 2999));
    /* text forced to binary: the record bytes, as stored */
    n = read_file(r1, 0, back, sizeof back, &t);
    CHECK(t == 0 && n > 0);

    /* a cancelled download leaves nothing */
    CHECK(write_file("GONE.BIN", 0, bin, 700, 700, 0, r4));
    {
        char spec[600];
        snprintf(spec, sizeof spec, "%sGONE.BIN", dir);
        CHECK(!vfp_exists(spec));
    }

    /* listing, delete of one version, rename, mkdir */
    count = 0;
    CHECK(vfp_list(dir, 1, lister, 0) & 1);
    CHECK(count == 5);
    CHECK(vfp_delete(r1) & 1);
    CHECK(!(vfp_delete("[.LIVE]T.TXT") & 1));          /* no version: refused */
    {
        char from[600], to[600], sub[600];
        snprintf(from, sizeof from, "%sB.BIN;1", dir);
        snprintf(to, sizeof to, "%sRENAMED.BIN", dir);
        char done[600];
        CHECK(vfp_rename(from, to, done, sizeof done) & 1);
        CHECK(vfp_exists(to));
        CHECK(strstr(done, "RENAMED.BIN;1") != 0);
        snprintf(sub, sizeof sub, "%.*s.SUB]", (int) strlen(dir) - 1, dir);
        CHECK(vfp_mkdir(sub) & 1);
    }
    count = 0;
    CHECK(vfp_list(dir, 0, lister, 0) & 1);
    CHECK(count == 5);                                  /* T.TXT;2, RENAMED, F, W, SUB */

    {
        /* a new directory denies its owner delete; vfp_delete gives it (N13 of VTREE) */
        char sub[600];
        snprintf(sub, sizeof sub, "%sSUB.DIR;1", dir);
        st = vfp_delete(sub);
        if (!(st & 1)) printf("delete %s: %X\n", sub, st);
        CHECK(st & 1);
        CHECK(!vfp_exists(sub));
    }
    {
        /* resume (D16): a half file keeps its whole blocks, opened again it goes on
           after them, and a read can start part way */
        static char big[1500], back[1600];
        char res[512], res2[512], spec[600], done[600];
        unsigned long long have = 0, est;
        vfp_file *f;
        int i, t, n, got;
        for (i = 0; i < (int) sizeof big; i++) big[i] = (char) (i % 241);
        CHECK(write_file("H.BIN_VFTP-PART", 0, big, 1300, 1500, VFP_KEEP_PART, res));
        CHECK(vfp_exists(res));
        st = vfp_open_resume(res, 1500, &have, &f, res2, sizeof res2);
        CHECK((st & 1) && have == 1024);
        if (st & 1) {
            CHECK(vfp_write(f, big + 1024, 476) & 1);
            CHECK(vfp_close(f, 1) & 1);
        }
        snprintf(spec, sizeof spec, "%sH.BIN", dir);
        CHECK(vfp_rename(res, spec, done, sizeof done) & 1);
        CHECK(strstr(done, "H.BIN;1") != 0);
        n = read_file(done, 0, back, sizeof back, &t);
        CHECK(n == 1500 && !t && !memcmp(back, big, 1500));
        /* under a block written: nothing kept */
        CHECK(write_file("S.BIN_VFTP-PART", 0, big, 300, 1500, VFP_KEEP_PART, res));
        CHECK(!vfp_exists(res));
        /* a read from byte 1000 */
        t = 0;
        st = vfp_open_read(done, &t, &est, &f);
        CHECK(st & 1);
        if (st & 1) {
            CHECK(vfp_skip(f, 1000) & 1);
            n = 0;
            while (n < (int) sizeof back && (vfp_read(f, back + n, (int) sizeof back - n, &got) & 1)) n += got;
            vfp_close(f, 1);
            CHECK(n == 500 && !memcmp(back, big + 1000, 500));
        }
        CHECK(vfp_delete(done) & 1);
    }
    vfp_ssh_file("known_hosts", kh, sizeof kh);
    printf("known_hosts %s exists %d\n", kh, vfp_exists(kh));
    CHECK(vfp_exists(kh));
    printf("PLAT: %d checks, %d failed\n", checks, fails);
    return fails ? 44 : 1;
}
