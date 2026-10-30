#!/usr/bin/env bash
# live.sh <node> [steps...] - build on <node> with the tests, run the native file
# test, then drive VFTP on a pseudo-terminal (tests/vms/test_live.com) and print
# the screens after the named steps (all if none). Output kept in out/live-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: live.sh <node> [step...]}; shift
read -r _ _ _ PORT _ _ _ _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
mkdir -p "$top/out"
sed "s/@PORT@/$PORT/" "$top/tests/vms/live_keys.tpl" > "$top/out/live_keys.txt"
"$top/tools/build.sh" "$node" TEST
"$top/tools/vms.sh" "$node" put "$top/out/live_keys.txt" -- tests/vms > /dev/null
"$top/tools/vms.sh" "$node" run tests/vms/test_plat.com > "$top/out/plat-$node.txt" 2>&1
grep -E 'PLAT:|FAIL' "$top/out/plat-$node.txt"
"$top/tools/vms.sh" "$node" run tests/vms/test_live.com > "$top/out/live-$node.txt" 2>&1
sed -n '/^---- startup/,/^---- end/p' "$top/out/live-$node.txt" > "$top/out/live-$node.pty"
args=()
for s in "$@"; do args+=(--step "$s"); done
python3 "$top/tools/vtscreen.py" "${args[@]}" "$top/out/live-$node.pty"
