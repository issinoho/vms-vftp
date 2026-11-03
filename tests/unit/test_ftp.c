/* test_ftp.c - the FTP core (vf_ftpcore): replies, PASV/EPSV/PWD, names, and LIST
   lines. The VMS lines are as TCP/IP Services FTP 6.0 sent them (NOTES N8), with
   the owner changed. */
#include <stdio.h>
#include <string.h>
#include "vf_ftpcore.h"
#include "vf_util.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKS(a, b) do { checks++; if (strcmp((a), (b))) { fails++; \
    printf("FAIL %s:%d: %s\n   got  [%s]\n   want [%s]\n", __FILE__, __LINE__, #a, (a), (b)); } } while (0)

static void test_reply(void)
{
    vf_ftp_reply r;
    char m[256];
    vf_ftp_reply_init(&r);
    CHECK(vf_ftp_reply_line(&r, "220 node FTP Server (Version 6.0) Ready."));
    CHECK(r.code == 220 && !r.multi);
    CHECKS(r.text, "node FTP Server (Version 6.0) Ready.");
    /* the next line starts a new reply */
    CHECK(!vf_ftp_reply_line(&r, "250-CWD command successful."));
    CHECK(r.code == 250 && r.multi && !r.done);
    CHECK(vf_ftp_reply_line(&r, "250 New default directory is USER$ROOT:[IAIN.VMS_VFTP]"));
    CHECKS(r.text, "CWD command successful.\nNew default directory is USER$ROOT:[IAIN.VMS_VFTP]");
    vf_ftp_reply_init(&r);
    vf_ftp_reply_line(&r, "550-Failed to open USER$ROOT:[IAIN]nosuch.file; for input.");
    vf_ftp_reply_line(&r, "550 file not found");
    vf_ftp_reply_msg(&r, m, sizeof m);
    CHECKS(m, "file not found (Failed to open USER$ROOT:[IAIN]nosuch.file; for input.)");
    /* FEAT: lines inside that are not codes */
    vf_ftp_reply_init(&r);
    CHECK(!vf_ftp_reply_line(&r, "211-Extensions supported."));
    CHECK(!vf_ftp_reply_line(&r, " AUTH TLS."));
    CHECK(!vf_ftp_reply_line(&r, " PBSZ."));
    CHECK(vf_ftp_reply_line(&r, "211 END."));
    CHECK(r.code == 211 && strstr(r.text, "AUTH TLS.") != 0);
    CHECK(!vf_ftp_feat(r.text, "REST STREAM"));
    CHECK(vf_ftp_feat("Features:\n MDTM\n rest stream\n SIZE\nEnd", "REST STREAM"));
    CHECK(vf_ftp_feat(" REST STREAM", "REST STREAM"));
    CHECK(!vf_ftp_feat(" REST", "REST STREAM"));
    CHECK(!vf_ftp_feat(" REST STREAMING", "REST STREAM"));
    /* a different code inside a multi-line reply does not end it */
    vf_ftp_reply_init(&r);
    vf_ftp_reply_line(&r, "214-The following commands are recognized:");
    CHECK(!vf_ftp_reply_line(&r, "200 not the end"));
    CHECK(vf_ftp_reply_line(&r, "214 End of Help."));
    vf_ftp_reply_msg(&r, m, sizeof m);
    CHECKS(m, "End of Help. (The following commands are recognized:)");
}

static void test_replies(void)
{
    char h[64], p[256];
    int port;
    unsigned long long n;
    CHECK(vf_ftp_pasv("Entering Passive Mode (192,0,2,10,236,21)", h, sizeof h, &port));
    CHECKS(h, "192.0.2.10"); CHECK(port == 236 * 256 + 21);
    CHECK(vf_ftp_pasv("=10,0,0,5,4,1", h, sizeof h, &port) && port == 1025);   /* no parentheses */
    CHECK(!vf_ftp_pasv("Entering Passive Mode (1,2,3)", h, sizeof h, &port));
    CHECK(vf_ftp_epsv("Entering Extended Passive Mode (|||60438|)", &port) && port == 60438);
    CHECK(!vf_ftp_epsv("Entering Extended Passive Mode (||60438|)", &port));
    CHECK(vf_ftp_pwd("\"USER$ROOT:[iain]\" is current directory.", p, sizeof p));
    CHECKS(p, "USER$ROOT:[iain]");
    CHECK(vf_ftp_pwd("\"/home/a \"\"b\"\"\" is the current directory", p, sizeof p));
    CHECKS(p, "/home/a \"b\"");
    CHECK(!vf_ftp_pwd("no quotes", p, sizeof p));
    CHECK(vf_ftp_size150("Opening data connection for USER$ROOT:[X]t.txt;2 (192.0.2.20,33202) (74 bytes)", &n));
    CHECK(n == 74);
    CHECK(!vf_ftp_size150("Opening data connection for USER$ROOT:[X]*.*;* (192.0.2.20,43768)", &n));
    vf_ftp_unescape("My^_File^.tar.gz;1", p, sizeof p); CHECKS(p, "My File.tar.gz;1");
    vf_ftp_unescape("a^]b.DIR;1", p, sizeof p); CHECKS(p, "a]b.DIR;1");
    vf_ftp_unescape("^.CONFIG.DIR;1", p, sizeof p); CHECKS(p, ".CONFIG.DIR;1");
    vf_ftp_unescape("x^20y.txt;1", p, sizeof p); CHECKS(p, "x y.txt;1");
    vf_ftp_unescape("PLAIN.TXT;3", p, sizeof p); CHECKS(p, "PLAIN.TXT;3");
}

/* every entry of a listing, as "name|dir|size|y-m-d hh:mm|owner|prot" lines */
static void list_all(const char *const *lines, char *out, size_t size)
{
    vf_ftp_list l;
    vf_entry e;
    char b[512];
    int i;
    out[0] = '\0';
    vf_ftp_list_init(&l, 2026, 10);
#define ADD() do { snprintf(b, sizeof b, "%s|%d|%llu|%d-%d-%d %02d:%02d|%s|%s|%o\n", e.name, e.is_dir, \
        e.size, e.mtime.y, e.mtime.m, e.mtime.d, e.mtime.hh, e.mtime.mm, e.owner, e.prot, e.perms); \
        vf_strlcat(out, b, size); } while (0)
    for (i = 0; lines[i]; i++) {
        if (vf_ftp_list_line(&l, lines[i], &e)) ADD();
        while (vf_ftp_list_more(&l, &e)) ADD();
    }
    if (vf_ftp_list_end(&l, &e)) ADD();
}

static void test_vms(void)
{
    static const char *const ls[] = {
        "",
        "Directory USER$ROOT:[USER.FTPPROBE]",
        "",
        "b.bin;1                  3/16         31-OCT-2026 20:11:18  [GROUP,USER]          (RWED,RWED,RE,)",
        "My^_File^.tar.gz;1       1/16         31-OCT-2026 20:11:18  [GROUP,USER]          (RWED,RWED,RE,)",
        "SUB.DIR;1                1/16         31-OCT-2026 20:11:13  [GROUP,USER]          (RWE,RWE,RE,RE)",
        "COVER_LETTER_VSI_CPLUSPLUS_AUG2016.PDF;1",
        "                       222/224         2-OCT-2026 17:28:57  [GROUP,USER]          (RWED,RWED,RE,)",
        "HUGE.DAT;7           39063/49152      25-SEP-2026 09:50:40  [GROUP,USER]          (RWED,RWED,RE,)",
        "errorlog.IAIN;1          0/0          24-OCT-2026 10:07:51  [GROUP,USER]          (RWED,RWED,RE,)",
        "",
        "Total of 6 files, 39290/49440 blocks",
        0 };
    static const char *const lone[] = {      /* a long name, then not its line */
        "AN_EXCEEDINGLY_LONG_FILE_NAME.TXT;1",
        "SHORT.TXT;1              1/16         31-OCT-2026 20:11:18  [GROUP,USER]          (RWED,RWED,RE,)",
        "LAST_ONE_WITH_A_LONG_NAME.TXT;2",
        0 };
    char out[2048];
    list_all(ls, out, sizeof out);
    CHECKS(out,
        "b.bin;1|0|1536|2026-10-31 20:11|GROUP,USER|(RWED,RWED,RE,)|0\n"
        "My^_File^.tar.gz;1|0|512|2026-10-31 20:11|GROUP,USER|(RWED,RWED,RE,)|0\n"
        "SUB.DIR;1|1|512|2026-10-31 20:11|GROUP,USER|(RWE,RWE,RE,RE)|0\n"
        "COVER_LETTER_VSI_CPLUSPLUS_AUG2016.PDF;1|0|113664|2026-10-2 17:28|GROUP,USER|(RWED,RWED,RE,)|0\n"
        "HUGE.DAT;7|0|20000256|2026-9-25 09:50|GROUP,USER|(RWED,RWED,RE,)|0\n"
        "errorlog.IAIN;1|0|0|2026-10-24 10:07|GROUP,USER|(RWED,RWED,RE,)|0\n");
    list_all(lone, out, sizeof out);
    CHECKS(out,
        "AN_EXCEEDINGLY_LONG_FILE_NAME.TXT;1|0|0|0-0-0 00:00|||0\n"
        "SHORT.TXT;1|0|512|2026-10-31 20:11|GROUP,USER|(RWED,RWED,RE,)|0\n"
        "LAST_ONE_WITH_A_LONG_NAME.TXT;2|0|0|0-0-0 00:00|||0\n");
}

static void test_unix(void)
{
    static const char *const ls[] = {
        "total 10260",
        "drwxrwxr-x 3 iain iain     4096 Oct 31 15:36 .",
        "drwx------ 3 iain iain     4096 Oct 31 15:31 ..",
        "drwxr-xr-x 2 iain iain     4096 Oct 31 15:35 SUBDIR",
        "-rw-rw-r-- 1 iain iain 10485760 Oct 31 15:31 big.bin",
        "-rw-r--r-- 1 ftp  ftp        56 Dec 20 09:01 two  spaces.txt",
        "-rw-r--r-- 1 ftp  ftp        56 Jan  3  2024 old.txt",
        "lrwxrwxrwx 1 root root        7 Mar  1 12:00 bin -> usr/bin",
        "-rwsr-x--T 1 root wheel    1234 Oct  1 00:00 odd",
        "-rw-r--r-- 1 owner     99 Oct  2 10:10 nogroup.txt",
        0 };
    char out[2048];
    list_all(ls, out, sizeof out);
    CHECKS(out,
        "SUBDIR|1|4096|2026-10-31 15:35|iain||755\n"
        "big.bin|0|10485760|2026-10-31 15:31|iain||664\n"
        "two  spaces.txt|0|56|2025-12-20 09:01|ftp||644\n"
        "old.txt|0|56|2024-1-3 00:00|ftp||644\n"
        "bin|0|7|2026-3-1 12:00|root||777\n"
        "odd|0|1234|2026-10-1 00:00|root||5750\n"
        "nogroup.txt|0|99|2026-10-2 10:10|owner||644\n");
}

static void test_dos(void)
{
    static const char *const ls[] = {
        "10-31-26  03:31PM       <DIR>          SUB DIR",
        "10-31-26  12:05AM                 1234 report 2026.txt",
        "01-02-2025  11:59AM                 7 a.txt",
        0 };
    char out[1024];
    list_all(ls, out, sizeof out);
    CHECKS(out,
        "SUB DIR|1|0|2026-10-31 15:31|||0\n"
        "report 2026.txt|0|1234|2026-10-31 00:05|||0\n"
        "a.txt|0|7|2025-1-2 11:59|||0\n");
}

int main(void)
{
    test_reply();
    test_replies();
    test_vms();
    test_unix();
    test_dos();
    printf("test_ftp: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
