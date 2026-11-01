# node_lock.sh - sourced by build.sh and the live tests: one run per node at a time.
# Two runs on one node share its [.BUILD] and [.LIVE]: a build clears [.BUILD] under
# a running test, VFTP does not start, and PTYDRIVE types the key script (the test
# password too) at the DCL prompt (NOTES N12). node_lock <node> <fd> takes
# out/.lock-<node> or stops; a script that already holds it (VFTP_LOCKED) passes.
node_lock() {
    local node=$1 fd=$2
    case " ${VFTP_LOCKED:-} " in *" $node "*) return 0 ;; esac
    mkdir -p "$top/out"
    eval "exec $fd>\"\$top/out/.lock-\$node\""
    if ! flock -n "$fd"; then
        echo "$(basename "$0"): another run is using $node (build or live test); wait for it to finish" >&2
        exit 1
    fi
    export VFTP_LOCKED="${VFTP_LOCKED:-} $node"
}
