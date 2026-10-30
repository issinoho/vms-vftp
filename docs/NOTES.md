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
