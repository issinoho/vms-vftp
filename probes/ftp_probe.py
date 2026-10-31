#!/usr/bin/env python3
"""ftp_probe.py <node> [--clear] - what a node's FTP server says once logged in, for the FTP
client (PLAN Phase 2): LIST and NLST formats, PASV/EPSV replies, paths, text and
binary transfers, SIZE/MDTM/REST, rename, delete, directories, errors.

Run by the user (it asks for the password; nothing stores it, D9). The login goes
over AUTH TLS when the server offers it, so the password is not sent in clear;
the server's certificate is not checked (a probe). Without TLS it stops unless
--clear says the password may go in clear (the user's choice). Works only in [.FTPPROBE] under
the node's workdir (tools/nodes.conf), which it makes and removes. Every reply and
listing goes to out/ftp-probe-<node>.txt; the password never does.
"""
import ftplib, getpass, io, os, ssl, sys

top = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
node = sys.argv[1] if len(sys.argv) > 1 else sys.exit("usage: ftp_probe.py <node> [--clear]")
clear = "--clear" in sys.argv[2:]
if not sys.stdin.isatty():
    sys.exit("ftp_probe.py: needs a terminal to ask for the password (run it in a terminal "
             "window, not with Claude Code's ! prefix)")
row = [l.split() for l in open(os.path.join(top, "tools", "nodes.conf"))
       if l.split() and l.split()[0] == node]
if not row:
    sys.exit("ftp_probe.py: unknown node " + node)
_, _, host, _, user, workdir = row[0][:6]
os.makedirs(os.path.join(top, "out"), exist_ok=True)
log = open(os.path.join(top, "out", "ftp-probe-%s.txt" % node), "w")


def say(*a):
    s = " ".join(str(x) for x in a)
    print(s)
    log.write(s + "\n")
    log.flush()


class Probe(ftplib.FTP_TLS):
    def putcmd(self, line):                     # log commands, never PASS
        say(">>", "PASS ****" if line.upper().startswith("PASS") else line)
        super().putcmd(line)

    def getmultiline(self):
        r = super().getmultiline()
        for l in r.split("\n"):
            say("  ", l)
        return r


def tryit(what, f, *a):
    try:
        return f(*a)
    except (ftplib.all_errors) as e:
        say("   (%s: %s)" % (what, e))
        return None


def lines(cmd):
    out = []
    tryit(cmd, ftp.retrlines, cmd, out.append)
    say("   ---- %s: %d lines" % (cmd, len(out)))
    for l in out:
        say("   |" + l)
    return out


def in_probe_dir():
    """Never write or delete outside [.FTPPROBE]: stop if the server is elsewhere."""
    d = tryit("PWD", ftp.pwd) or ""
    if "FTPPROBE" not in d.upper():
        say("   not in [.FTPPROBE] (%r): stopping, nothing written or deleted" % d)
        tryit("QUIT", ftp.quit)
        sys.exit(1)


def get(name, binary):
    b = io.BytesIO()                            # the TYPE is set by the caller
    tryit("RETR", ftp.retrbinary, "RETR " + name, b.write)
    say("   ---- %d bytes: %r" % (len(b.getvalue()), b.getvalue()[:200]))
    return b.getvalue()


ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
ftp = Probe(context=ctx, timeout=15)
ftp.set_debuglevel(0)
say("==== connect", host)
ftp.connect(host, 21)
say("   greeting:", ftp.getwelcome())
tls = tryit("AUTH TLS", ftp.auth)
if tls is None and not clear:
    sys.exit("ftp_probe.py: no AUTH TLS; not sending a password in clear (--clear to allow)")
say("   login:", "TLS" if tls is not None else "IN CLEAR (--clear)")
pw = getpass.getpass("FTP password for %s on %s (not stored%s): "
                     % (user, node, "" if tls is not None else "; sent in clear"))
try:
    ftp.login(user, pw, secure=tls is not None)
except ftplib.error_perm as e:
    sys.exit("ftp_probe.py: login refused (%s); nothing done on the node" % e)
finally:
    del pw
if tls is not None:
    say("==== data channel protected?")
    if tryit("PROT P", ftp.prot_p) is None:
        tryit("PROT C", ftp.prot_c)

say("==== where are we")
tryit("SYST", ftp.sendcmd, "SYST")
tryit("PWD", ftp.sendcmd, "PWD")
tryit("STAT", ftp.sendcmd, "STAT")
tryit("SITE HELP", ftp.sendcmd, "SITE HELP")
say("==== PASV and EPSV replies (raw)")
tryit("PASV", ftp.sendcmd, "PASV")
tryit("EPSV", ftp.sendcmd, "EPSV")
say("==== the login directory")
lines("LIST")
lines("NLST")

say("==== to the workdir, VMS syntax then Unix syntax")
tryit("CWD", ftp.sendcmd, "CWD " + workdir)
tryit("PWD", ftp.sendcmd, "PWD")
tryit("CWD", ftp.sendcmd, "CWD [-]")
tryit("PWD", ftp.sendcmd, "PWD")
tryit("CWD", ftp.sendcmd, "CWD " + workdir)
tryit("MKD", ftp.sendcmd, "MKD FTPPROBE")
tryit("MKD again", ftp.sendcmd, "MKD FTPPROBE")
tryit("CWD", ftp.sendcmd, "CWD [.FTPPROBE]")
in_probe_dir()

say("==== put a text file (TYPE A, storlines) and a binary one (TYPE I)")
# storbinary always sends TYPE I and storlines TYPE A (the first probe run put t.txt
# in IMAGE by mistake); storlines sends each line with CRLF, as the protocol says
text = b"First line of a text file.\nSecond line.\n\nFourth, after an empty one.\n"
binary = bytes(range(256)) * 4 + b"\n\r\x00 end"
tryit("STOR A", ftp.storlines, "STOR t.txt", io.BytesIO(text))
tryit("STOR I", ftp.storbinary, "STOR b.bin", io.BytesIO(binary))
tryit("STOR A again", ftp.storlines, "STOR t.txt", io.BytesIO(text))   # a new version?
tryit("STOR I CRLF", ftp.storbinary, "STOR crlf.txt", io.BytesIO(text.replace(b"\n", b"\r\n")))
tryit("STOR", ftp.storbinary, "STOR My File.tar.gz", io.BytesIO(b"x" * 10))
long = b"L" * 300 + b"\n" + b"short\n"
tryit("STOR A long", ftp.storlines, "STOR long.txt", io.BytesIO(long))
tryit("MKD", ftp.sendcmd, "MKD SUB")
say("==== listings of [.FTPPROBE]")
lines("LIST")
lines("NLST")
lines("LIST t.txt")
lines("LIST *.TXT;*")
lines("LIST nosuch.file")
lines("NLST nosuch.file")

say("==== get them back")
say("   t.txt in TYPE A (retrlines: the lines as sent, CRLF taken off)")
lines("RETR t.txt")
say("   t.txt in TYPE I (the bytes of the file the server made from TYPE A)")
get("t.txt", 1)
say("   crlf.txt (put in IMAGE) in TYPE A, then TYPE I")
lines("RETR crlf.txt")
get("crlf.txt", 1)
say("   long.txt in TYPE A (a 300-byte line)")
lines("RETR long.txt")
say("   a file made by DCL on the node, in TYPE A and TYPE I:")
lines("RETR " + workdir + "BUILD_VMS.COM")
get(workdir + "BUILD_VMS.COM", 1)
say("   b.bin in TYPE I")
got = get("b.bin", 1)
say("   b.bin round trip identical:", got == binary)
say("   t.txt;1 by version")
get("t.txt;1", 1)
for c in ("MODE S", "STRU F", "STRU R", "ALLO 1000"):
    tryit(c, ftp.sendcmd, c)
tryit("STRU F", ftp.sendcmd, "STRU F")
say("==== errors")
get("nosuch.file", 1)
tryit("CWD nosuch", ftp.sendcmd, "CWD [.NOSUCH]")
tryit("DELE nosuch", ftp.sendcmd, "DELE nosuch.file")
tryit("MKD bad name", ftp.sendcmd, "MKD a]b")
tryit("NOOP", ftp.sendcmd, "NOOP")

say("==== rename, delete, directories")
tryit("RNFR", ftp.sendcmd, "RNFR b.bin")
tryit("RNTO", ftp.sendcmd, "RNTO moved.bin")
tryit("RNFR onto existing", ftp.sendcmd, "RNFR moved.bin")
tryit("RNTO onto existing", ftp.sendcmd, "RNTO t.txt")
tryit("CWD SUB", ftp.sendcmd, "CWD SUB")
tryit("PWD", ftp.sendcmd, "PWD")
tryit("CDUP", ftp.sendcmd, "CDUP")
tryit("PWD", ftp.sendcmd, "PWD")
tryit("CWD Unix-style", ftp.sendcmd, "CWD SUB/")
tryit("PWD", ftp.sendcmd, "PWD")
tryit("CDUP", ftp.sendcmd, "CDUP")
tryit("RMD full dir", ftp.sendcmd, "RMD [-.FTPPROBE]")
tryit("DELE full dir", ftp.sendcmd, "DELE SUB.DIR;1")
lines("LIST")
say("==== SITE UNIX: a Unix-style listing and paths?")
tryit("SITE UNIX", ftp.sendcmd, "SITE UNIX")
tryit("PWD", ftp.sendcmd, "PWD")
lines("LIST")
lines("NLST")
tryit("CWD SUB", ftp.sendcmd, "CWD SUB")
tryit("PWD", ftp.sendcmd, "PWD")
tryit("CWD ..", ftp.sendcmd, "CWD ..")
tryit("PWD", ftp.sendcmd, "PWD")
tryit("SITE VMS", ftp.sendcmd, "SITE VMS")
tryit("PWD", ftp.sendcmd, "PWD")
tryit("CWD", ftp.sendcmd, "CWD " + workdir + "FTPPROBE.DIR")
tryit("CWD", ftp.sendcmd, "CWD " + workdir[:-1] + ".FTPPROBE]")
say("==== clean up")
in_probe_dir()
for n in ftp.nlst() if tryit("NLST", ftp.nlst) else []:
    if not n.upper().startswith("SUB.DIR"):
        tryit("DELE " + n, ftp.sendcmd, "DELE " + n)
tryit("RMD SUB", ftp.sendcmd, "RMD SUB")
tryit("RMD SUB.DIR", ftp.sendcmd, "RMD SUB.DIR;1")
lines("LIST")
tryit("CWD up", ftp.sendcmd, "CWD [-]")
tryit("RMD FTPPROBE", ftp.sendcmd, "RMD FTPPROBE")
tryit("RMD FTPPROBE.DIR", ftp.sendcmd, "RMD FTPPROBE.DIR;1")
lines("LIST FTPPROBE.DIR")
tryit("QUIT", ftp.quit)
say("==== done")
