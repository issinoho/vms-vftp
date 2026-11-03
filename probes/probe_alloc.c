/* probe_alloc.c - how long a Get's local open takes (vfp_open_write): sys$create of a
   block-I/O file with ALQ 20481 (10 MB), as a cancelled Get on IA64 FTP took 4 s to
   notice the key. Times the create, the first 32 KB written and the close (erased
   after), then the same with ALQ 0, in the current directory (a scratch one). */
#include <stdio.h>
#include <string.h>
#include <rms.h>
#include <starlet.h>
#include "probe_common.h"

static double now(void)
{
    unsigned int t[2];                     /* 100 ns units */
    sys$gettim((void *) t);
    return ((double) t[1] * 4294967296.0 + (double) t[0]) / 1e7;
}

static void one(const char *name, unsigned int alq)
{
    static char buf[32768];
    struct FAB fab = cc$rms_fab;
    struct RAB rab = cc$rms_rab;
    unsigned int st;
    double t0 = now(), t1, t2;
    fab.fab$l_fna = (char *) name; fab.fab$b_fns = (unsigned char) strlen(name);
    fab.fab$b_fac = FAB$M_PUT | FAB$M_BIO; fab.fab$b_rfm = FAB$C_FIX; fab.fab$w_mrs = 512;
    fab.fab$l_fop = FAB$M_MXV | FAB$M_TEF; fab.fab$w_deq = 64; fab.fab$l_alq = alq;
    st = sys$create(&fab, 0, 0);
    t1 = now();
    if (!(st & 1)) { report("create", st); return; }
    rab.rab$l_fab = &fab; rab.rab$l_rop = RAB$M_BIO;
    st = sys$connect(&rab, 0, 0);
    rab.rab$l_rbf = buf; rab.rab$w_rsz = sizeof buf; rab.rab$l_bkt = 1;
    if (st & 1) st = sys$write(&rab, 0, 0);
    t2 = now();
    fab.fab$l_fop |= FAB$M_DLT;
    sys$close(&fab, 0, 0);
    printf("ALLOC: ALQ %u: create %.3f s, first 32 KB %.3f s, close %.3f s (%s)\n",
           alq, t1 - t0, t2 - t1, now() - t2, msg(st));
}

int main(void)
{
    one("A.BIN", 20481); one("B.BIN", 0); one("C.BIN", 20481);
    return 0;
}
