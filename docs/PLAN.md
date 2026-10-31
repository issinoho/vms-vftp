# vms-vftp: a full-screen FTP, SFTP and SCP client for OpenVMS

## Goal

Upload, download and manage files on another system from one full-screen program
with the look and keys of VTREE and VLMF: framed panes, XTree Gold's colours,
one-letter commands, PF1 GOLD, LK201 keys. Three protocols: SFTP and SCP (through
libssh2, ported as the family's vms-libssh2, D3) and FTP (our own client, D4).
Same conventions as the other vms-* projects: `tools/vms.sh`, DECISIONS.md,
NOTES.md, PCSI kits for X86VMS and I64VMS, the family release process.

## The screen (80 or 132 columns, 24 or more rows)

```
 VFTP  sftp iain@host:/home/iain                              25-OCT-2026 10:12
┌ LOCAL DKA0:[IAIN] ─────────────────┬ REMOTE /home/iain ─────────────────────┐
│ LOGIN.COM;3           2  24-OCT    │ .bashrc               3K  12-Oct 09:14  │
│◆NOTES.TXT;1          14  25-OCT    │◆data/                     20-Oct 18:02  │
│ [.WORK]                  20-OCT    │ notes.txt            14K  25-Oct 10:01  │
│ ...                                │ ...                                     │
├────────────────────────────────────┴─────────────────────────────────────────┤
│ 12 files 340 blocks  tagged 2 (28 blocks)        Mode AUTO   sftp connected  │
└──────────────────────────────────────────────────────────────────────────────┘
 COMMANDS  Get Put Copy Delete Rename Mkdir Tag Open Site Mode View Attributes
 COMMANDS  PF1 GOLD   Tab other pane   Return enter dir   Help   F10 exit
```

Two panes side by side (D2): local on the left, remote on the right, the current
one with the bright frame and the highlight bar. ◆ = tagged. A strip under the
panes holds the statistics, transfer mode and connection state; during a
transfer it shows the progress bar, rate and time left. At 132 columns the panes
show full dates, protections or permissions, and owners.

## Keys (proposed; settled in Phase 1)

| Key | Action |
|---|---|
| Arrows, Prev/Next Screen | move; GOLD Up/Down first/last |
| Tab | the other pane |
| Return, Select | enter a directory; on a file, View |
| `-`, Backspace | up one directory |
| `O` | Open: connect (form: protocol, host, port, user, password or key) |
| GOLD `O` | close the connection |
| `G` / `P` | Get (download) / Put (upload) the tagged or current files |
| `C`, F5 | Copy: the tagged or current files to the other pane (Get or Put) |
| `D`, Remove | delete (confirm, default No) |
| `R` | rename (or move, by typing a path) |
| `M` | make a directory |
| `T`, Space | tag / untag; GOLD `T` tag all, GOLD `U` untag all, `*` tag by wildcard |
| `V` | view a text file (local, or remote fetched to SYS$SCRATCH) |
| `A` | attributes: protection (local) or permissions (remote), dates, owner |
| `X` | transfer mode: AUTO, ASCII, BINARY (D6) |
| `S` | sort by name, size, date, type |
| `F`, Find | filter by wildcard |
| `L` | log in to a saved site (sites file, D9) |
| GOLD `W`, Ctrl/W | read both panes again; redraw |
| Help, PF2 | help |
| F10, Ctrl/Z | exit |

Forms: Up/Down/Tab/Return move between fields, Do or KP Enter is done, F10 cancels.
A transfer in progress is cancelled by Ctrl/C or F10 (partial file removed).

## Phase 0: scaffolding, probes, libssh2 (started 2026-10-25)

- Repo from vms-vlmf's tooling; VTREE's screen model, colour renderer and line
  editor (`src/core/vf_*`), building and passing on Linux and both nodes. Done.
- vms-libssh2 port (D3): libssh2 1.11.1 from the signed tarball, VSI C, against
  VSI's SSL3 (OpenSSL 3.0), as an object library for static linking; smoke test
  on both nodes against the nodes' own sshd (connect, host key, key and password
  auth, SFTP directory listing, get and put).
- Probes (NOTES): what VSI OpenSSH's sftp-server on VMS shows (paths, sizes of
  text files, attributes); the TCP/IP Services FTP server's LIST and MLSD replies;
  where VSI OpenSSH keeps known_hosts and keys (SYS$LOGIN:[.SSH], D7); reading
  and writing RMS record formats from C (D6).

Status 2026-10-25: vms-libssh2 builds on both nodes; handshake, host-key check
against VSI's known_hosts and ed25519 key login work (N3). SFTP needs a test login.

Exit: vms-libssh2's smoke test passes on both nodes; the user signs off this plan.

## Phase 1: SFTP

The two panes, local browsing (directories, every version or the latest, D8),
the Open form, host-key check (D7), password and key login, remote browsing,
Get/Put/Copy of tagged files with progress and cancel, text and binary modes,
delete, rename, make directory, tag, sort, filter, view, help. A transfer
never replaces a local file: it makes a new version. Core and app tested on Linux
against the stub (a fake remote file system), natively on both nodes, and end to
end under PTYDRIVE against each node's own sshd and the other node.

Status 2026-10-31: done. Tested on Linux (stub) and natively on both nodes, and
live under PTYDRIVE against each node's own sshd (N5, N6), the other node in both
directions and OpenSSH on Linux (N7). A replacing Put on a server without versions
now writes a temporary file and renames it over the old one (N7).
Carried forward: password login has been tried against the stub only (the tests
type no password; to be checked by hand with the user); F5 as Copy, Backspace as
up one directory, and `L` with the sites file (D9) are not built yet.

Exit: signed off by the user, 2026-10-31.

## Phase 2: FTP

Our own client (D4): control connection, USER/PASS, PASV and EPSV, TYPE A/I,
LIST parsing for Unix, VMS and Windows servers, MLSD where offered, REST for
resume, SITE for permissions. Tested against the IA64 node's TCP/IP Services FTP
server and a Linux server. FTPS (AUTH TLS through SSL3) if it is cheap by then.

## Phase 3: SCP and the rest

SCP through libssh2 (transfers) with listing and management by remote shell
commands (Unix servers only, as WinSCP's SCP mode does). Recursive transfers of
directories, resume of an interrupted transfer, the sites file (D9), attributes.

## Phase 4: kits and release

vms-libssh2 released first (its own kit, for other ports too); VFTP links it
statically (D3), so VFTP's kit needs only SSL3. Kits via `tools/kit.sh`,
`VFTP$STARTUP.COM` and `VFTP$SETUP.COM` as VLMF's, the family release process.

## Later ideas

Keeping VMS file attributes across VMS-to-VMS transfers (as a sidecar or by
VSI's own extensions, if the probes find any); a transfer queue run in the
background while browsing; FXP (server to server); compare and synchronise
directories; ssh-agent (none on VMS today).
