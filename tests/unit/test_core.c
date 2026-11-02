/* test_core.c - the pure core: strings and dates, wildcards, the line editor,
   the screen model and its colour rendering. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vf_util.h"
#include "vf_edit.h"
#include "vf_screen.h"
#include "vf_ansi.h"
#include "vf_pane.h"
#include "vf_path.h"
#include "vf_text.h"
#include "vf_sites.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKS(a, b) do { checks++; if (strcmp((a), (b))) { fails++; \
    printf("FAIL %s:%d: %s\n   got  [%s]\n   want [%s]\n", __FILE__, __LINE__, #a, (a), (b)); } } while (0)

static void test_util(void)
{
    long d;
    char b[64];
    vf_when w = { 2026, 10, 25, 9, 5 };
    CHECK(vf_parse_date("30-APR-2027", &d) && d == vf_days(2027, 4, 30));
    CHECK(!vf_parse_date("29-FEB-2027", 0));
    CHECK(vf_parse_date("29-FEB-2028", 0));
    CHECK(vf_days(1970, 1, 1) == 0);
    CHECKS(vf_fmt_when(&w, b, sizeof b), "25-OCT-2026 09:05");
    CHECK(vf_match("*.TXT", "notes.txt"));
    CHECK(vf_match("LOGIN.%OM", "LOGIN.COM"));
    CHECK(!vf_match("*.C", "main.cpp"));
    vf_strlcpy(b, "  data  ", sizeof b);
    CHECKS(vf_trim(b), "data");
}

static void test_edit(void)
{
    vf_edit e;
    vf_edit_init(&e, "put");
    CHECK(vf_edit_key(&e, ' ') == VF_EDIT_MORE);
    vf_edit_key(&e, 'x');
    CHECKS(e.buf, "put x");
    vf_edit_key(&e, VK_DEL);
    vf_edit_key(&e, VK_CTRL_H);
    vf_edit_key(&e, 'g');
    CHECKS(e.buf, "gput ");
    CHECK(vf_edit_key(&e, VK_RETURN) == VF_EDIT_DONE);
    vf_edit_init(&e, "x");
    CHECK(vf_edit_key(&e, VK_F10) == VF_EDIT_CANCEL);
}

static void test_screen(void)
{
    vf_screen s;
    char text[512];
    vf_cell bar = { 'A', VF_A_REV, 0 }, txt = { 'A', 0, 0 };
    CHECK(vf_screen_init(&s, 4, 12));
    vf_box(&s, 0, 0, 3, 11);
    vf_vline(&s, 5, 0, 3);
    vf_gfx(&s, 0, 5, VF_G_TTEE, 0);       /* the app joins the lines itself */
    vf_gfx(&s, 3, 5, VF_G_BTEE, 0);
    vf_put(&s, 1, 1, "LOCAL", 0, 4);
    vf_put(&s, 1, 6, "REMOTE", 0, -1);
    vf_screen_text(&s, text, sizeof text);
    CHECKS(text, "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac"
                 "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90\n"
                 "\xe2\x94\x82LOCA\xe2\x94\x82REMOTE\n"
                 "\xe2\x94\x82    \xe2\x94\x82     \xe2\x94\x82\n"
                 "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4"
                 "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98\n");
    vf_screen_free(&s);
    /* the bar is reverse video in both modes, so it shows without colour (VLMF D10) */
    CHECKS(vf_ansi_sgr(&bar, 1), "0;36;40;7");
    CHECKS(vf_ansi_sgr(&bar, 0), "0;7");
    CHECKS(vf_ansi_sgr(&txt, 1), "0;37;44");
}

static void test_path(void)
{
    char b[512];
    vf_rjoin("/home/iain", "a.txt", b, sizeof b); CHECKS(b, "/home/iain/a.txt");
    vf_rjoin("/", "etc", b, sizeof b); CHECKS(b, "/etc");
    CHECK(vf_rparent("/home/iain/", b, sizeof b)); CHECKS(b, "/home");
    CHECK(vf_rparent("/home", b, sizeof b)); CHECKS(b, "/");
    CHECK(!vf_rparent("/", b, sizeof b)); CHECKS(b, "/");
    CHECKS(vf_rbase("/a/b.c"), "b.c");
    vf_lsubdir("DKA0:[IAIN]", "WORK", b, sizeof b); CHECKS(b, "DKA0:[IAIN.WORK]");
    vf_lsubdir("DKA0:[000000]", "IAIN", b, sizeof b); CHECKS(b, "DKA0:[IAIN]");
    CHECK(vf_lparent("DKA0:[IAIN.WORK]", b, sizeof b)); CHECKS(b, "DKA0:[IAIN]");
    CHECK(vf_lparent("DKA0:[IAIN]", b, sizeof b)); CHECKS(b, "DKA0:[000000]");
    CHECK(!vf_lparent("DKA0:[000000]", b, sizeof b));
    CHECK(vf_lparent("DKA0:[A.B^.C]", b, sizeof b)); CHECKS(b, "DKA0:[A]");
    vf_ldirname("SRC.DIR;1", b, sizeof b); CHECKS(b, "SRC");
    vf_lfile("DKA0:[IAIN]", "X.TXT;2", b, sizeof b); CHECKS(b, "DKA0:[IAIN]X.TXT;2");
    /* either kind of server */
    vf_xfile(0, "/home/a", "x.txt", b, sizeof b); CHECKS(b, "/home/a/x.txt");
    vf_xfile(1, "USER$ROOT:[IAIN]", "x.txt;2", b, sizeof b); CHECKS(b, "USER$ROOT:[IAIN]x.txt;2");
    vf_xsubdir(0, "/home/a", "data", b, sizeof b); CHECKS(b, "/home/a/data");
    vf_xsubdir(1, "USER$ROOT:[IAIN]", "SUB.DIR;1", b, sizeof b); CHECKS(b, "USER$ROOT:[IAIN.SUB]");
    vf_xsubdir(1, "USER$ROOT:[IAIN]", "^.CONFIG.DIR;1", b, sizeof b); CHECKS(b, "USER$ROOT:[IAIN.^.CONFIG]");
    CHECK(vf_xparent(1, "USER$ROOT:[IAIN.SUB]", b, sizeof b)); CHECKS(b, "USER$ROOT:[IAIN]");
    CHECK(vf_xparent(0, "/home/a", b, sizeof b)); CHECKS(b, "/home");
    vf_xleft(1, "USER$ROOT:[IAIN.VMS_VFTP]", b, sizeof b); CHECKS(b, "VMS_VFTP.DIR;1");
    vf_xleft(1, "USER$ROOT:[IAIN.^.CONFIG]", b, sizeof b); CHECKS(b, "^.CONFIG.DIR;1");
    vf_xleft(1, "USER$ROOT:[000000]", b, sizeof b); CHECKS(b, "");
    vf_xleft(0, "/home/a", b, sizeof b); CHECKS(b, "a");
    CHECK(!vf_local_name("notes.txt", 1, b, sizeof b)); CHECKS(b, "notes.txt");
    CHECK(vf_local_name("my file.tar.gz", 1, b, sizeof b)); CHECKS(b, "my^_file^.tar.gz");
    CHECK(!vf_local_name("build_vms.com;3", 1, b, sizeof b)); CHECKS(b, "build_vms.com");
    vf_local_name(".profile", 1, b, sizeof b); CHECKS(b, ".profile");
    CHECK(vf_local_name("my file.tar.gz", 0, b, sizeof b)); CHECKS(b, "MY_FILE_TAR.GZ");
    CHECK(!vf_local_name("notes.txt", 0, b, sizeof b)); CHECKS(b, "NOTES.TXT");
    CHECK(vf_version("R.TXT;3", b, sizeof b) == 3); CHECKS(b, "R.TXT");
    CHECK(vf_version("notes.txt", b, sizeof b) == -1); CHECKS(b, "notes.txt");
    CHECK(vf_version("odd;name", b, sizeof b) == -1); CHECKS(b, "odd;name");
    vf_dirbase("SUB.DIR;1", b, sizeof b); CHECKS(b, "SUB");
    vf_dirbase("work.DIR;1", b, sizeof b); CHECKS(b, "work");
    vf_dirbase("data", b, sizeof b); CHECKS(b, "data");
    vf_dirbase("my.dir", b, sizeof b); CHECKS(b, "my.dir");
    /* a directory's name: no dot may stay, it would start a subdirectory */
    CHECK(!vf_local_dirname("data", 1, b, sizeof b)); CHECKS(b, "data");
    CHECK(vf_local_dirname("v1.2.3", 1, b, sizeof b)); CHECKS(b, "v1^.2^.3");
    CHECK(vf_local_dirname("my dir", 1, b, sizeof b)); CHECKS(b, "my^_dir");
    CHECK(!vf_local_dirname("data", 0, b, sizeof b)); CHECKS(b, "DATA");
    CHECK(vf_local_dirname("v1.2", 0, b, sizeof b)); CHECKS(b, "V1_2");
    vf_remote_name("NOTES.TXT;1", b, sizeof b); CHECKS(b, "notes.txt");
    vf_remote_name("LICENSE.;1", b, sizeof b); CHECKS(b, "license");
    vf_remote_name("ReadMe.md;2", b, sizeof b); CHECKS(b, "ReadMe.md");
    vf_remote_name("my^_file^.tar.gz;1", b, sizeof b); CHECKS(b, "my file.tar.gz");
    CHECK(vf_text_type("notes.txt", 0));
    CHECK(vf_text_type("BUILD.COM;1", 0));
    CHECK(!vf_text_type("kit.pcsi", 0));
    CHECK(!vf_text_type("Makefile", 0));
    CHECK(vf_text_type("a.foo", "BAR, .foo"));
}

static char recs[8][64];
static int nrecs;
static unsigned int rec(void *ctx, const char *r, int len)
{
    (void) ctx;
    if (nrecs < 8) { memcpy(recs[nrecs], r, (size_t) len); recs[nrecs][len] = 0; nrecs++; }
    return 1;
}

static void test_text(void)
{
    static vf_lines l;
    vf_lines_init(&l);
    nrecs = 0;
    vf_lines_feed(&l, "one\r\ntw", 7, rec, 0);
    vf_lines_feed(&l, "o\n\nlast", 7, rec, 0);
    CHECK(nrecs == 3);
    vf_lines_end(&l, rec, 0);
    CHECK(nrecs == 4);
    CHECKS(recs[0], "one"); CHECKS(recs[1], "two"); CHECKS(recs[2], ""); CHECKS(recs[3], "last");
}

static void add(vf_pane *p, const char *name, int dir, unsigned int blocks, int day)
{
    vf_entry e;
    memset(&e, 0, sizeof e);
    vf_strlcpy(e.name, name, sizeof e.name);
    e.is_dir = dir; e.blocks = blocks; e.size = blocks * 512ULL; e.has_size = 1; e.has_time = 1;
    e.mtime.y = 2026; e.mtime.m = 10; e.mtime.d = day;
    vf_pane_add(p, &e);
}

static void test_pane(void)
{
    vf_pane p;
    char b[256];
    int n, f, d;
    unsigned long long by;
    unsigned int bl;
    vf_entry par;
    vf_pane_init(&p, 1);
    memset(&par, 0, sizeof par); par.is_parent = 1; par.is_dir = 1; vf_strlcpy(par.name, "[-]", sizeof par.name);
    vf_pane_add(&p, &par);
    add(&p, "NOTES.TXT;1", 0, 14, 25);
    add(&p, "NOTES.TXT;2", 0, 15, 25);
    add(&p, "A.COM;1", 0, 2, 1);
    add(&p, "WORK.DIR;1", 1, 1, 20);
    vf_pane_rebuild(&p);
    CHECK(p.nvis == 5);
    CHECKS(p.e[p.vis[0]].name, "[-]");
    CHECKS(p.e[p.vis[1]].name, "WORK.DIR;1");
    CHECKS(p.e[p.vis[2]].name, "A.COM;1");
    CHECKS(p.e[p.vis[3]].name, "NOTES.TXT;2");
    p.sort = VF_SORT_SIZE; vf_pane_rebuild(&p);
    CHECKS(p.e[p.vis[2]].name, "NOTES.TXT;2");
    vf_strlcpy(p.filter, "*.COM", sizeof p.filter); vf_pane_rebuild(&p);
    CHECK(p.nvis == 3);
    vf_strlcpy(p.filter, "*", sizeof p.filter); vf_pane_rebuild(&p);
    n = vf_pane_tag_match(&p, "NOTES.*", 1);
    CHECK(n == 2);
    vf_pane_tagged(&p, &n, &by, &bl); CHECK(n == 2 && bl == 29);
    vf_pane_totals(&p, &f, &d, &by, &bl); CHECK(f == 3 && d == 1 && bl == 31);
    vf_pane_goto(&p, "a.com;1"); CHECKS(vf_pane_cur(&p)->name, "A.COM;1");
    vf_pane_row(&p, vf_pane_cur(&p), 36, 2026, b, sizeof b);
    CHECKS(b, "A.COM;1                     2  1-OCT");
    vf_pane_free(&p);
    /* remote: a Unix directory gets "/", a VMS server's SUB.DIR;1 is shown as it is (N5) */
    vf_pane_init(&p, 0);
    add(&p, "data", 1, 0, 20);
    add(&p, "SUB.DIR;1", 1, 0, 20);
    vf_pane_rebuild(&p);
    vf_pane_goto(&p, "data"); vf_pane_row(&p, vf_pane_cur(&p), 36, 2026, b, sizeof b);
    CHECK(!strncmp(b, "data/ ", 6));
    vf_pane_goto(&p, "SUB.DIR;1"); vf_pane_row(&p, vf_pane_cur(&p), 36, 2026, b, sizeof b);
    CHECK(!strncmp(b, "SUB.DIR;1 ", 10));
    vf_pane_free(&p);
    /* a VMS FTP server's files (N8): sizes in blocks, the highest version first */
    vf_pane_init(&p, 0);
    p.vms = 1;
    add(&p, "u.txt;1", 0, 1, 20);
    add(&p, "u.txt;2", 0, 3, 20);
    add(&p, "SUB.DIR;1", 1, 1, 20);
    vf_pane_rebuild(&p);
    CHECKS(p.e[p.vis[0]].name, "SUB.DIR;1");
    CHECKS(p.e[p.vis[1]].name, "u.txt;2");
    CHECKS(p.e[p.vis[2]].name, "u.txt;1");
    vf_pane_goto(&p, "u.txt;2"); vf_pane_row(&p, vf_pane_cur(&p), 36, 2026, b, sizeof b);
    CHECKS(b, "u.txt;2                     3 20-OCT");
    vf_pane_free(&p);
    CHECKS(vf_fmt_bytes(99999, b, sizeof b), "99999");
    CHECKS(vf_fmt_bytes(123456, b, sizeof b), "121K");
    CHECKS(vf_fmt_bytes(3500000, b, sizeof b), "3.3M");
    CHECKS(vf_fmt_perms(0750, 1, 0, b, sizeof b), "drwxr-x---");
}

static char written[4096];
static unsigned int collect_line(void *ctx, const char *line, int len)
{
    (void) ctx;
    strncat(written, line, (size_t) len);
    strcat(written, "\n");
    return 1;
}

static void test_sites(void)
{
    vf_sites t;
    vf_site s;
    static const char *file[] = {
        "! my sites", "", "[work]", "protocol = sftp", "host = build.example.com", "port=2222",
        "user = iain", "key = USER$ROOT:[IAIN.SSH]ID_ED25519.", "remote = /home/iain/src",
        "[ Old FTP ]", "protocol = FTP", "host = vax.example.com", "local = DKA0:[IAIN.OLD]",
    };
    int i;
    vf_sites_init(&t);
    for (i = 0; i < (int) (sizeof file / sizeof *file); i++) vf_sites_line(&t, file[i]);
    CHECK(t.n == 2 && !t.bad);
    CHECKS(t.s[0].proto, "SFTP"); CHECKS(t.s[0].port, "2222"); CHECKS(t.s[0].remote, "/home/iain/src");
    CHECKS(t.s[1].name, "Old FTP"); CHECKS(t.s[1].proto, "FTP"); CHECKS(t.s[1].local, "DKA0:[IAIN.OLD]");
    CHECK(vf_sites_find(&t, "old ftp") == 1 && vf_sites_find(&t, "home") == -1);
    /* written back sorted by name, then read again the same */
    written[0] = '\0';
    CHECK(vf_sites_write(&t, collect_line, 0) & 1);
    CHECK(strstr(written, "[Old FTP]\nprotocol = FTP\nhost = vax.example.com\nlocal = DKA0:[IAIN.OLD]\n\n[work]\n") != 0);
    CHECK(!strstr(written, "port = \n"));                    /* empty fields left out */
    {
        vf_sites u;
        char *line = written, *nl;
        vf_sites_init(&u);
        while ((nl = strchr(line, '\n')) != 0) { *nl = '\0'; vf_sites_line(&u, line); line = nl + 1; }
        CHECK(u.n == 2 && !u.bad && !strcmp(u.s[1].key, "USER$ROOT:[IAIN.SSH]ID_ED25519."));
        vf_sites_free(&u);
    }
    /* add, replace by name, remove */
    memset(&s, 0, sizeof s);
    vf_strlcpy(s.name, "home", sizeof s.name); vf_strlcpy(s.host, "h.example", sizeof s.host);
    CHECK(vf_sites_put(&t, &s) && t.n == 3);
    vf_strlcpy(s.name, "WORK", sizeof s.name);
    CHECK(vf_sites_put(&t, &s) && t.n == 3 && !strcmp(t.s[0].host, "h.example"));
    vf_sites_remove(&t, 0);
    CHECK(t.n == 2 && vf_sites_find(&t, "work") == -1);
    vf_strlcpy(s.name, "a]b", sizeof s.name);
    CHECK(!vf_sites_put(&t, &s) && !vf_site_name_ok("  ") && vf_site_name_ok("x"));
    vf_sites_free(&t);

    /* lines it cannot read: left out, the first remembered (and the app saves nothing) */
    vf_sites_init(&t);
    vf_sites_line(&t, "host = early");
    vf_sites_line(&t, "[x]");
    vf_sites_line(&t, "password = secret");
    vf_sites_line(&t, "colour = blue");
    vf_sites_line(&t, "protocol = telnet");
    vf_sites_line(&t, "port = 22a");
    vf_sites_line(&t, "[y]");
    vf_sites_line(&t, "host = ok");
    vf_sites_line(&t, "[X]");                                  /* a second x */
    CHECK(t.bad == 6 && t.n == 2);
    CHECKS(t.why, "line 1: a key before the first [name]");
    CHECKS(t.s[1].host, "ok");
    CHECK(!t.s[0].host[0]);
    vf_sites_free(&t);
    vf_sites_init(&t);
    vf_sites_line(&t, "[x]");
    vf_sites_line(&t, "Password=secret");
    CHECKS(t.why, "line 2: passwords are never kept (D9): take the line out");
    vf_sites_free(&t);
}

static void test_perms(void)
{
    unsigned int m = 0;
    char why[160];
#define PERMS(spec, mode, dir, known, want) \
    do { m = 0xFFFF; CHECK(vf_perms_apply(spec, mode, dir, known, &m, why, sizeof why) && m == (want)); } while (0)
    PERMS("644", 0, 0, 0, 0644u);
    PERMS(" 2775 ", 0, 1, 0, 02775u);
    PERMS("rw-r--r--", 0, 0, 0, 0644u);
    PERMS("rwsr-x--T", 0, 0, 0, 05750u);
    PERMS("go-w", 0666, 0, 1, 0644u);
    PERMS("u+x,g=r", 0600, 0, 1, 0740u);
    PERMS("+x", 0644, 0, 1, 0755u);
    PERMS("a+X", 0644, 0, 1, 0644u);                    /* a file with no x: none */
    PERMS("a+X", 0644, 1, 1, 0755u);                    /* a directory: x */
    PERMS("o=", 0757, 0, 1, 0750u);
    PERMS("u-w+x", 0644, 0, 1, 0544u);
    CHECK(!vf_perms_apply("go-w", 0, 0, 0, &m, why, sizeof why) && strstr(why, "not known"));
    CHECK(!vf_perms_apply("888", 0644, 0, 1, &m, why, sizeof why));
    CHECK(!vf_perms_apply("rw-r--r-x-", 0644, 0, 1, &m, why, sizeof why));
    CHECK(!vf_perms_apply("rw-rw-rwz", 0644, 0, 1, &m, why, sizeof why));
    CHECK(!vf_perms_apply("g+q", 0644, 0, 1, &m, why, sizeof why));
    CHECK(!vf_perms_apply("", 0644, 0, 1, &m, why, sizeof why));
#undef PERMS
}

static void test_shquote(void)
{
    char b[64];
    CHECK(vf_shquote("/home/a b", b, sizeof b)); CHECKS(b, "'/home/a b'");
    CHECK(vf_shquote("it's", b, sizeof b)); CHECKS(b, "'it'\\''s'");
    CHECK(vf_shquote("$(rm -rf ~);`x`", b, sizeof b)); CHECKS(b, "'$(rm -rf ~);`x`'");
    CHECK(vf_shquote("", b, sizeof b)); CHECKS(b, "''");
    CHECK(!vf_shquote("abc", b, 5) && !b[0]);              /* 'abc' and NUL are 6 */
    CHECK(vf_shquote("abc", b, 6)); CHECKS(b, "'abc'");
    CHECK(!vf_shquote("a'", b, 7) && !b[0]);               /* 'a'\''' and NUL are 8 */
    CHECK(vf_shquote("a'", b, 8)); CHECKS(b, "'a'\\'''");
}

int main(void)
{
    test_util();
    test_path();
    test_text();
    test_pane();
    test_edit();
    test_screen();
    test_sites();
    test_perms();
    test_shquote();
    printf("test_core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
