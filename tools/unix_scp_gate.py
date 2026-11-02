"""unix_scp_gate.py <root> - the ForceCommand of the SCP live test's sshd
(tools/unix_sftpd.sh start scp). It runs only what VFTP's SCP sends (D15): libssh2's
"scp -t <path>" / "scp -pf <path>", and "sh -c <script>" where the script is one of
the templates in src/net/vf_sftp.c, every name in it inside <root> (the test's
scratch directory, where it runs). Anything else is refused. Each command is logged
to <root>/../scp-gate.log, ALLOW or REFUSE, with <root>/ taken out of names."""
import os
import re
import shlex
import sys

root = os.path.realpath(sys.argv[1])
log = os.path.join(os.path.dirname(root), "scp-gate.log")
cmd = os.environ.get("SSH_ORIGINAL_COMMAND", "")

# A name as vf_shquote writes it: '...' with each ' as '\''
Q = r"('[^']*'(?:\\''[^']*')*)"

# vf_sftp.c's scripts, %1 and %2 the names (keep these the same as the C)
TEMPLATES = [
    "printf 'VFTP-%s\\n' UNIX",
    "cd -- %1 && pwd -P",
    "cd -- %1 || exit 1; LC_ALL=C ls -la || exit 1; echo '//VFTP-DIRLINKS'; "
    "for f in .* *; do [ -L \"$f\" ] && [ -d \"$f\" ] && printf '%s\\n' \"$f\"; done; exit 0",
    "LC_ALL=C ls -ld -- %1 || exit 1; [ -d %1 ] && echo //VFTP-DIR; exit 0",
    "mkdir -- %1",
    "rmdir -- %1",
    "rm -- %1",
    "if [ -e %2 ] || [ -L %2 ]; then echo 'there is one already: '%2 >&2; exit 1; fi; mv -- %1 %2",
    "mv -f -- %1 %2",
    "chmod %M -- %1",
]


def pattern(t):
    seen = set()
    out = ""
    for part in re.split(r"(%[12M])", t):
        if part in ("%1", "%2"):
            g = "a" + part[1]
            out += "(?P=%s)" % g if g in seen else "(?P<%s>%s)" % (g, Q[1:-1])
            seen.add(g)
        elif part == "%M":
            out += "[0-7]{1,4}"
        else:
            out += re.escape(part)
    return re.compile(out + r"\Z")


PATTERNS = [pattern(t) for t in TEMPLATES]


def unquote(q):
    return q[1:-1].replace("'\\''", "'")


def inside(name):
    p = os.path.normpath(os.path.join(root, name))
    return p == root or p.startswith(root + "/")


def note(what, text=None):
    text = cmd if text is None else text
    with open(log, "a") as f:
        f.write("%s %s\n" % (what, text.replace(root + "/", "").replace(root, ".")))


def refuse(why):
    note("REFUSE")
    sys.stderr.write("unix_scp_gate: refused: %s\n" % why)
    sys.exit(1)


os.chdir(root)
try:
    words = shlex.split(cmd)
except ValueError:
    refuse("cannot split the command")
if len(words) == 3 and words[0] == "scp" and words[1] in ("-t", "-f", "-pf", "-pt"):
    if not inside(words[2]):
        refuse("outside the scratch directory")
    note("ALLOW")
    os.execvp("scp", words)
if len(words) == 3 and words[:2] == ["sh", "-c"]:
    for p in PATTERNS:
        m = p.match(words[2])
        if m:
            if not all(inside(unquote(v)) for v in m.groupdict().values() if v is not None):
                refuse("outside the scratch directory")
            note("ALLOW", "sh -c: " + words[2])
            os.execvp("sh", words)
    refuse("not one of VFTP's commands")
refuse("not scp or sh -c")
