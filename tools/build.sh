#!/usr/bin/env bash
# build.sh <node> [TEST] - upload and run @build_vms on <node> with that node's
# libssh2 install tree (tools/nodes.conf, column 8). Log: out/build-<node>.log.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: build.sh <node> [TEST]}
what=${2:-}
. "$top/tools/node_lock.sh"
node_lock "$node" 9
read -r _ _ _ _ _ _ _ SSH2 < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
[ -n "${SSH2:-}" ] || { echo "build: no libssh2 install tree for $node in tools/nodes.conf" >&2; exit 2; }
mkdir -p "$top/out"
"$top/tools/push.sh" "$node" > /dev/null 2>&1
job=$top/out/build-$node.com
printf '$ set noon\n$ @build_vms "%s" %s\n' "$what" "$SSH2" > "$job"
"$top/tools/vms.sh" "$node" run "$job" > "$top/out/build-$node.log" 2>&1 || true
grep -aE 'checks|TEST-OK|BUILD-FAILED|build:|%CC-|%LINK-|%ILINK-' "$top/out/build-$node.log" || true
grep -aq 'BUILD-FAILED' "$top/out/build-$node.log" && exit 1
grep -aq 'build: \[.build\]vftp.exe' "$top/out/build-$node.log"
