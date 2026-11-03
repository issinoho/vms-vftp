# Decisions

## D1. Name: VFTP (2026-10-25, user's choice)

`vms-vftp`, command and PCSI product `VFTP`: a "V" tool like VTREE and VLMF, named
after the best-known of the protocols it speaks. Considered: VXFER (protocol-neutral),
VSFTP (easily confused with Linux's vsftpd).

## D2. Look: VTREE's, with two panes side by side (2026-10-25, user's choice)

The screen model, the colour renderer (`vf_ansi.c`: XTree Gold's palette as ANSI
colour, VTREE D11, reverse-video bars VLMF D10), the line editor and the SMG$
keyboard and broadcast code are VTREE's and VLMF's, renamed `vf_`. The layout is
two framed panes, local left and remote right (Norton Commander style), with Tab
between them and a statistics strip under them. Considered: VTREE's single list
with the panel on the right and Tab flipping it between local and remote (less to
draw, but a transfer client wants both ends in view).

## D3. SFTP and SCP through libssh2, ported as vms-libssh2 (2026-10-25, user's choice)

libssh2 1.11.1 ships VMS build files (`vms/libssh2_make_lib.dcl`), and VSI's SSL3
kit (OpenSSL 3.0) is on both nodes. The port is a family project of its own
(signed tarball, patches, overlay, kit, as vms-zstd), built as an object library;
VFTP links it statically, as vms-curl links vms-zstd, so VFTP's kit needs only SSL3.
VFTP then speaks SFTP and SCP itself: exact progress, cancel, resume, attributes,
password and key login, host-key checks with the same known_hosts as VSI's ssh.
Considered: running VSI OpenSSH's `sftp` and `scp` in a subprocess and parsing
their output (nothing to port, but passwords need a pseudo-terminal, errors and
progress come from text, listings from `ls -l` output). The port may later give
vms-curl SFTP and SCP too.

## D4. FTP is our own code (2026-10-25)

FTP is a small text protocol over TCP; a client of our own (C sockets from the
CRTL) gives the same control as D3. The command and reply handling and the LIST
parsers are pure core code (no I/O), tested on Linux. Considered: libcurl from
vms-curl (no rename, delete or listing parse without QUOTE commands and our own
parsers anyway; a large dependency); driving TCP/IP Services' FTP client
(interactive text again).

## D5. Phase 1 is SFTP (2026-10-25, user's choice)

SFTP first (with the libssh2 port in Phase 0), FTP in Phase 2, SCP and the rest
in Phase 3. Considered: FTP first (no porting, sooner to try).

## D6. Text and binary transfers (2026-10-25, accepted)

Mode AUTO (default), ASCII or BINARY, shown in the strip and set with `X`.
- Upload: in AUTO a local file with variable-length or VFC records and carriage
  control is text (each record sent as a line ending in LF; FTP TYPE A), anything
  else (fixed, undefined, stream) is binary (the bytes as they are).
- Download: in AUTO a name whose type is in a text list (`.TXT .C .H .COM .HTML ...`,
  replaceable by the logical name `VFTP$TEXT_TYPES`) is text, written as a variable
  record file; anything else is binary, written as fixed 512-byte records
  (`SET FILE/ATTRIBUTE` can change it after, as for a PCSI kit).
To be checked by the Phase 0 probes; the alternative for downloads is Stream_LF
for everything (no choice to make, but binary files then read badly in VMS tools).

## D7. Host keys and keys: VSI OpenSSH's files (2026-10-25, accepted)

VFTP reads and adds to the known_hosts that VSI's ssh uses, and offers the keys
VSI's ssh would (`id_ed25519`, `id_ecdsa`, `id_rsa`), so trust and keys are shared.
An unknown host shows its key fingerprint (SHA-256, as ssh does) and asks; a
changed key refuses to connect. The files are in `SYS$LOGIN:[.SSH]` (N3), and libssh2
reads them as they are.

## D8. Local files: VMS specifications (2026-10-25, accepted)

The local pane shows VMS specifications (`DKA0:[IAIN]NOTES.TXT;1`), the latest
version by default and every version on request, as VTREE. A downloaded file is
always a new version, never a replacement. Remote names that are not valid on
ODS-5 (or on ODS-2 on such a disk) are changed by a stated rule and the new name
is shown before the transfer.

## D9. No stored passwords (2026-10-25, accepted)

The sites file (`VFTP$SITES`, default `SYS$LOGIN:VFTP.SITES`) holds protocol, host,
port, user, key file and starting directories, never a password. Passwords are
typed each time and kept only in memory for the connection.

## D10. A password without TLS is sent only when the user says so (2026-10-31, user's choice)

Both nodes' FTP servers list AUTH TLS and refuse it (N8), so a login there sends
the password unencrypted. Before PASS goes on a connection with no TLS, VFTP asks
"The password will cross the network unencrypted. Send it? (Y/N) [N]", default No,
every time. Considered: a warning only (the user's choice is lost in a status
line); refusing (no FTP at all to these servers).

## D11. The FTP live test logs in to a test account (2026-10-31, user's choice)

PTYDRIVE has to type a password. It is the password of an account made for the
test (e.g. VFTPTEST, no privileges, its own login directory as the scratch area),
kept in the git-ignored `tools/live_ftp_pass` like the test key (CLAUDE.md: test
passwords are made for the test and never committed), and put on the node only
for the run. The user's own password is never in a script. Considered: anonymous
FTP (a larger server change); FTP tested by hand only.

## D12. FTPS: explicit TLS when the server takes it; the certificate pinned on first use (2026-11-01, accepted)

An FTP connection sends AUTH TLS first. If the server takes it, the control
connection goes to TLS (1.2 or later) and, after the login, PBSZ 0 and PROT P put
every data connection in TLS too, reusing the control connection's session. The
server's certificate is not checked against certificate authorities: its SHA-256
is shown the first time ("openssl x509 -fingerprint -sha256" shows the same),
added to VFTP's known_tls file in the SSH directory when the user says Y, and a
different one later is refused, as an SSH host key is (D7). A server that refuses
AUTH TLS (both nodes, N8) gets plain FTP, and the password question of D10.
Considered: CA checking (VMS systems rarely have a CA bundle set up, and test and
internal servers use self-signed certificates); requiring TLS (no FTP to the
nodes at all); implicit FTPS on port 990 (rare, and not in RFC 4217).

## D13. Whole directories: the tree read first, then copied (2026-11-01, accepted)

Get and Put take tagged (or current) directories as well as files; Space tags a
directory, `T` and `*` still tag files only. Before anything is copied VFTP reads
the whole tree on the source side and, where a directory is there already, the
matching one on the destination side, and makes a list of steps (directories to
make, files to copy). So the replace screen of a Put names every file that would
be replaced at any depth (`work/plan.txt`), Y or N covers the lot, and N copies
nothing. The steps then run in order. A directory that cannot be made skips what
would go in it; a directory that cannot be read, or a file on the server where a
directory would go, stops the transfer before anything is copied.
- Links to directories are not followed (they may lead back up); the message says
  how many were left out. At most 32 levels below the tagged directory, and 100000
  steps in one transfer.
- A download is still always a new version (D8): a local directory that is there
  already is used, its files get new versions. Directory names from a server
  become local directory names with every dot escaped (`v1.2` is `[.v1^.2]` on
  ODS-5, `[.V1_2]` on ODS-2), since a dot would start a subdirectory.
- A VMS server lists every version and its directories as `SUB.DIR;1`, by FTP
  and by SFTP (N2, N8): a directory Get takes the highest version of each file
  and names the directory `SUB`.
- On a VMS server a Put makes new versions (N5), so nothing is listed as replaced.
Considered: copying while reading (no full list to confirm, and the replace
question would come file by file, mid-transfer); following links (loops, and the
same files twice); asking before every directory (noise: copying a directory into
one that does not hold it changes nothing that is there).

## D14. The sites file: blocks of "key = value", saved from the Open form (2026-11-01, accepted)

`VFTP$SITES` (default `SYS$LOGIN:VFTP.SITES`), a text file to read and edit by
hand: a `[name]` line starts a site, then `protocol`, `host`, `port`, `user`, `key`,
`remote` and `local` (the starting directories), each `key = value`; `!` or `#`
starts a comment. There is no password key: a `password` (or `passphrase`) line is
refused (D9).
- `L` shows the sites. Return fills the Open form from one (empty fields keep the
  form's defaults; the local pane goes to `local`), the cursor on the password, so
  a key login is Do and a password login is the password and Do. `A` saves what the
  Open form holds, with the local pane's directory, under a name (the host by
  default; the same name asks before replacing). `D` deletes one, after asking.
- `vftp <name>` (a word with no `@` or `:`) opens the site of that name.
- The file is read again each time (it may have been edited); a save is a new
  version, written whole, sorted by name, comments other than VFTP's not kept.
- A line VFTP cannot read (an unknown key, a password, a key before any `[name]`)
  is left out, the rest is used, and nothing is saved over the file until it is
  put right: saving would lose that line.
Considered: one line per site with separators (hard to read, and a Unix path may
have spaces); saving after every connection (sites the user never asked for);
keeping comments in place (VFTP would have to rewrite the user's file line by
line; the old version stays on disk anyway).

## D15. SCP: SFTP's session, files by scp, the rest by shell commands (2026-11-02, accepted)

SCP is the third protocol on the Open form (port 22, like SFTP). The connection,
host key (D7) and login are SFTP's, on the same libssh2 session code. Files go by
libssh2's scp (`scp -f`, `scp -t` on the server); listing and managing them is done
as WinSCP's SCP mode does, by commands run on the server, each as
`sh -c '<script>'` so that the user's login shell does not matter, every name in
single quotes (`'` as `'\''`):
- list: `cd -- dir; LC_ALL=C ls -la`, read by the FTP code's Unix LIST parser,
  then the links that lead to directories (`[ -L f ] && [ -d f ]`); stat
  `ls -ld`; the directory a path means `cd -- p && pwd -P`;
- `mkdir`, `rmdir`, `rm`, `mv -f` (a replacing Put's temporary file over the old
  one, as SFTP's), `chmod` (GOLD A); a rename refuses a name that is there
  already, as SFTP's does. A command's first line of errors is the message.
- Unix servers only. After the login VFTP runs `printf 'VFTP-%s\n' UNIX`; a server
  without a Unix shell (VMS sshd: DCL) is refused, "use SFTP".
- scp must know a file's size before its first byte, and a text Put's size is
  only known after its records become LF lines: when the server needs it
  (`vfr_needs_size`), the app reads the file once to count, then sends it.
- A Get is bytes, made into records as an SFTP Get's are.
Considered: SCP to VMS sshd (no shell to list with, and VMS scp is not what Unix
scp is); the size by a temporary copy in SYS$SCRATCH (twice the disk, and no
progress while it is sent); listing by SFTP when the server has it (then it is
SFTP).
