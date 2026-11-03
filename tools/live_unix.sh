#!/usr/bin/env bash
# live_unix.sh <node> [--scp] [steps...] - VFTP on <node> against an OpenSSH SFTP
# server on this Linux machine (tools/unix_sftpd.sh, started here and stopped at the
# end), then the server-side checks and the screens after the named steps (all if
# none). With --scp the same by SCP (D15), the server running only VFTP's commands
# (tools/unix_scp_gate.py), whose log the checks read instead of the SFTP journal.
# The server's scratch directory out/unix/remote starts with r.txt, r.bin and a
# 10 MB big.bin of random bytes (not the node's BIG.BIN), so a replace shows.
# Output kept in out/live-unix[-scp]-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: live_unix.sh <node> [--scp] [step...]}; shift
scp=
if [ "${1:-}" = --scp ]; then scp=1; shift; fi
[ -f "$top/tools/live_key" ] || { echo "live_unix.sh: no tools/live_key (the test's login key)" >&2; exit 2; }
. "$top/tools/node_lock.sh"
node_lock "$node" 9
since=$(date '+%Y-%m-%d %H:%M:%S')
read -r HOST PORT RDIR < <("$top/tools/unix_sftpd.sh" start ${scp:+scp})
trap '"$top/tools/unix_sftpd.sh" stop' EXIT
rm -rf "${RDIR:?}"/* "$RDIR"/.[!.]* 2>/dev/null || true
printf 'A text file on the Unix server.\nIts second line.\n' > "$RDIR/r.txt"
head -c 3000 /dev/urandom > "$RDIR/r.bin"
head -c 10485760 /dev/urandom > "$RDIR/big.bin"
big=$(md5sum < "$RDIR/big.bin" | cut -d' ' -f1)
rbin=$(md5sum < "$RDIR/r.bin" | cut -d' ' -f1)
mkdir -p "$top/out/$node"
proto=()                                          # SCP: set on the form first
[ -n "$scp" ] && proto=(-e '/^3 open | o$/a 2 protocol | \\e[A\\x05\\x15SCP\\e[B' -e '/^[0-9.]* resume /d')
sed -e "s/@HOST@/$HOST/" -e "s/@PORT@/$PORT/" -e "s/@RUSER@/$(id -un)/" -e "s|@RDIR@|$RDIR|" "${proto[@]}" \
    "$top/tests/vms/live_unix.tpl" > "$top/out/$node/live_unix_keys.txt"
"$top/tools/build.sh" "$node" TEST
"$top/tools/vms.sh" "$node" put "$top/out/$node/live_unix_keys.txt" -- tests/vms > /dev/null
"$top/tools/vms.sh" "$node" run tests/vms/test_plat.com > "$top/out/plat-$node.txt" 2>&1
grep -E 'PLAT:|FAIL' "$top/out/plat-$node.txt"
"$top/tools/vms.sh" "$node" put "$top/tools/live_key" -- tests/vms > /dev/null   # test_live.com moves it
out=$top/out/live-unix${scp:+-scp}-$node.txt
"$top/tools/vms.sh" "$node" run tests/vms/test_live.com '[.tests.vms]live_unix_keys.txt' UNIX "[$HOST]:$PORT" > "$out" 2>&1
grep -E '^LIVE' "$out" || true

echo "==== the server's scratch directory"
ls -la "$RDIR"
ok() { if eval "$2"; then echo "UNIX: $1"; else echo "UNIX: FAILED: $1"; fi; }
after=", and after the Put of its download"; [ -n "$scp" ] && after=
ok "big.bin as it was after the cancelled replacing Put$after" \
   '[ "$(md5sum < "$RDIR/big.bin" | cut -d" " -f1)" = "$big" ]'
ok "no temporary .vftp-part file left" '! ls "$RDIR" | grep -q vftp-part'
ok "no local half file left" '! grep -aqi "^big\.bin_vftp-part;" "$out"'
if [ -z "$scp" ]; then                            # resume (D16)
    ok "the cancelled Get kept a half file, the next one went on from it" \
       'grep -aqi "KEPT .*big\.bin_VFTP-PART;1 to resume" "$out" && grep -aq "GET .*/big.bin RESUMED at " "$out"'
    ok "big.bin downloaded by resuming, byte for byte" 'grep -aqi "^LIVE-MD5 big.bin $big" "$out"'
    ok "the cancelled Put kept big.bin.vftp-part; N started it again" \
       'grep -aq "KEPT .*/big.bin.vftp-part to resume" "$out" && grep -aq "DELETE .*/big.bin.vftp-part (a half file, started again)" "$out"'
else
    ok "SCP: no half file kept, nothing resumed" '! grep -aq "KEPT \|RESUMED" "$out"'
fi
ok "r.bin downloaded byte for byte" 'grep -qi "^LIVE-MD5 r.bin $rbin" "$out"'
ok "r.txt downloaded as text lines" 'grep -qx "A text file on the Unix server." "$out" && grep -qx "Its second line." "$out"'
ok "U.TXT put as LF lines, then renamed moved.txt" \
   '[ "$(cat "$RDIR/moved.txt" 2>/dev/null)" = "$(printf "Written on VMS, put to another server.\nIts second line.")" ]'
if [ -n "$scp" ]; then
    gate=$top/out/unix/scp-gate.log
    echo "==== the server's commands (scp-gate.log)"; cut -c1-150 "$gate"
    ok "the server ran only VFTP's commands (none refused)" '[ -s "$gate" ] && ! grep -q "^REFUSE" "$gate"'
    ok "the replace went by mv -f" 'grep -qF "ALLOW sh -c: mv -f -- '"'"'u.txt.vftp-part'"'"' '"'"'u.txt'"'"'" "$gate"'
    ok "big.bin itself never sent to" '! grep -qE "^ALLOW scp -p?t .big\.bin.?$" "$gate"'
    how="chmod"
else
    # internal-sftp, not chrooted, logs each operation to the journal, not sshd's -E file
    journalctl -t internal-sftp --since "$since" --no-pager -o cat 2>/dev/null |
        grep -E 'open .*WRITE|remove|rename' | sed "s|$RDIR/||g" > "$top/out/unix/sftp-ops-$node.txt" || true
    echo "==== the server's writes (journal)"; cat "$top/out/unix/sftp-ops-$node.txt"
    ok "the replace went by posix-rename" 'grep -q "posix-rename old \"u.txt.vftp-part\" new \"u.txt\"" "$top/out/unix/sftp-ops-$node.txt"'
    ok "big.bin itself never opened to write" '! grep -q "open \"big.bin\" flags WRITE" "$top/out/unix/sftp-ops-$node.txt"'
    how="setstat"
fi
ok "GOLD A go-r: moved.txt has no group or world read ($how)" \
   'm=$(stat -c %a "$RDIR/moved.txt") && [ $(( 0$m & 044 )) = 0 ] && [ $(( 0$m & 0400 )) != 0 ]'
ok "SUBDIR made, r.txt deleted" '[ -d "$RDIR/SUBDIR" ] && [ ! -e "$RDIR/r.txt" ]'

sed -n '/^---- startup/,/^---- end/p' "$out" > "${out%.txt}.pty"
args=()
for s in "$@"; do args+=(--step "$s"); done
python3 "$top/tools/vtscreen.py" "${args[@]}" "${out%.txt}.pty"
