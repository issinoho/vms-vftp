#!/usr/bin/env bash
# live_unix.sh <node> [steps...] - VFTP on <node> against an OpenSSH SFTP server on
# this Linux machine (tools/unix_sftpd.sh, started here and stopped at the end), then
# the server-side checks and the screens after the named steps (all if none).
# The server's scratch directory out/unix/remote starts with r.txt, r.bin and a
# 10 MB big.bin of random bytes (not the node's BIG.BIN), so a replace shows.
# Output kept in out/live-unix-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: live_unix.sh <node> [step...]}; shift
[ -f "$top/tools/live_key" ] || { echo "live_unix.sh: no tools/live_key (the test's login key)" >&2; exit 2; }
. "$top/tools/node_lock.sh"
node_lock "$node" 9
since=$(date '+%Y-%m-%d %H:%M:%S')
read -r HOST PORT RDIR < <("$top/tools/unix_sftpd.sh" start)
trap '"$top/tools/unix_sftpd.sh" stop' EXIT
rm -rf "${RDIR:?}"/* "$RDIR"/.[!.]* 2>/dev/null || true
printf 'A text file on the Unix server.\nIts second line.\n' > "$RDIR/r.txt"
head -c 3000 /dev/urandom > "$RDIR/r.bin"
head -c 10485760 /dev/urandom > "$RDIR/big.bin"
big=$(md5sum < "$RDIR/big.bin" | cut -d' ' -f1)
rbin=$(md5sum < "$RDIR/r.bin" | cut -d' ' -f1)
mkdir -p "$top/out/$node"
sed -e "s/@HOST@/$HOST/" -e "s/@PORT@/$PORT/" -e "s/@RUSER@/$(id -un)/" -e "s|@RDIR@|$RDIR|" \
    "$top/tests/vms/live_unix.tpl" > "$top/out/$node/live_unix_keys.txt"
"$top/tools/build.sh" "$node" TEST
"$top/tools/vms.sh" "$node" put "$top/out/$node/live_unix_keys.txt" -- tests/vms > /dev/null
"$top/tools/vms.sh" "$node" run tests/vms/test_plat.com > "$top/out/plat-$node.txt" 2>&1
grep -E 'PLAT:|FAIL' "$top/out/plat-$node.txt"
"$top/tools/vms.sh" "$node" put "$top/tools/live_key" -- tests/vms > /dev/null   # test_live.com moves it
out=$top/out/live-unix-$node.txt
"$top/tools/vms.sh" "$node" run tests/vms/test_live.com '[.tests.vms]live_unix_keys.txt' UNIX "[$HOST]:$PORT" > "$out" 2>&1
grep -E '^LIVE' "$out" || true

echo "==== the server's scratch directory"
ls -la "$RDIR"
ok() { if eval "$2"; then echo "UNIX: $1"; else echo "UNIX: FAILED: $1"; fi; }
ok "big.bin unchanged after the cancelled replacing Put" '[ "$(md5sum < "$RDIR/big.bin" | cut -d" " -f1)" = "$big" ]'
ok "no temporary .vftp-part file left" '! ls "$RDIR" | grep -q vftp-part'
ok "r.bin downloaded byte for byte" 'grep -qi "^LIVE-MD5 r.bin $rbin" "$out"'
ok "r.txt downloaded as text lines" 'grep -qx "A text file on the Unix server." "$out" && grep -qx "Its second line." "$out"'
ok "U.TXT put as LF lines, then renamed moved.txt" \
   '[ "$(cat "$RDIR/moved.txt" 2>/dev/null)" = "$(printf "Written on VMS, put to another server.\nIts second line.")" ]'
# internal-sftp, not chrooted, logs each operation to the journal, not sshd's -E file
journalctl -t internal-sftp --since "$since" --no-pager -o cat 2>/dev/null |
    grep -E 'open .*WRITE|remove|rename' | sed "s|$RDIR/||g" > "$top/out/unix/sftp-ops-$node.txt" || true
echo "==== the server's writes (journal)"; cat "$top/out/unix/sftp-ops-$node.txt"
ok "the replace went by posix-rename" 'grep -q "posix-rename old \"u.txt.vftp-part\" new \"u.txt\"" "$top/out/unix/sftp-ops-$node.txt"'
ok "big.bin itself never opened to write" '! grep -q "open \"big.bin\" flags WRITE" "$top/out/unix/sftp-ops-$node.txt"'
ok "SUBDIR made, r.txt deleted" '[ -d "$RDIR/SUBDIR" ] && [ ! -e "$RDIR/r.txt" ]'

sed -n '/^---- startup/,/^---- end/p' "$out" > "${out%.txt}.pty"
args=()
for s in "$@"; do args+=(--step "$s"); done
python3 "$top/tools/vtscreen.py" "${args[@]}" "${out%.txt}.pty"
