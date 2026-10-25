/* vf_main.c - VFTP [[user@]host[:port]]   (run as a foreign command)
   With a host, the Open form starts filled in; without, it starts empty. */
#include <stdio.h>
#include <string.h>
#include <ssdef.h>
#include "vf_app.h"

int main(int argc, char **argv)
{
    static vf_app app;
    unsigned int st;
    int rows, cols;
    char msg[256];

    /* MAKE_KIT checks the image's version this way, without a terminal. */
    if (argc > 1 && (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version"))) {
        printf("vftp %s\n", VF_VERSION);
        return SS$_NORMAL;
    }
    st = vfp_start(&app.sess);
    if (!(st & 1)) {
        vfp_message(st, msg, sizeof msg);
        fprintf(stderr, "VFTP: cannot start: %s\n", msg);
        return st;
    }
    vfp_screen_size(&rows, &cols);
    if (rows < 20 || cols < 80) {
        vfp_stop();
        fprintf(stderr, "VFTP: needs at least 20 rows and 80 columns (this terminal: %d x %d)\n", rows, cols);
        return SS$_ABORT;
    }
    if (!vf_app_init(&app, argc > 1 ? argv[1] : 0, rows, cols)) {
        vfp_stop();
        fprintf(stderr, "VFTP: out of memory\n");
        return SS$_ABORT;
    }
    vf_app_run(&app);
    vf_app_free(&app);
    vfp_stop();
    return SS$_NORMAL;
}
