/* probe_rename.c - resume's local pieces (D16), in the current directory (a scratch one):
   1. sys$rename of a temporary file onto A.BIN, which has a version already, with the
      new name given as A.BIN, A.BIN; and A.BIN;0, and onto N.BIN, which has none;
      each prints the status and the resulting name.
   2. A block-I/O file of 2 blocks, closed, opened again ($OPEN, PUT|BIO) and written
      at VBN 3 and 4 beyond its allocation, as a resumed Get would write. */
#include <stdio.h>
#include <string.h>
#include <rms.h>
#include <starlet.h>
#include "probe_common.h"

static unsigned int make(const char *name, int blocks, int bio_open)
{
    static char buf[512];
    struct FAB fab = cc$rms_fab;
    struct RAB rab = cc$rms_rab;
    unsigned int st;
    int i;
    fab.fab$l_fna = (char *) name; fab.fab$b_fns = (unsigned char) strlen(name);
    fab.fab$b_fac = FAB$M_PUT | FAB$M_BIO;
    fab.fab$b_rfm = FAB$C_FIX; fab.fab$w_mrs = 512;
    fab.fab$l_fop = FAB$M_MXV | FAB$M_TEF; fab.fab$l_alq = (unsigned int) blocks;
    st = bio_open ? sys$open(&fab, 0, 0) : sys$create(&fab, 0, 0);
    if (!(st & 1)) return st;
    rab.rab$l_fab = &fab; rab.rab$l_rop = RAB$M_BIO;
    st = sys$connect(&rab, 0, 0);
    for (i = 0; (st & 1) && i < blocks; i++) {
        memset(buf, 'A' + i + (bio_open ? 2 : 0), sizeof buf);
        rab.rab$l_rbf = buf; rab.rab$w_rsz = 512;
        rab.rab$l_bkt = (unsigned int) (i + 1 + (bio_open ? 2 : 0));
        st = sys$write(&rab, 0, 0);
    }
    if (st & 1) { unsigned int cs = sys$close(&fab, 0, 0); if (!(cs & 1)) st = cs; }
    else sys$close(&fab, 0, 0);
    return st;
}

static void ren(const char *from, const char *to)
{
    static char rsa[256];
    struct FAB of = cc$rms_fab, nf = cc$rms_fab;
    struct NAM nn = cc$rms_nam;
    unsigned int st;
    of.fab$l_fna = (char *) from; of.fab$b_fns = (unsigned char) strlen(from);
    nf.fab$l_fna = (char *) to; nf.fab$b_fns = (unsigned char) strlen(to);
    nn.nam$l_rsa = rsa; nn.nam$b_rss = sizeof rsa - 1; nf.fab$l_nam = &nn;
    st = sys$rename(&of, 0, 0, &nf);
    rsa[nn.nam$b_rsl] = '\0';
    printf("RENAME: %s -> \"%s\": %s %s result %s\n", from, to, (st & 1) ? "OK" : "FAIL", msg(st), rsa);
}

int main(void)
{
    report("make A.BIN", make("A.BIN", 1, 0));
    report("make P1.TMP P2.TMP P3.TMP P4.TMP", make("P1.TMP", 1, 0) & make("P2.TMP", 1, 0)
           & make("P3.TMP", 1, 0) & make("P4.TMP", 1, 0));
    ren("P1.TMP", "A.BIN");
    ren("P2.TMP", "A.BIN;");
    ren("P3.TMP", "A.BIN;0");
    ren("P4.TMP", "N.BIN");
    report("make Q.BIN, 2 blocks", make("Q.BIN", 2, 0));
    report("open Q.BIN again, write VBN 3-4", make("Q.BIN", 2, 1));
    return 0;
}
