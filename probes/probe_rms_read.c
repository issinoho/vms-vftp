/* probe_rms_read.c - which open options let $GET read a variable-record file
   (vf_plat_vms.c's vfp_open_read crashed in $GET on x86). P1 = file. */
#include <stdio.h>
#include <string.h>
#include <rms.h>
#include <starlet.h>

static void try(const char *spec, int bro, int fhc)
{
    struct FAB fab = cc$rms_fab;
    struct RAB rab = cc$rms_rab;
    struct XABFHC x = cc$rms_xabfhc;
    char buf[256];
    unsigned int st;
    fab.fab$l_fna = (char *) spec;
    fab.fab$b_fns = (unsigned char) strlen(spec);
    fab.fab$b_fac = FAB$M_GET | (bro ? FAB$M_BRO : 0);
    fab.fab$b_shr = FAB$M_SHRGET;
    if (fhc) fab.fab$l_xab = (void *) &x;
    st = sys$open(&fab, 0, 0);
    printf("bro %d fhc %d: open %X", bro, fhc, st);
    if (!(st & 1)) { printf("\n"); return; }
    rab.rab$l_fab = &fab;
    rab.rab$b_rac = RAB$C_SEQ;
    rab.rab$l_rop = RAB$M_RRL | RAB$M_NLK;
    st = sys$connect(&rab, 0, 0);
    printf(" connect %X", st);
    rab.rab$l_ubf = buf;
    rab.rab$w_usz = sizeof buf;
    fflush(stdout);
    st = sys$get(&rab, 0, 0);
    printf(" get %X len %d\n", st, rab.rab$w_rsz);
    sys$close(&fab, 0, 0);
}

int main(int argc, char **argv)
{
    try(argv[1], 0, 0);
    try(argv[1], 0, 1);
    try(argv[1], 1, 0);
    try(argv[1], 1, 1);
    return 1;
}
