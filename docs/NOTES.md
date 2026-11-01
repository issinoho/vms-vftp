# Notes: what was observed on the nodes

## N1. What the nodes have (2026-10-25, x86 E9.2-4 and IA64 V8.4-2L3)

Read-only probe (PRODUCT SHOW, logical names, SYS$SYSTEM and SYS$SHARE). Both
nodes: VSI TCP/IP Services V6.0-31 (with `TCPIP$FTP_CLIENT.EXE` and the FTP
server), VSI OpenSSH V9.9-2C (`SSH$ROOT` = `SYS$SYSDEVICE:[VMS$COMMON.OPENSSH.]`,
with `[BIN]`, `[ETC]`, `[LIBEXEC]`, `[SBIN]`), SSL3 (x86 V3.0-21, IA64 V3.0-22)
and SSL31 V3.1-4, with `SSL3$LIBCRYPTO_SHR32.EXE` and `SSL3$LIBSSL_SHR32.EXE`
(32-bit pointer interfaces) in SYS$SHARE. No `SSH`, `SFTP` or `SCP` global
symbols are defined. From the laptop: the IA64 node answers on ports 21 and 22;
the x86 node's sshd is reached on another port and its FTP port does not answer.
The Linux laptop has no libssh2 or OpenSSL headers.

## N2. VSI OpenSSH's SFTP server, seen from Linux (2026-10-25, IA64)

`sftp` from the laptop into the IA64 node, read-only. The login directory is
`/USER$ROOT/iain` (device as the first path element, then directories in lower
case as created). `ls -l` names files with their version (`build_vms.com;1`,
`LICENSE.;1`) and directories as `src.DIR;1`, with Unix-style permissions mapped
from the VMS protection, the owner's user name and its UIC group, and `?` for
the link count. `/` is a pseudo root showing `BIN.DIR;1`, `USR.DIR;1`, `home.DIR;1`...
A Stream_LF file (uploaded earlier by sftp) downloads byte for byte. Still to see:
a variable-record text file (does the server send LF lines and what size does it
report?), and paths with a version in them.

## N3. libssh2 on both nodes (2026-10-25, vms-libssh2 smoke test)

Our libssh2 1.11.1 build (SSL3) negotiates curve25519-sha256, ecdsa-sha2-nistp256
and chacha20-poly1305@openssh.com with VSI OpenSSH 9.9 and with GitHub. The
account's known_hosts is VSI ssh's own `USER$ROOT:[IAIN.SSH]KNOWN_HOSTS.`
(SYS$LOGIN:[.SSH], D7) and libssh2 reads it in OpenSSH format (16 entries on x86,
3 on IA64); GitHub's key MATCHes there. The OpenSSH-format `ID_ED25519.` loads
and logs in to GitHub on both nodes. Each node's sshd offers publickey, password
and keyboard-interactive. On x86 sshd listens on 2222 inside the node too (service
SSHD2222; SSHD22 and FTP are disabled); IA64's sshd is on 22.

## N4. How VSI's SFTP server moves VMS files (2026-10-25, IA64, from Linux)

`probes/probe_sftp_files.com` makes the files; Linux `sftp` fetches them.
- A text file (VFC, print carriage control, 3 records) downloads as LF lines,
  43 bytes, but `ls -l` (and so SFTP's size attribute) says 52: the on-disk bytes
  with record headers. **The listed size of a VMS text file is not the number of
  bytes a download gives**: progress must allow for it and a short read is not an
  error.
- A name without a version gets the highest version; `name;1` gets that version.
- An image (fixed 512-byte records, 92 blocks) downloads as its raw bytes, 47104.
- An upload always becomes a Stream_LF file, whatever the source was.
So a VMS server handles text conversion itself: VFTP sends and receives LF lines
and leaves record formats to the server. Writing VMS files locally is VFTP's job
(D6), through RMS as VTREE's copy does (vms-vtree `vtp_copy`, its N9/N10).

## N5. VFTP against each node's own sshd (2026-10-30, tools/live.sh, both nodes)

The live test logs in to the node's own sshd with a key made for the test
(`tools/live_key`, git-ignored; in the account's authorized_keys as
`restrict,from="127.0.0.1,::1"`). While VFTP runs, SYS$LOGIN is `[.LIVE]`, so its SSH
directory is `[.LIVE.SSH]`: the real known_hosts is copied there and never written.
- The Open form finds the key as `[.SSH]id_ed25519.` under SYS$LOGIN. The unknown
  host key is shown and refused with N, added with Y (`[localhost]:2222` for a port
  other than 22, `localhost` on 22, as ssh writes), and the key login works.
- A relative Remote directory (`vms_vftp/live/remote`) resolves from the login
  directory, as an absolute one (`/DISK$SYSDUMP/...`) does.
- The server lists names with versions (`R.TXT;1`), directories as `SUBDIR.DIR;1`.
- Get of a 5-block binary gives 2560 bytes, the same as the source (DIFFERENCES/HEX).
  Get of a 2-line text file listed as 62 bytes gives 60 (N4) and the same records.
- **Put of a name the server has makes a new version there** (`t.txt;1`, `t.txt;2`).
  Nothing is replaced, so VFTP's replace prompt correctly does not fire: it looks up
  `t.txt` and the server lists `t.txt;1`.
- Mkdir, rename of `t.txt;2` to `moved.txt` and delete of `t.txt;1` work and are logged.
Still to see: a transfer between the two nodes (needs the key let in from the other
node), Put of a large file with progress and cancel, and a Unix SFTP server.

## N6. Progress, cancel, and Ctrl/C (2026-10-30, tools/live.sh)

`test_live.com` makes a 10 MB BIG.BIN (F.BIN, fixed 512, doubled 12 times) on both
sides. `COPY A,A A` does not double A: its second input is the new, empty version.
- **Ctrl/C did not reach VFTP.** With no out-of-band AST the terminal driver turns
  it into Ctrl/Y: "Interrupt" was echoed over the progress bar and the Get ran on,
  and keys typed during it were read and dropped by the progress loop. VFTP now
  traps Ctrl/C with SMG$SET_OUT_OF_BAND_ASTS (Ctrl/Y is left to DCL).
- With that, Get and Put are both cancelled by Ctrl/C part way (4-36%). Over
  localhost, Get runs at 1.5 MB/s on IA64 and 4.3-4.7 MB/s on x86 (where 10 MB
  finished before a Ctrl/C at 4 s; the step now waits 1 s), Put at 0.5-1.4 MB/s. No partial local file is left, and **the
  cancelled Put's delete of `big.bin` removed only the new, partial version: the
  server's BIG.BIN;1 is unchanged (MD5).** The VMS server deletes the highest version.
- x86 under load: each SSH connection starts a new SSHD222_* job, and with a
  compute-bound process at the same priority the banner took over 15 s, which
  vms.sh's ConnectTimeout turned into "timed out during banner exchange"; it is
  now 60 s. Once the sshd master sat in a loop for 95 minutes (seen by SHOW
  PROCESS, stopped by the user). After a full live run no sshd job outlives it
  (`probes/sshd_socks.com`), so VFTP's sessions do close.

## N7. Across nodes, and against a Unix server (2026-10-31)

Across nodes (`tools/live.sh <node> --server <node2>`, both directions; the test
key's `from=` widened to the two nodes by the user): the same session as N5 against
the other node's sshd. R.BIN byte for byte (MD5 the same on both nodes), R.TXT as
lines, Get and Put of 10 MB cancelled (Get 47%, Put 6% and 13%), the server's
BIG.BIN;1 unchanged.

Against OpenSSH 10.2 on Linux (`tools/live_unix.sh`, a private sshd under the
user's login, SFTP only, test key only, from the nodes only; started by the user):
- **A replacing Put lost the old file when cancelled.** A Unix server has no
  versions, so a Put of a name it has asks first; after Y, opening the file for
  writing truncated it and the cancel deleted it. Found by reading `put_one`, shown
  by `test_app`, fixed: the Put writes `name.vftp-part` and replaces the old file
  only when complete. Both nodes, from the server's journal (`internal-sftp -l
  INFO`, which logs to syslog, not to sshd's `-E` file): `big.bin.vftp-part`
  opened and removed on Ctrl/C, `big.bin` never opened to write (MD5 unchanged);
  "Replace them?" N wrote nothing; Y wrote `u.txt.vftp-part` and `posix-rename`d
  it over `u.txt`.
- Downloads: r.bin byte for byte (MD5), r.txt (LF lines) as VMS text records. U.TXT
  put as LF lines. Rename, mkdir (`SUBDIR/` shown with a slash), enter and leave
  it, delete: all as on VMS.
- Two `live_unix.sh` runs at once broke each other (the second start reset the
  server's files, the first one's end stopped the server); `unix_sftpd.sh` now
  refuses to start while one runs.

## N8. TCP/IP Services FTP 6.0 on IA64, seen from Linux (2026-10-31, probes/ftp_probe.py)

Run by the user (the password typed, in clear: AUTH TLS is listed by FEAT but
refused, "500 AUTH command unsuccessful", likely no certificate). x86's FTP
service is disabled. Before login: FEAT lists only AUTH TLS, PBSZ, PROT (each line
ending in "."); HELP lists USER PASS SITE MODE TYPE STRU PORT EPRT RETR STOR STOU
APPE ALLO RNFR RNTO NLST LIST DELE RMD MKD PWD CWD CDUP SYST STAT NOOP HELP ABOR
PASV EPSV QUIT. OPTS, SIZE, MDTM, REST are "502 unimplemented": **no
resume, no MLSD; sizes and dates come from LIST only.** SYST needs a login, then
"200 VMS OpenVMS V8.4 on node ...".

- **Paths are VMS specifications.** PWD: `257 "USER$ROOT:[iain]" is current
  directory.` (the login directory in the case the account has; after a CWD in
  upper case). `CWD [.SUB]`, `CWD SUB`, `CWD [-]`, `CDUP` and a full spec work;
  `CWD SUB/` and `CWD X.DIR` do not. CWD replies `250-CWD command successful.` /
  `250 New default directory is <spec>`.
- **LIST is DIRECTORY output**, all versions, highest first: a blank line,
  `Directory <spec>`, a blank line, entries, a blank line, `Total of N files,
  used/alloc blocks`. An entry: `NAME.TYPE;V  used/alloc  D-MMM-YYYY HH:MM:SS
  [GROUP,OWNER]  (S,O,G,W)`; a name too long for its column (about 20) is alone on
  its line and the rest follows on the next, indented. **Sizes are in blocks
  only.** Names keep their ODS-5 case and escapes (`^.CONFIG.DIR;1`,
  `My^_File^.tar.gz;1`). A LIST of nothing is `226` with "Total of 0 files";
  NLST of nothing is `550 file not found`. NLST gives `NAME.TYPE;V` per line.
- `SITE VMS` (not the default) turns LIST into DIRECTORY/FULL (File ID, Size,
  Created, Revised, record format, ...). `SITE UNIX` lists only the highest
  versions, and NLST then gives lower-case names with no version (`license.`,
  `build.dir`); PWD stays VMS. VFTP sends neither.
- **Text:** TYPE A STOR of CRLF lines makes a variable-length file (records); TYPE
  A RETR gives the records as CRLF lines; TYPE I RETR of the same file gives the
  record bytes with **no separators at all** (65 bytes for 4 lines). A 300-byte
  line goes and comes back whole. BUILD_VMS.COM (Stream_LF, put there by
  sftp) comes in TYPE I as its bytes, LF after each line, 3658 as the 150 reply
  said. So TYPE I of a text file is useful only for stream files: VFTP gets and
  puts text in TYPE A. **Binary:** TYPE I STOR makes fixed 512-byte records; RETR
  in TYPE I gives the bytes back exactly (1031 of 1031); in TYPE A, the last
  record comes back padded with blanks to 512.
- The 150 reply to RETR names the file with its version and its size: `150
  Opening data connection for USER$ROOT:[...]t.txt;2 (192.0.2.20,33202) (74
  bytes)` (an estimate for text: 4 lines of 65 bytes plus CRLFs is 73). The 150
  reply to STOR names the file it makes, with its version.
- **A STOR of an existing name makes a new version** (`t.txt;2`); **RNTO onto an
  existing name too** (`moved.bin` became `t.txt;3`): nothing is ever replaced.
- **DELE needs a version**: `DELE x` is "550 The file specification must contain a
  version number (even if wildcarded)". RMD takes `SUB` (not `SUB.DIR;1`).
- **Names are sent as plain names; the server adds the ODS-5 escapes.** `STOR My
  File.tar.gz` made `My^_File^.tar.gz;1`, and `DELE My^_File^.tar.gz;1` failed
  as `My^^_File^^^.tar.gz;1`: the escapes were escaped again. So a name from the
  listing is unescaped before it goes in a command. `MKD a]b` made `a^]b.DIR`.
- PASV: `227 Entering Passive Mode (192,0,2,10,236,21)`; EPSV: `229 Entering
  Extended Passive Mode (|||60438|)`. Errors are two-line 550s: `550-Failed to
  open ...` / `550 file not found`. STOU without a data connection waits for one
  (the first probe run hung there). Each FTP login leaves a
  TCPIP$FTP_SERVER.LOG in the account's login directory (the server's own).

## N9. VFTP's FTP client by hand on x86 (2026-10-31, the user)

The user ran VFTP on x86 against its own FTP server (TCP/IP Services 6.0, now
enabled): Protocol FTP, localhost, the user's password. Reported working. VFTP's log
(SYS$LOGIN:VFTP.LOG) shows the D10 question both ways: N first ("DISCONNECT
iain@localhost (password not sent in clear)", nothing sent), then Y and "LOGIN
iain@localhost:21 by password". It shows no GET, PUT, DELETE, RENAME or MKDIR, so
transfers and changes over FTP are still to be tried (D11's live test).
(`TYPE/TAIL` of the Stream_LF log fails with RMS-F-ORG; plain TYPE reads it.)

## N10. VFTP over FTP to each node's own server (2026-10-31, tools/live_ftp.sh)

Logged in as the test account VFTPTEST (D11) to localhost:21 on x86 and on IA64
(TCP/IP Services FTP 6.0 on both), the password from tools/live_ftp_pass.
- **The account:** TCP/IP Services counts an FTP login as remote interactive: with
  /NOREMOTE the server's log says "%TCPIP-E-FTP_LOGFAL, remote interactive login
  failure" and VFTP shows "530 Login incorrect". AUTHORIZE's ADD also copies
  DEFAULT's DisUser flag: /FLAGS=NODISUSER is needed. Both set by the user.
- D10: N disconnects and sends nothing ("password not sent in clear"); Y logs in.
  The password is in no output (PTYDRIVE logs step labels only; the field shows
  stars; the filled key script is deleted from the node).
- Get of R.BIN (TYPE I) byte for byte by MD5, R.TXT (TYPE A) as records; Get and Put
  of a 10 MB BIG.BIN cancelled by Ctrl/C (no partial local file; the server's
  BIG.BIN;1 unchanged by MD5, no new version left). U.TXT put twice: u.txt;1 and
  ;2 (a new version, no replace question, as on SFTP). MKD, CWD into SUBDIR and
  back up (VMS specs: USER$ROOT:[VFTPTEST.SUBDIR]), RNFR/RNTO, DELE with the
  version: all as the log says. Localhost moves 10 MB in about 1 s, so the cancel
  step waits 0.3 s.
- Seen on the screens and fixed: the remote pane showed sizes as blocks x 512 and
  versions lowest first, and the attributes Unix permissions; a VMS server's pane
  now shows blocks, the highest version first, and the protection (vf_pane.vms).
  The title and status line said SFTP on an FTP connection.
- On x86 the workdir and [VFTPTEST] are on different disks: RENAME cannot move a
  file between them (the test's setup now copies).

## N11. VFTP over FTP to a Unix server (2026-11-01, tools/live_unix_ftp.sh)

pyftpdlib 2.0.1 on this Linux machine (tools/unix_ftpd.py, in out/ftpd-venv; one
login, vftptest with the test password; only from the nodes; run by the user), from
x86 and from IA64, one after the other. SYST "215 UNIX Type: L8", PWD "/": Unix
paths. LIST is ls -l lines, as test_ftp.c has them. All ten checks on both nodes:
- The replacing Put that is cancelled: the server's log has only `STOR
  big.bin.vftp-part` and `DELE big.bin.vftp-part`; big.bin is unchanged by MD5.
- "Replace them?" N writes nothing; Y: `STOR u.txt.vftp-part`, `RNFR
  u.txt.vftp-part`, `RNTO u.txt` (pyftpdlib's RNTO replaces, so no DELE).
- r.bin byte for byte (TYPE I), r.txt as records (TYPE A); U.TXT as LF lines;
  MKD, CWD in and out, rename (SIZE first: 550, so no file is in the way), DELE.
- pyftpdlib logs `PASS ******`; the password is in no output.

## N12. Two runs on one node at once (2026-11-01)

A plain FTP run on x86 (tools/live_ftp.sh) started while the user's FTPS run
(live_unix_ftp.sh x86 --tls) was building there. Each build clears [.BUILD], so
VFTP.EXE was not there when the first run typed "vftp"; PTYDRIVE then typed the
whole key script at the DCL prompt, which echoed it, the test password too, into
the saved output (out/, git-ignored; masked; the harness's "password in no output"
check caught it). And editing live_unix_ftp.sh while it ran made bash read it at a
shifted offset ("line 59: dev/null: No such file"), start a second server, stop,
and leave the first one running. Now: tools/node_lock.sh, taken by build.sh and
every live script, allows one run per node (a second stops at once, saying so);
test_live.com types nothing if VFTP.EXE is missing; and a script is not edited
while it runs.

## N13. FTPS against pyftpdlib with TLS required (2026-11-01, live_unix_ftp.sh --tls)

pyftpdlib 2.0.1 with pyOpenSSL 24.2.1, a test-only self-signed EC certificate
(CN=vftp-test-ftps, 30 days), tls_control_required and tls_data_required: nothing
in clear is accepted. From x86 and IA64 (VSI SSL3, OpenSSL 3.0), run by the user one
after the other; all 13 checks on both:
- AUTH TLS (234), the handshake, the certificate asked about on the host-key screen:
  N disconnects; Y adds "[host]:2121 <SHA-256>" to [.SSH]vftp_known_tls, the same
  fingerprint as openssl x509 -fingerprint -sha256 gives. No D10 question. After the
  login PBSZ 0 and PROT P; every LIST, RETR and STOR on a TLS data connection (the
  server would refuse a plain one), reusing the control connection's session.
- A second login in the same run asks nothing (known_tls matches).
- The rest as N11 (replace by temporary name, N/Y, Get/Put/cancel, mkdir, rename,
  delete), over TLS.
The nodes' own FTP servers refuse AUTH TLS (500): plain FTP with the D10 question,
re-run after the TLS work on both nodes (live_ftp.sh, all 7 checks).

## N14. Whole directories, both nodes, by SFTP and FTP (2026-11-01, tools/live_dirs.sh)

VFTP puts `[.LIVE.DIRS]` (A.TXT;1 and ;2, `[.SUB]B.BIN` binary, `[.SUB.DEEP]C.TXT`,
an empty `[.EMPTY]`) to a server on the same node twice, then gets the tree back
into `[.LIVE.BACK]` (test_live.com DIRS). The server: the node's own sshd into
`[.LIVE.REMOTE]` (key login), and its own FTP server as VFTPTEST (D11). All checks
on x86 and IA64, both protocols:
- Put: the four directories made (the empty one too), only A.TXT;2 sent, records the
  same on the server (DIFFERENCES), C.TXT three levels down. Directory names go as
  `dirs`, `sub`: both servers keep the lower case on ODS-5.
- Put again: no replace question and no MKDIR (the directories are found, listed
  `dirs.DIR;1` by either server); every file a new version (`a.txt;2`).
- Get back: the directories made in `[.LIVE.BACK]` (`[.BACK.dirs.sub.deep]`), only
  the highest of the two versions on the server (`a.txt;2` asked for by name, `;1`
  made locally), text as the same records, B.BIN byte for byte (DIFFERENCES/HEX).
- Over SFTP to VSI's server, a directory path works as `.../dirs` and as
  `.../dirs.DIR;1` (the pane enters it that way); a file path takes its version
  (`.../a.txt;2`).
- Two things in the tests, not VFTP. DIFFERENCES takes the device and directory the
  second file lacks from the first, so a relative spec found the wrong disk when
  [VFTPTEST] is on another (x86). And F$SEARCH asked the same spec again goes on
  from its last answer instead of looking afresh: the empty-out loops of
  test_plat.com and live_server.com ("anything left? delete again") stopped after
  two passes, leaving `dirs.DIR` in [VFTPTEST], which shifted the rows the FTP
  test's key script counts (its R.TXT check failed). They now make a fixed eight
  passes (one per level), and live_server.com stops if anything is left.

## N15. The sites file on both nodes, by SFTP and FTP (2026-11-01, tools/live_dirs.sh)

At the end of the directory test (N14) the key script presses L, A, names the
connection `livesite`, leaves VFTP and starts it again as `vftp livesite`. SYS$LOGIN
is `[.LIVE]` while VFTP runs, so the file is `[.LIVE]VFTP.SITES`, never the real one.
On x86 and IA64, by SFTP (key) and FTP (VFTPTEST, password typed again):
- The file is a variable-record text file TYPE shows as written: VFTP's three comment
  lines, `[livesite]`, protocol, host, port, user, key, remote, and local (the local
  pane's `[.LIVE.BACK]`). No password line; the FTP run's password is in no output.
- `vftp livesite` opens the Open form filled ("Open a connection: the site
  livesite"), the local pane in `[.LIVE.BACK]`, the cursor on the password. SFTP:
  Do logs in by key (the host key known from the first login); FTP: the password,
  Do, and the D10 question again. The log has two LOGIN lines each time, the
  second into the saved remote directory.
- Seen there: an FTP site was saved with the form's default key file, which FTP
  never uses; it is now left out of an FTP site (Linux tests only since).

## N16. Changing permissions (GOLD A) on the nodes (2026-11-01, tools/live_dirs.sh)

In the directory test, after the Get, GOLD A on the server's R.TXT;1 (x86 and IA64 alike):
- By SFTP, `640`: VSI's server takes setstat (permissions only) and R.TXT;1 is then
  `(S:RWD,O:RWD,G:R,W:)` on the server: owner rw is RWD, the system gets what the
  owner has, group r is R, world nothing. The pane, read again, lists it as
  -rw-r----- (0640), as the message says. Logged `CHMOD .../R.TXT;1 0640`.
- By FTP to the VMS server: refused before anything is sent ("A VMS FTP server
  keeps VMS protection"); a VMS server's listing has VMS protection, not modes (N8).
  Whether TCP/IP Services takes SITE CHMOD was not tried (N8 saw SITE VMS and
  SITE UNIX); VMS protection from here would be its own piece of work.
The Unix servers, GOLD A `go-r` on moved.txt, checked with stat on Linux (run by the
user, both nodes): setstat to OpenSSH (live_unix.sh), SITE CHMOD to pyftpdlib, plain
(live_unix_ftp.sh, all 11 checks) and with TLS (--tls, all 14). The test server
makes files 0664, so go-r gave 0620 (`SITE CHMOD 620 moved.txt`).
- Not explained yet: in live_unix.sh on x86 (IA64 passed all 9), the Get of r.txt
  failed at once ("0 copied, 1 failed: r.txt: .../r.txt:", cut at 80 columns) and
  View of the same file a moment later worked. The server's journal has one open
  of r.txt, the View's; a 60 s timeout cannot have struck that fast. VFTP did not
  log a Get that fails at the open: it does now (and a Put, and a failed close),
  with the whole error, for the next run. The ssh to x86 had timed out an hour
  before ("server not responding").
  Run again on x86 (by the user, 1-NOV-2026): all 9 checks passed, r.txt among them,
  and the log has no FAILED line but the two cancels the test makes. Not seen again;
  if it comes back, VFTP$LOG now has the reason.

## N17. F5, Backspace, and an SSH password login (2026-11-01, x86 and IA64)

- F5 arrives as SMG$K_TRM_F5 (285) from the PTY's `ESC [15~`, and DEL (127) as
  itself: live_dirs.sh's SFTP script leaves the server directory by DEL and gets
  the tree by F5, all 11 checks on both nodes. SMGDEF also has SMG$K_TRM_CTRLH and
  SMG$K_TRM_BS (8), which go up a directory too.
- A password login by SSH as VFTPTEST to localhost fails on both nodes, from VFTP
  ("Waiting for keyboard USERAUTH response") and from probes/probe_sshpass.c, which
  tries each method on a fresh session: the "password" method, sshd closes the
  connection (-13, LIBSSH2_ERROR_SOCKET_DISCONNECT); keyboard-interactive is
  refused (-18) without a single prompt. sshd's log (SSH$ROOT:[VAR]<node>_localhost_
  <pid>.log) has "Failed password for vftptest from 127.0.0.1". The same password
  logs in by FTP; in upper case it fails the same way. sshd_config has no Allow/Deny
  lines and nothing about passwords; the UAF gives VFTPTEST Network and Remote
  access, no Local, Batch or Dialup, Pwdminimum 15 against a 7-character password,
  and no intrusion record names it. Not explained yet: needs the system manager.
