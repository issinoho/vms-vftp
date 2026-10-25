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

## D6. Text and binary transfers (2026-10-25, proposed)

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

## D7. Host keys and keys: VSI OpenSSH's files (2026-10-25, proposed)

VFTP reads and adds to the known_hosts that VSI's ssh uses, and offers the keys
VSI's ssh would (`id_ed25519`, `id_ecdsa`, `id_rsa`), so trust and keys are shared.
An unknown host shows its key fingerprint (SHA-256, as ssh does) and asks; a
changed key refuses to connect. The files are in `SYS$LOGIN:[.SSH]` (N3), and libssh2
reads them as they are.

## D8. Local files: VMS specifications (2026-10-25, proposed)

The local pane shows VMS specifications (`DKA0:[IAIN]NOTES.TXT;1`), the latest
version by default and every version on request, as VTREE. A downloaded file is
always a new version, never a replacement. Remote names that are not valid on
ODS-5 (or on ODS-2 on such a disk) are changed by a stated rule and the new name
is shown before the transfer.

## D9. No stored passwords (2026-10-25, proposed)

The sites file (`VFTP$SITES`, default `SYS$LOGIN:VFTP.SITES`) holds protocol, host,
port, user, key file and starting directories, never a password. Passwords are
typed each time and kept only in memory for the connection.
