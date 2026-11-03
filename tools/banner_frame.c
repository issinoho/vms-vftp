/* banner_frame.c - VFTP's two panes, as the terminal would get them, for the
   README banner: the app against the Linux stubs (a made-up VMS disk and the
   stub server test.example), connected by SFTP, two local files tagged, written
   by vf_ansi in colour. Built and run by tools/banner.sh; no real host, user
   or file goes into the picture. */
#include <stdio.h>
#include <string.h>
#include "vf_app.h"
#include "vf_ansi.h"
#include "vf_stub.h"

static void out(void *ctx, const char *b, int n) { fwrite(b, 1, (size_t) n, (FILE *) ctx); }

static char blob[40000];

static void go(vf_app *a, int pane, const char *name)
{
    vf_pane *p = &a->pane[pane];
    int i;
    a->act = pane;
    for (i = 0; i < p->nvis; i++)
        if (!strcmp(p->e[p->vis[i]].name, name)) { p->cur = i; return; }
}

static void keys(vf_app *a, const char *s) { for (; *s; s++) vf_app_key(a, (unsigned char) *s); }

int main(void)
{
    static vf_app a;
    const char *L = "STUB$DKA0:[USERS.TEST]";
    stub_reset();
    stub_set_size(24, 80);
    stub_trust(1);
    stub_add_local(L, "BUILD.LOG", 1, blob, 9000);
    stub_add_local(L, "REPORT.TXT", 1, blob, 3100);
    stub_add_local(L, "BACKUP.BCK", 0, blob, 32768);
    stub_add_local(L, "SITES.DAT", 1, blob, 400);
    stub_add_remote_dir("/home/test/src");
    stub_add_remote_dir("/home/test/releases");
    stub_add_remote("/home/test/report.txt", blob, 3100);
    stub_add_remote("/home/test/backup.bck", blob, 32768);
    stub_add_remote("/home/test/build.log", blob, 9000);
    vfp_start(&a.sess);
    if (!vf_app_init(&a, "test@test.example", 24, 80)) return 1;
    keys(&a, "secret");
    vf_app_key(&a, VK_DO);
    if (!a.rem) return 1;
    go(&a, PANE_LOCAL, "REPORT.TXT;1"); vf_app_key(&a, ' ');
    go(&a, PANE_LOCAL, "BACKUP.BCK;1"); vf_app_key(&a, ' ');
    go(&a, PANE_LOCAL, "BACKUP.BCK;1");
    vf_app_draw(&a);
    a.scr.cursor_on = 0;
    vf_ansi_begin(1, out, stdout);
    vf_ansi_render(&a.scr, 0, 1, out, stdout);
    vf_app_free(&a);
    return 0;
}
