#!/usr/bin/env bash
# live.sh <node> [--server <node2>] [steps...] - build on <node> with the tests, run
# the native file test, then drive VFTP on a pseudo-terminal (tests/vms/test_live.com)
# and print the screens after the named steps (all if none).
# The server is <node>'s own sshd; with --server, <node2>'s, whose scratch directory
# [.LIVE.REMOTE] tests/vms/live_server.com sets up and checks (the cross-node test).
# Output kept in out/live-<node>[-<node2>].txt.
# The login is tools/live_key (git-ignored, made for the test; its public half is in
# each node's authorized_keys as restrict,from="..."). See test_live.com.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: live.sh <node> [--server <node2>] [step...]}; shift
server=
if [ "${1:-}" = --server ]; then server=${2:?live.sh: --server needs a node}; shift 2; fi
conf() { awk -v n="$1" '$1==n' "$top/tools/nodes.conf"; }
[ -f "$top/tools/live_key" ] || { echo "live.sh: no tools/live_key (the test's login key)" >&2; exit 2; }
if [ -n "$server" ]; then
    read -r _ _ HOST PORT _ _ SFTPDIR _ < <(conf "$server")
    [ -n "${HOST:-}" ] || { echo "live.sh: unknown node '$server'" >&2; exit 2; }
    if [ "$PORT" = 22 ]; then khname=$HOST; else khname="[$HOST]:$PORT"; fi
    tag=$node-$server
else
    read -r _ _ _ PORT _ _ SFTPDIR _ < <(conf "$node")
    HOST=localhost
    tag=$node
fi
mkdir -p "$top/out/$node"                     # one per node: runs on two nodes can overlap
sed -e "s/@HOST@/$HOST/" -e "s/@PORT@/$PORT/" -e "s|@RDIR@|$SFTPDIR/live/remote|" \
    "$top/tests/vms/live_keys.tpl" > "$top/out/$node/live_keys.txt"
if [ -n "$server" ]; then                     # the server side first: its [.LIVE.REMOTE]
    "$top/tools/build.sh" "$server" TEST
    "$top/tools/vms.sh" "$server" run tests/vms/test_plat.com > "$top/out/plat-$server.txt" 2>&1
    grep -E 'PLAT:|FAIL' "$top/out/plat-$server.txt"
    "$top/tools/vms.sh" "$server" run tests/vms/live_server.com SETUP > "$top/out/server-$tag.txt" 2>&1
    srv_rbin=$(sed -n 's/^LIVE-MD5 r.bin //p' "$top/out/server-$tag.txt")
    srv_big=$(sed -n 's/^LIVE-MD5 big.bin //p' "$top/out/server-$tag.txt")
    [ -n "$srv_rbin" ] && [ -n "$srv_big" ] || { echo "live.sh: server setup failed" >&2; cat "$top/out/server-$tag.txt" >&2; exit 1; }
fi
"$top/tools/build.sh" "$node" TEST
"$top/tools/vms.sh" "$node" put "$top/out/$node/live_keys.txt" -- tests/vms > /dev/null
"$top/tools/vms.sh" "$node" run tests/vms/test_plat.com > "$top/out/plat-$node.txt" 2>&1
grep -E 'PLAT:|FAIL' "$top/out/plat-$node.txt"
"$top/tools/vms.sh" "$node" put "$top/tools/live_key" -- tests/vms > /dev/null   # test_live.com moves it
out=$top/out/live-$tag.txt
if [ -n "$server" ]; then
    "$top/tools/vms.sh" "$node" run tests/vms/test_live.com '[.tests.vms]live_keys.txt' VMS "$khname" > "$out" 2>&1
    "$top/tools/vms.sh" "$server" run tests/vms/live_server.com CHECK > "$top/out/server-$tag.txt" 2>&1
    grep -aE '^LIVE' "$out" || true
    ok() { if eval "$2"; then echo "CROSS: $1"; else echo "CROSS: FAILED: $1"; fi; }
    ok "R.BIN downloaded byte for byte" 'grep -aqi "^LIVE-MD5 r.bin $srv_rbin" "$out"'
    ok "R.TXT downloaded as text lines" 'grep -aqx "A variable-length text file on the server." "$out"'
    ok "server BIG.BIN;1 unchanged after the cancelled Put" 'grep -aqi "^LIVE-MD5 big.bin $srv_big" "$top/out/server-$tag.txt"'
    ok "U.TXT put to the server" 'grep -aqx "Written on VMS, put to another server." "$top/out/server-$tag.txt"'
    sed -n '/^==== \[.LIVE.REMOTE\]/,$p' "$top/out/server-$tag.txt"
else
    "$top/tools/vms.sh" "$node" run tests/vms/test_live.com > "$out" 2>&1
fi
sed -n '/^---- startup/,/^---- end/p' "$out" > "${out%.txt}.pty"
args=()
for s in "$@"; do args+=(--step "$s"); done
python3 "$top/tools/vtscreen.py" "${args[@]}" "${out%.txt}.pty"
