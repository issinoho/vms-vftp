# CLAUDE.md

Working notes for vms-vftp: a full-screen FTP, SFTP and SCP client for OpenVMS
(x86-64 and IA64), with vms-vtree's and vms-vlmf's look. `docs/PLAN.md` is the phased
plan; `docs/DECISIONS.md` records every decision and the alternatives considered;
`docs/NOTES.md` records what was observed on the nodes and how. Read them before
starting work. Work phase by phase, and stop and ask at each phase exit.

SFTP and SCP come from libssh2, ported in `~/projects/vms-libssh2` (D3) and linked
statically; FTP is our own code (D4).

## Ground rules (from the sibling vms-* repos)

- **Use `tools/vms.sh`** for remote work. Never raw `ssh host cmd`, never `WAIT` over
  ssh. Run long jobs with `run_in_background`. Pass DCL specs to it in single quotes;
  one DCL command per `dcl` call (use `run` with a procedure for more).
- **Committed files must not contain real node details** (they live in the git-ignored
  `tools/nodes.conf`), host addresses, credentials, passwords or private keys. Test
  passwords and keys are made for the test and never committed.
- **Never store a password** (D9), not in the sites file, logs or test scripts.
- **Never overwrite or delete a user's file without a confirm screen** (default No); a
  download makes a new version (D8). Tests work in scratch directories on both ends.
- Prefer a 20-line probe in `probes/` over speculation. Don't claim something works
  until a test on the node shows it.

## Hard constraints

- **C99, VSI C, 32-bit pointers.** No C11, no VLAs, no anonymous unions. The Linux
  build uses `-std=c99 -pedantic -Wall -Wextra -Werror`. Under `/STANDARD=C99`,
  `fopen` takes no RMS keyword arguments.
- **`src/core/` does no I/O** (VLMF D3). `src/plat/vf_platform.h` will be the whole OS
  surface; `src/plat/vms/` implements it, `src/plat/stub/` fakes it for Linux tests.
- **The screen goes through `vf_ansi.c`**, SMG$ is for keys and broadcasts (VTREE D11).

## Testing

`make test` (Linux). `tools/push.sh <node>` then `tools/vms.sh <node> dcl '@build_vms TEST'`
runs the same tests natively. Later phases add `tools/live.sh` (PTYDRIVE, `tools/vtscreen.py`)
as in VLMF.

## Commits

Commit in logical steps with messages that explain why. Record what was verified and how.
Don't push unless the user asks; there is no GitHub repository yet.
