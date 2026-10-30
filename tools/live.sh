#!/usr/bin/env bash
# live.sh <node> [steps...] - build on <node> with the tests, run the native file
# test, then drive VFTP on a pseudo-terminal (tests/vms/test_live.com) and print
# the screens after the named steps (all if none). Output kept in out/live-<node>.txt.
# The login is tools/live_key (git-ignored, made for the test; its public half is in
# the node's authorized_keys as restrict,from="127.0.0.1,::1"). See test_live.com.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: live.sh <node> [step...]}; shift
read -r _ _ _ PORT _ _ SFTPDIR _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
[ -f "$top/tools/live_key" ] || { echo "live.sh: no tools/live_key (the test's login key)" >&2; exit 2; }
mkdir -p "$top/out"
sed -e "s/@PORT@/$PORT/" -e "s|@RDIR@|$SFTPDIR/live/remote|" "$top/tests/vms/live_keys.tpl" > "$top/out/live_keys.txt"
"$top/tools/build.sh" "$node" TEST
"$top/tools/vms.sh" "$node" put "$top/out/live_keys.txt" -- tests/vms > /dev/null
"$top/tools/vms.sh" "$node" run tests/vms/test_plat.com > "$top/out/plat-$node.txt" 2>&1
grep -E 'PLAT:|FAIL' "$top/out/plat-$node.txt"
"$top/tools/vms.sh" "$node" put "$top/tools/live_key" -- tests/vms > /dev/null   # test_live.com moves it
"$top/tools/vms.sh" "$node" run tests/vms/test_live.com > "$top/out/live-$node.txt" 2>&1
sed -n '/^---- startup/,/^---- end/p' "$top/out/live-$node.txt" > "$top/out/live-$node.pty"
args=()
for s in "$@"; do args+=(--step "$s"); done
python3 "$top/tools/vtscreen.py" "${args[@]}" "$top/out/live-$node.pty"
