#!/usr/bin/env bash
# push.sh <node> - upload the sources, tests and build procedure to the node's work
# directory, keeping the directory layout ([.SRC.CORE] etc.).
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: push.sh <node>}
v=$top/tools/vms.sh
cd "$top"
$v "$node" put build_vms.com LICENSE
for d in kit probes src src/core src/app src/plat src/plat/vms src/plat/stub tests tests/unit tests/snap tests/vms; do
    files=$(find "$d" -maxdepth 1 -type f | sort)
    [ -n "$files" ] || continue
    # shellcheck disable=SC2086
    $v "$node" put $files -- "$d"
done
echo "push: $node done"
