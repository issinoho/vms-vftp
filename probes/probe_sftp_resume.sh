#!/usr/bin/env bash
# probe_sftp_resume.sh <node> - what a node's sshd does with resume's pieces (D16), by
# OpenSSH's sftp from Linux in a scratch directory [.PROBE_RESUME] of the workdir:
# a .vftp-part name, a rename onto a name that exists and onto one that does not,
# a write at an offset (reput) and a read from one (reget), each checked by md5.
# Then the directory as DCL sees it, and the scratch directory is removed.
set -uo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: probe_sftp_resume.sh <node>}
read -r _ _ HOST PORT USER _ SFTPDIR _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
key=${VMS_SSH_KEY:-$HOME/.ssh/vms_ed25519}
w=$(mktemp -d); trap 'rm -rf "$w"' EXIT
head -c 300000 /dev/urandom > "$w/full.bin"; head -c 100000 "$w/full.bin" > "$w/half.bin"
cp "$w/half.bin" "$w/got.bin"
s() { printf '%s\n' "$@" | sftp -P "$PORT" -i "$key" -o BatchMode=yes -b - "$USER@$HOST" 2>&1 |
      grep -vE '^ *Welcome to|^ *$' | sed 's/^/SFTP: /'; }
d=$SFTPDIR/probe_resume
s "-mkdir $d" "cd $d" "put $w/full.bin a.bin" "put $w/half.bin a.bin.vftp-part" "ls -l" \
  "rename a.bin.vftp-part a.bin" "put $w/half.bin b.bin.vftp-part" "rename b.bin.vftp-part b.bin" \
  "put $w/half.bin c.bin" "reput $w/full.bin c.bin" "get c.bin $w/c.bin" \
  "reget c.bin $w/got.bin" "ls -l"
echo "MD5: full $(md5sum < "$w/full.bin" | cut -c1-8) c.bin after reput $(md5sum < "$w/c.bin" | cut -c1-8)" \
     "got.bin after reget of c.bin $(md5sum < "$w/got.bin" | cut -c1-8)"
"$top/tools/vms.sh" "$node" dcl 'directory/size=all/date [.probe_resume]'
s "cd $d" "-rm *" "cd .." "-rmdir probe_resume"
