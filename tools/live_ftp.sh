#!/usr/bin/env bash
# live_ftp.sh <node> [steps...] - VFTP over FTP to <node>'s own FTP server, logged in
# as the test account VFTPTEST (D11) with the password in tools/live_ftp_pass (made
# for the test, git-ignored). The server side is the account's login directory,
# set up and checked by tests/vms/live_server.com. The filled key script holds the
# password: it is mode 600 here, and deleted on the node after the run. Then the
# screens after the named steps (all if none). Output in out/live-ftp-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: live_ftp.sh <node> [step...]}; shift
pass_file=$top/tools/live_ftp_pass
. "$top/tools/node_lock.sh"
node_lock "$node" 9
rdir=${VFTP_FTP_DIR:-'USER$ROOT:[VFTPTEST]'}
[ -f "$pass_file" ] || { echo "live_ftp.sh: no tools/live_ftp_pass (the test account's password)" >&2; exit 2; }
[ -f "$top/tools/live_key" ] || { echo "live_ftp.sh: no tools/live_key" >&2; exit 2; }
pass=$(head -n 1 "$pass_file")
[ -n "$pass" ] || { echo "live_ftp.sh: tools/live_ftp_pass is empty" >&2; exit 2; }
mkdir -p "$top/out/$node"
keys=$top/out/$node/live_ftp_keys.txt
cleanup() {
    rm -f "$keys"
    "$top/tools/vms.sh" "$node" dcl 'delete/nolog [.tests.vms]live_ftp_keys.txt;*' > /dev/null 2>&1 || true
}
trap cleanup EXIT
umask 077
# the password goes in with sed: escape what sed would read as special
pesc=$(printf '%s' "$pass" | sed -e 's/[\\&|]/\\&/g')
sed -e "s|@PASS@|$pesc|" -e "s|@RDIR@|$rdir|" "$top/tests/vms/live_ftp.tpl" > "$keys"
umask 022
"$top/tools/build.sh" "$node" TEST
"$top/tools/vms.sh" "$node" run tests/vms/test_plat.com > "$top/out/plat-$node.txt" 2>&1
grep -E 'PLAT:|FAIL' "$top/out/plat-$node.txt"
srv=$top/out/server-ftp-$node.txt
"$top/tools/vms.sh" "$node" run tests/vms/live_server.com SETUP "$rdir" > "$srv" 2>&1
srv_rbin=$(sed -n 's/^LIVE-MD5 r.bin //p' "$srv")
srv_big=$(sed -n 's/^LIVE-MD5 big.bin //p' "$srv")
[ -n "$srv_rbin" ] && [ -n "$srv_big" ] || { echo "live_ftp.sh: server setup failed" >&2; cat "$srv" >&2; exit 1; }
"$top/tools/vms.sh" "$node" put "$keys" -- tests/vms > /dev/null
rm -f "$keys"
"$top/tools/vms.sh" "$node" put "$top/tools/live_key" -- tests/vms > /dev/null   # test_live.com moves it
out=$top/out/live-ftp-$node.txt
"$top/tools/vms.sh" "$node" run tests/vms/test_live.com '[.tests.vms]live_ftp_keys.txt' VMS localhost > "$out" 2>&1
cleanup
"$top/tools/vms.sh" "$node" run tests/vms/live_server.com CHECK "$rdir" > "$srv" 2>&1
grep -aE '^LIVE' "$out" || true
ok() { if eval "$2"; then echo "FTP: $1"; else echo "FTP: FAILED: $1"; fi; }
ok "the D10 question: N sent nothing, Y logged in" \
   'grep -aq "password not sent in clear" "$out" && grep -aq "CLEAR PASSWORD agreed for vftptest@localhost:21" "$out"'
ok "R.BIN downloaded byte for byte (TYPE I)" 'grep -aqi "^LIVE-MD5 r.bin $srv_rbin" "$out"'
ok "R.TXT downloaded as text lines (TYPE A)" 'grep -aqx "A variable-length text file on the server." "$out"'
ok "server BIG.BIN;1 unchanged after the cancelled Put" 'grep -aqi "^LIVE-MD5 big.bin $srv_big" "$srv"'
ok "U.TXT put as text, a new version each time" 'grep -aqx "Written on VMS, put to another server." "$srv"'
ok "SUBDIR made on the server" 'grep -aq "SUBDIR.DIR;1" "$srv"'
ok "the password is in no output" '! grep -aqF -- "$pass" "$out" "$srv" "$top/out/plat-$node.txt"'
sed -n '/^==== \[.LIVE.REMOTE\]/,$p' "$srv"
sed -n '/^---- startup/,/^---- end/p' "$out" > "${out%.txt}.pty"
args=()
for s in "$@"; do args+=(--step "$s"); done
python3 "$top/tools/vtscreen.py" "${args[@]}" "${out%.txt}.pty"
