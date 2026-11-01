/* test_app.c - the whole app against the stubs (a VMS disk and an SFTP server in
   memory): keys in, screens and files out. Each snapshot is compared with
   tests/snap/<name>.txt; run with VF_UPDATE_SNAPSHOTS=1 to write them (then read
   every changed one). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vf_app.h"
#include "vf_stub.h"
#include "vf_util.h"

static int checks, fails;
static const char *snapdir = "tests/snap";
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKS(a, b) do { checks++; if (strcmp((a), (b))) { fails++; \
    printf("FAIL %s:%d: %s\n   got  [%s]\n   want [%s]\n", __FILE__, __LINE__, #a, (a), (b)); } } while (0)

static void snap(vf_app *a, const char *name)
{
    static char text[65536], want[65536];
    char path[256];
    FILE *f;
    size_t n;
    vf_app_draw(a);
    vf_screen_text(&a->scr, text, sizeof text);
    snprintf(path, sizeof path, "%s/%s.txt", snapdir, name);
    checks++;
    if (getenv("VF_UPDATE_SNAPSHOTS")) {
        f = fopen(path, "w");
        if (f) { fputs(text, f); fclose(f); }
        return;
    }
    f = fopen(path, "r");
    if (!f) { fails++; printf("FAIL snapshot %s missing\n", path); return; }
    n = fread(want, 1, sizeof want - 1, f);
    want[n] = '\0';
    fclose(f);
    if (strcmp(text, want)) { fails++; printf("FAIL snapshot %s differs; got:\n%s\n", name, text); }
}

static void key(vf_app *a, int k) { vf_app_key(a, k); vf_app_draw(a); }
static void type(vf_app *a, const char *s) { for (; *s; s++) key(a, (unsigned char) *s); }
static void erase(vf_app *a) { key(a, VK_CTRL_E); key(a, VK_CTRL_U); }

static void go(vf_app *a, int pane, const char *name)
{
    vf_pane *p = &a->pane[pane];
    int i;
    a->act = pane;
    for (i = 0; i < p->nvis; i++)
        if (!strcmp(p->e[p->vis[i]].name, name)) { p->cur = i; return; }
    printf("FAIL: %s not in pane %d\n", name, pane);
    fails++;
}

static vf_app app;

static void start(int rows, int cols, const char *target)
{
    vf_app_free(&app);
    stub_reset();
    stub_set_size(rows, cols);
    vfp_start(&app.sess);
    CHECK(vf_app_init(&app, target, rows, cols));
}

static void test_connect(void)
{
    vf_app *a = &app;
    int len;
    const char *kh;
    start(24, 80, 0);
    CHECK(a->mode == M_FORM && a->fcur == FF_HOST);
    snap(a, "01_open_form");
    key(a, VK_F10);
    CHECK(a->mode == M_MAIN && !a->rem);
    snap(a, "02_local_only");
    key(a, VK_TAB);
    CHECK(a->act == PANE_LOCAL);               /* nothing to switch to */

    /* the wrong password, then the right one */
    key(a, 'O');
    erase(a); type(a, "test.example");
    key(a, VK_DOWN); key(a, VK_DOWN); key(a, VK_DOWN);
    type(a, "wrong");
    key(a, VK_DO);
    CHECK(a->mode == M_CONFIRM && a->ask == ASK_HOSTKEY);
    snap(a, "03_hostkey");
    key(a, 'Y');
    CHECK(a->mode == M_FORM && a->fcur == FF_PASSWORD && !a->rem);
    CHECK(strstr(a->form_err, "Login refused") != 0);
    CHECK(a->fv[FF_PASSWORD][0] == '\0');     /* wiped after use */
    snap(a, "04_login_refused");
    kh = stub_local("STUB$DKA0:[USERS.TEST.SSH]KNOWN_HOSTS.", &len, 0, 0);
    CHECK(kh && strstr(kh, "test.example ecdsa-sha2-nistp256"));
    type(a, "secret");
    key(a, VK_DO);                              /* known now: no question */
    CHECK(a->mode == M_MAIN && a->rem && a->act == PANE_REMOTE);
    CHECKS(a->pane[PANE_REMOTE].dir, "/home/test");
    snap(a, "05_connected");
    CHECK(strstr(stub_log(), "LOGIN test@test.example:22 by password") != 0);
    CHECK(strstr(stub_log(), "HOSTKEY ADDED test.example") != 0);
    CHECK(!strstr(stub_log(), "secret") && !strstr(stub_log(), "wrong"));
}

/* FTP (D10): the port follows the protocol; no password goes in clear unless the
   user says Y; FTP has no key login */
static void test_ftp_login(void)
{
    vf_app *a = &app;
    start(24, 80, 0);
    key(a, VK_UP);                               /* Host -> Protocol */
    CHECK(a->fcur == FF_PROTO);
    erase(a); type(a, "ftp");
    key(a, VK_DOWN);
    CHECKS(a->fv[FF_PROTO], "FTP");
    CHECKS(a->fv[FF_PORT], "21");               /* followed the protocol */
    erase(a); type(a, "test.example");
    snap(a, "16_ftp_form");
    key(a, VK_DO);                               /* no password: FTP needs one */
    CHECK(a->mode == M_FORM && a->fcur == FF_PASSWORD && !a->rem);
    CHECK(strstr(a->form_err, "FTP logs in with a password") != 0);
    type(a, "secret");
    key(a, VK_DO);
    CHECK(a->mode == M_CONFIRM && a->ask == ASK_CLEARPW);
    snap(a, "17_ftp_clear_password");
    key(a, VK_RETURN);                           /* the default: No */
    CHECK(a->mode == M_FORM && !a->rem && a->fv[FF_PASSWORD][0] == '\0');
    CHECK(strstr(a->form_err, "password was not sent") != 0);
    CHECK(!strstr(stub_log(), "LOGIN test@test.example:21"));
    type(a, "secret");
    key(a, VK_DO);
    CHECK(a->ask == ASK_CLEARPW);
    key(a, 'Y');
    CHECK(a->mode == M_MAIN && a->rem && a->proto == VFR_FTP);
    CHECKS(a->pane[PANE_REMOTE].dir, "/home/test");
    CHECK(strstr(stub_log(), "CLEAR PASSWORD agreed for test@test.example:21") != 0);
    CHECK(strstr(stub_log(), "LOGIN test@test.example:21 by password") != 0);
    CHECK(!strstr(stub_log(), "secret"));
    /* back to SFTP: the port follows again; a typed port stays */
    key(a, 'O');
    { int i; for (i = 0; i < FF_COUNT; i++) key(a, VK_UP); }   /* to the top: Protocol */
    erase(a); type(a, "sftp"); key(a, VK_DOWN);
    CHECKS(a->fv[FF_PORT], "22");
    key(a, VK_DOWN); erase(a); type(a, "2222"); key(a, VK_UP); key(a, VK_UP);
    erase(a); type(a, "FTP"); key(a, VK_DOWN);
    CHECKS(a->fv[FF_PORT], "2222");
    key(a, VK_F10);

    /* FTPS (D12): the certificate is asked about first, as an SSH key; then no
       question about the password, which goes over TLS */
    start(24, 80, 0);
    stub_ftps(1);
    key(a, VK_UP); erase(a); type(a, "FTP"); key(a, VK_DOWN);
    erase(a); type(a, "test.example");
    key(a, VK_DOWN); key(a, VK_DOWN); key(a, VK_DOWN);
    type(a, "secret");
    key(a, VK_DO);
    CHECK(a->mode == M_CONFIRM && a->ask == ASK_HOSTKEY);
    snap(a, "18_ftps_certificate");
    key(a, 'Y');
    CHECK(a->mode == M_MAIN && a->rem && a->proto == VFR_FTP);
    CHECK(!strstr(stub_log(), "CLEAR PASSWORD"));
    CHECK(strstr(stub_log(), "LOGIN test@test.example:21 by password") != 0);
    stub_ftps(0);
}

static void test_get_put(void)
{
    vf_app *a = &app;
    int len, text, vers;
    const char *d;
    start(24, 80, "test@test.example");
    CHECKS(a->fv[FF_HOST], "test.example");
    CHECK(a->fcur == FF_PASSWORD);
    type(a, "secret"); key(a, VK_DO); key(a, 'Y');
    CHECK(a->rem != 0);

    /* a text file: LF lines, the CR dropped, a new version */
    go(a, PANE_REMOTE, "notes.txt");
    key(a, 'G');
    d = stub_local("STUB$DKA0:[USERS.TEST]NOTES.TXT", &len, &text, &vers);
    CHECK(d && text && vers == 3);
    CHECK(d && len == 22 && !memcmp(d, "remote notes\nline two\n", 22));
    CHECK(strstr(a->msg, "Got 1 file") != 0);
    snap(a, "06_after_get");

    /* a binary file: the bytes as they are */
    go(a, PANE_REMOTE, "kit.zip");
    key(a, 'G');
    d = stub_local("STUB$DKA0:[USERS.TEST]KIT.ZIP", &len, &text, 0);
    CHECK(d && !text && len == 2000);
    CHECK(d && (unsigned char) d[1999] == 1999 % 251);
    CHECK(strstr(stub_log(), "GET /home/test/kit.zip -> STUB$DKA0:[USERS.TEST]KIT.ZIP;1 BINARY 2000 bytes") != 0);

    /* forcing ASCII */
    key(a, 'X');
    CHECK(a->xmode == X_ASCII);
    go(a, PANE_REMOTE, "README.md");
    key(a, 'G');
    d = stub_local("STUB$DKA0:[USERS.TEST]README.MD", &len, &text, 0);
    CHECK(d && text);
    key(a, 'X'); key(a, 'X');
    CHECK(a->xmode == X_AUTO);

    /* Put a text file: records become LF lines; the remote name is lower case */
    go(a, PANE_LOCAL, "LOGIN.COM;1");
    key(a, 'P');
    d = stub_remote("/home/test/login.com", &len);
    CHECK(d && len == 23 && !memcmp(d, "$ set noon\n$ show time\n", 23));
    /* Put a binary file: bytes */
    go(a, PANE_LOCAL, "KIT.BCK;1");
    key(a, 'C');                                  /* Copy from the local pane = Put */
    d = stub_remote("/home/test/kit.bck", &len);
    CHECK(d && len == 1536 && (unsigned char) d[100] == (unsigned char) (100 * 7));

    /* Put over an existing file asks first */
    go(a, PANE_LOCAL, "NOTES.TXT;3");
    key(a, 'P');
    CHECK(a->mode == M_CONFIRM && a->ask == ASK_REPLACE);
    snap(a, "07_replace_confirm");
    key(a, 'n');
    CHECK(a->mode == M_MAIN);
    d = stub_remote("/home/test/notes.txt", &len);
    CHECK(d && len == 23);                         /* untouched */
    key(a, VK_PF1); key(a, 'V');                  /* every version, to pick ;2 */
    go(a, PANE_LOCAL, "NOTES.TXT;2");
    key(a, 'P'); key(a, 'Y');
    key(a, VK_PF1); key(a, 'V');
    d = stub_remote("/home/test/notes.txt", &len);
    CHECK(d && len == 24 && !memcmp(d, "Shopping list\n", 14));

    /* two tagged files at once */
    go(a, PANE_LOCAL, "LOGIN.COM;1"); key(a, ' ');
    go(a, PANE_LOCAL, "KIT.BCK;1"); key(a, ' ');
    go(a, PANE_LOCAL, "KIT.BCK;1");
    snap(a, "08_tagged");
    key(a, 'P');
    CHECK(a->mode == M_CONFIRM);                   /* both are there already */
    key(a, 'Y');
    CHECK(strstr(a->msg, "Put 2 files") != 0);
    CHECK(!a->pane[PANE_LOCAL].e[0].tagged);
}

static void test_cancel(void)
{
    vf_app *a = &app;
    int vers = 0;
    start(24, 80, "test@test.example");
    type(a, "secret"); key(a, VK_DO); key(a, 'Y');
    go(a, PANE_REMOTE, "kit.zip");
    stub_cancel_after(2);
    key(a, 'G');
    CHECK(strstr(a->msg, "Cancelled") != 0);
    CHECK(!stub_local("STUB$DKA0:[USERS.TEST]KIT.ZIP", 0, 0, &vers) && vers == 0);
    CHECK(strstr(stub_log(), "FAILED: cancelled") != 0);
    /* a cancelled Put leaves no remote file */
    go(a, PANE_LOCAL, "KIT.BCK;1");
    stub_cancel_after(2);
    key(a, 'P');
    CHECK(!stub_remote("/home/test/kit.bck", 0));
    /* a cancelled Put that was to replace a file (a server without versions) keeps
       the old file: the new one goes to a temporary name, renamed over it at the end */
    {
        int len;
        const char *d;
        stub_add_remote("/home/test/kit.bck", "old copy", 8);
        a->act = PANE_REMOTE; key(a, 'J'); key(a, VK_RETURN);       /* re-read the server */
        go(a, PANE_LOCAL, "KIT.BCK;1");
        stub_cancel_after(2);
        key(a, 'P');
        CHECK(a->mode == M_CONFIRM && a->ask == ASK_REPLACE);
        key(a, 'Y');
        CHECK(strstr(a->msg, "Cancelled") != 0);
        d = stub_remote("/home/test/kit.bck", &len);
        CHECK(d && len == 8 && !memcmp(d, "old copy", 8));
        CHECK(!stub_remote_like("/home/test/kit.bck."));
        /* and one that completes replaces it, leaving no temporary file */
        key(a, 'P'); key(a, 'Y');
        d = stub_remote("/home/test/kit.bck", &len);
        CHECK(d && len == 1536);
        CHECK(!stub_remote_like("/home/test/kit.bck."));
    }
}

static void test_manage(void)
{
    vf_app *a = &app;
    int len;
    start(24, 80, "test@test.example");
    type(a, "secret"); key(a, VK_DO); key(a, 'Y');

    /* delete on the server */
    go(a, PANE_REMOTE, "README.md");
    key(a, 'D');
    CHECK(a->mode == M_CONFIRM && a->ask == ASK_DELETE);
    snap(a, "09_delete_confirm");
    key(a, 'Y');
    CHECK(!stub_remote("/home/test/README.md", 0));
    CHECK(strstr(a->msg, "1 deleted") != 0);

    /* make a directory, go in, come back up */
    key(a, 'M'); type(a, "new"); key(a, VK_RETURN);
    CHECK(stub_remote("/home/test/new", 0) != 0);
    CHECKS(vf_pane_cur(&a->pane[PANE_REMOTE])->name, "new");
    key(a, VK_RETURN);
    CHECKS(a->pane[PANE_REMOTE].dir, "/home/test/new");
    key(a, VK_LEFT);
    CHECKS(a->pane[PANE_REMOTE].dir, "/home/test");
    CHECKS(vf_pane_cur(&a->pane[PANE_REMOTE])->name, "new");
    key(a, 'D'); key(a, 'Y');                   /* an empty directory goes */
    CHECK(!stub_remote("/home/test/new", 0));

    /* rename on the server */
    go(a, PANE_REMOTE, "notes.txt");
    key(a, 'R'); erase(a); type(a, "old-notes.txt"); key(a, VK_RETURN);
    CHECK(stub_remote("/home/test/old-notes.txt", &len) && len == 23);
    CHECKS(vf_pane_cur(&a->pane[PANE_REMOTE])->name, "old-notes.txt");

    /* local: delete one version only, after a No */
    go(a, PANE_LOCAL, "NOTES.TXT;2");
    key(a, VK_REMOVE); key(a, 'x');
    CHECK(stub_local("STUB$DKA0:[USERS.TEST]NOTES.TXT;2", 0, 0, 0) != 0);
    key(a, VK_REMOVE); key(a, 'y');
    CHECK(!stub_local("STUB$DKA0:[USERS.TEST]NOTES.TXT;2", 0, 0, 0));
    CHECK(stub_local("STUB$DKA0:[USERS.TEST]NOTES.TXT;1", 0, 0, 0) != 0);
    /* local: into WORK and back; jump */
    go(a, PANE_LOCAL, "WORK.DIR;1");
    key(a, VK_RETURN);
    CHECKS(a->pane[PANE_LOCAL].dir, "STUB$DKA0:[USERS.TEST.WORK]");
    key(a, '-');
    CHECKS(a->pane[PANE_LOCAL].dir, "STUB$DKA0:[USERS.TEST]");
    CHECKS(vf_pane_cur(&a->pane[PANE_LOCAL])->name, "WORK.DIR;1");
    key(a, 'J'); erase(a); type(a, "[.SSH]"); key(a, VK_RETURN);
    CHECKS(a->pane[PANE_LOCAL].dir, "STUB$DKA0:[USERS.TEST.SSH]");
    key(a, 'J'); erase(a); type(a, "[NOWHERE]"); key(a, VK_RETURN);
    CHECK(a->msg_err && strstr(a->msg, "DNF"));
    key(a, VK_PF1); key(a, 'V');                /* every version */
    CHECK(a->all_versions);
}

static void test_screens(void)
{
    vf_app *a = &app;
    start(24, 80, "test@test.example");
    type(a, "secret"); key(a, VK_DO); key(a, 'Y');
    go(a, PANE_REMOTE, "notes.txt");
    key(a, VK_RETURN);
    CHECK(a->mode == M_VIEW && a->ntext == 2);
    snap(a, "10_view");
    key(a, ' ');
    go(a, PANE_REMOTE, "kit.zip");
    key(a, 'V');
    CHECK(a->mode == M_MAIN && a->msg_err);      /* binary: not shown */
    key(a, 'A');
    snap(a, "11_attributes");
    key(a, ' ');
    key(a, VK_HELP);
    snap(a, "12_help");
    key(a, ' ');
    go(a, PANE_LOCAL, "KIT.BCK;1");
    key(a, 'V');
    CHECK(a->mode == M_MAIN && strstr(a->msg, "not a text file"));
    key(a, VK_PF1);
    snap(a, "13_gold");
    key(a, 'O');
    CHECK(!a->rem && a->act == PANE_LOCAL);

    /* a server whose key changed is refused */
    stub_change_key(1);
    key(a, 'O'); key(a, VK_DO);
    CHECK(a->mode == M_FORM && !a->rem && strstr(a->form_err, "REFUSED"));
    snap(a, "14_key_changed");
    CHECK(strstr(stub_log(), "HOSTKEY MISMATCH") != 0);
    stub_change_key(0);

    start(30, 132, "test@test.example");
    type(a, "secret"); key(a, VK_DO); key(a, 'Y');
    snap(a, "15_wide");
}

/* Directories go across whole (D13) */
static void test_dirs(void)
{
    vf_app *a = &app;
    int len, vers = 0, i, found = 0;
    const char *d;
    start(24, 80, "test@test.example");
    type(a, "secret"); key(a, VK_DO); key(a, 'Y');
    stub_add_remote_dir("/home/test/data/v1.2");
    stub_add_remote("/home/test/data/v1.2/deep.txt", "deep\n", 5);
    stub_add_remote_link("/home/test/data/loop");
    a->act = PANE_REMOTE; key(a, 'J'); key(a, VK_RETURN);           /* re-read the server */

    /* cancelled while the directories are read: nothing made */
    go(a, PANE_REMOTE, "data");
    stub_cancel_after(1);
    key(a, 'G');
    CHECK(strstr(a->msg, "Cancelled while reading") != 0);
    CHECK(!strstr(stub_log(), "MKDIR"));

    /* Get a directory: what is in it, a dotted name escaped, the link not followed */
    go(a, PANE_REMOTE, "data");
    key(a, 'G');
    d = stub_local("STUB$DKA0:[USERS.TEST.DATA]BIG.LOG", &len, 0, 0);
    CHECK(d && len == 2 && !memcmp(d, "x\n", 2));
    d = stub_local("STUB$DKA0:[USERS.TEST.DATA.V1^.2]DEEP.TXT", &len, 0, 0);
    CHECK(d && len == 5);
    CHECK(strstr(a->msg, "Got 2 files, 2 directories made, 7 bytes") != 0);
    CHECK(strstr(a->msg, "; 1 link to a directory not followed") != 0);
    CHECK(strstr(stub_log(), "MKDIR STUB$DKA0:[USERS.TEST.data]\n") != 0);
    CHECK(vf_pane_find(&a->pane[PANE_LOCAL], "DATA.DIR;1") != 0);
    /* again: the directories are there, the files new versions (D8) */
    go(a, PANE_REMOTE, "data");
    key(a, 'G');
    CHECK(strstr(a->msg, "Got 2 files, 7 bytes") != 0);
    CHECK(stub_local("STUB$DKA0:[USERS.TEST.DATA]BIG.LOG", 0, 0, &vers) && vers == 2);

    /* Put a directory */
    go(a, PANE_LOCAL, "WORK.DIR;1");
    key(a, 'P');
    d = stub_remote("/home/test/work/plan.txt", &len);
    CHECK(d && len == 8 && !memcmp(d, "one\ntwo\n", 8));
    CHECK(strstr(a->msg, "Put 1 file, 1 directory made") != 0);
    /* again: a file below would be replaced, so the confirm screen names it */
    stub_add_local("STUB$DKA0:[USERS.TEST.WORK]", "NEW.TXT", 1, "new\n", 4);
    go(a, PANE_LOCAL, "WORK.DIR;1");
    key(a, 'P');
    CHECK(a->mode == M_CONFIRM && a->ask == ASK_REPLACE);
    for (i = 0; i < a->ntext; i++) if (strstr(a->text[i], "work/plan.txt") && strstr(a->text[i], "(PLAN.TXT;1)")) found = 1;
    CHECK(found);
    snap(a, "19_dir_replace");
    key(a, 'n');
    CHECK(!stub_remote("/home/test/work/new.txt", 0));             /* nothing at all was put */
    go(a, PANE_LOCAL, "WORK.DIR;1");
    key(a, 'P'); key(a, 'Y');
    CHECK(stub_remote("/home/test/work/new.txt", 0) != 0);
    CHECK(!stub_remote_like("/home/test/work/plan.txt."));
    CHECK(strstr(a->msg, "Put 2 files,") != 0);

    /* a file on the server where the directory would go: nothing is copied */
    stub_add_remote("/home/test/ssh", "a file", 6);
    a->act = PANE_REMOTE; key(a, 'J'); key(a, VK_RETURN);
    go(a, PANE_LOCAL, "SSH.DIR;1");
    key(a, 'P');
    CHECK(strstr(a->msg, "ssh is a file on the server") != 0);
    CHECK(!stub_remote_like("/home/test/ssh/"));

    /* tagging takes directories too (Space), and the tag goes when it is copied */
    go(a, PANE_REMOTE, "data"); key(a, ' ');
    go(a, PANE_REMOTE, "kit.zip"); key(a, ' ');
    key(a, 'G');
    CHECK(strstr(a->msg, "Got 3 files") != 0);
    CHECK(!vf_pane_find(&a->pane[PANE_REMOTE], "data")->tagged);
}

/* The sites file (D9, D14) */
static void test_sites(void)
{
    vf_app *a = &app;
    const char *d;
    int len, vers = 0;
    const char *file = "STUB$DKA0:[USERS.TEST]VFTP.SITES";
    start(24, 80, "test@test.example");
    type(a, "secret"); key(a, VK_DO); key(a, 'Y');
    CHECK(a->rem != 0);
    key(a, 'L');
    CHECK(a->mode == M_SITES && a->sites.n == 0);
    snap(a, "20_sites_empty");
    /* A: the Open form as a site, never the password */
    key(a, 'A');
    CHECK(a->prompt == P_SITE && !strcmp(a->pedit.buf, "test.example"));
    erase(a); type(a, "home"); key(a, VK_RETURN);
    d = stub_local(file, &len, 0, &vers);
    CHECK(d && vers == 1);
    CHECK(d && strstr(d, "[home]\nprotocol = SFTP\nhost = test.example\nport = 22\nuser = test\n") != 0);
    CHECK(d && strstr(d, "local = STUB$DKA0:[USERS.TEST]\n") != 0);
    CHECK(d && !strstr(d, "secret") && !strstr(d, "assword ="));
    CHECK(strstr(a->msg, "Saved the site home") != 0);
    snap(a, "21_sites");
    /* the same name again asks first; N leaves the file */
    key(a, 'A'); key(a, VK_RETURN);
    CHECK(a->prompt == P_ASK && a->ask == ASK_SITE_REPLACE);
    key(a, 'n');
    CHECK(a->mode == M_SITES && stub_local(file, 0, 0, &vers) && vers == 1);
    key(a, 'A'); key(a, VK_RETURN); key(a, 'y');
    CHECK(stub_local(file, 0, 0, &vers) && vers == 2);          /* a new version */
    /* Return: the Open form filled, the cursor on the password */
    key(a, VK_F10);
    key(a, VK_PF1); key(a, 'O');                                 /* close the connection */
    CHECK(!a->rem);
    key(a, 'L'); key(a, VK_RETURN);
    CHECK(a->mode == M_FORM && a->fcur == FF_PASSWORD && !strcmp(a->fv[FF_HOST], "test.example"));
    CHECK(a->fv[FF_PASSWORD][0] == '\0');
    snap(a, "22_site_form");
    type(a, "secret"); key(a, VK_DO);
    CHECK(a->rem != 0);
    /* D asks; Y deletes */
    key(a, 'L'); key(a, 'D');
    CHECK(a->prompt == P_ASK && a->ask == ASK_SITE_DELETE);
    key(a, 'x');
    CHECK(a->sites.n == 1);
    key(a, 'D'); key(a, 'Y');
    CHECK(a->sites.n == 0 && strstr(a->msg, "Deleted the site home") != 0);
    d = stub_local(file, &len, 0, &vers);
    CHECK(d && vers == 3 && !strstr(d, "[home]"));

    /* a line VFTP cannot read: the rest is used, nothing is saved over it */
    {
        const char *t = "[old]\nhost = test.example\npassword = hunter2\n";
        stub_add_local("STUB$DKA0:[USERS.TEST]", "VFTP.SITES", 1, t, (int) strlen(t));
    }
    key(a, VK_F10); key(a, 'L');
    CHECK(a->sites.n == 1 && a->sites.bad == 1);
    CHECK(strstr(a->msg, "line 3: passwords are never kept") != 0);
    key(a, 'A'); erase(a); type(a, "new"); key(a, VK_RETURN);
    CHECK(strstr(a->msg, "Nothing saved") != 0);
    CHECK(stub_local(file, 0, 0, &vers) && vers == 4);

    /* "vftp work": the site by name from the command line */
    vf_app_free(a);
    stub_reset();
    {
        const char *t = "[work]\nhost = test.example\nuser = test\nremote = /home/test/data\nlocal = [.WORK]\n";
        stub_add_local("STUB$DKA0:[USERS.TEST]", "VFTP.SITES", 1, t, (int) strlen(t));
    }
    vfp_start(&a->sess);
    CHECK(vf_app_init(a, "work", 24, 80));
    CHECK(a->mode == M_FORM && a->fcur == FF_PASSWORD && !strcmp(a->fv[FF_DIR], "/home/test/data"));
    CHECKS(a->pane[PANE_LOCAL].dir, "STUB$DKA0:[USERS.TEST.WORK]");
    type(a, "secret"); key(a, VK_DO); key(a, 'Y');
    CHECKS(a->pane[PANE_REMOTE].dir, "/home/test/data");
}

/* GOLD A: a server's permissions */
static void test_chmod(void)
{
    vf_app *a = &app;
    start(24, 80, "test@test.example");
    type(a, "secret"); key(a, VK_DO); key(a, 'Y');
    go(a, PANE_REMOTE, "notes.txt");
    key(a, VK_PF1); key(a, 'A');
    CHECK(a->prompt == P_CHMOD && !strcmp(a->pedit.buf, "644"));
    snap(a, "23_chmod_prompt");
    erase(a); type(a, "600"); key(a, VK_RETURN);
    CHECK(vf_pane_find(&a->pane[PANE_REMOTE], "notes.txt")->perms == 0600);
    CHECKS(a->msg, "notes.txt: now -rw------- (0600)");
    CHECK(strstr(stub_log(), "CHMOD /home/test/notes.txt 0600") != 0);
    /* tagged, a change from each one's own */
    go(a, PANE_REMOTE, "notes.txt"); key(a, ' ');
    go(a, PANE_REMOTE, "kit.zip"); key(a, ' ');
    key(a, VK_PF1); key(a, 'A');
    CHECK(a->prompt == P_CHMOD && !a->pedit.buf[0]);
    type(a, "a+x"); key(a, VK_RETURN);
    CHECK(vf_pane_find(&a->pane[PANE_REMOTE], "notes.txt")->perms == 0711);
    CHECK(vf_pane_find(&a->pane[PANE_REMOTE], "kit.zip")->perms == 0755);
    CHECKS(a->msg, "Permissions changed on 2 entries");
    /* a mistake changes nothing */
    go(a, PANE_REMOTE, "kit.zip");
    key(a, VK_PF1); key(a, 'A'); erase(a); type(a, "rwz"); key(a, VK_RETURN);
    CHECK(strstr(a->msg, "Nothing changed") != 0);
    CHECK(vf_pane_find(&a->pane[PANE_REMOTE], "kit.zip")->perms == 0755);
    /* not on the local pane */
    a->act = PANE_LOCAL;
    key(a, VK_PF1); key(a, 'A');
    CHECK(a->prompt == P_NONE && strstr(a->msg, "SET SECURITY") != 0);
}

int main(void)
{
    test_connect();
    test_get_put();
    test_cancel();
    test_ftp_login();
    test_manage();
    test_dirs();
    test_sites();
    test_chmod();
    test_screens();
    vf_app_free(&app);
    printf("test_app: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
