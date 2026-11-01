#!/usr/bin/env bash
# live_dirs.sh <node> [--ftp] [steps...] - whole directories (D13) on <node>: VFTP
# puts [.LIVE.DIRS] (three levels, an empty directory, two versions of A.TXT) to a
# server on the same node twice, then gets it back into [.LIVE.BACK]
# (tests/vms/test_live.com DIRS). The server is the node's own sshd, its side
# [.LIVE.REMOTE], logged in with tools/live_key; with --ftp the node's own FTP
# server as VFTPTEST (D11, the password in tools/live_ftp_pass, as live_ftp.sh).
# Then it saves the connection as the site livesite (D14) and starts VFTP again
# as "vftp livesite". Then the screens after the named steps (all if none).
# Output in out/live-dirs[-ftp]-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: live_dirs.sh <node> [--ftp] [step...]}; shift
ftp=
if [ "${1:-}" = --ftp ]; then ftp=1; shift; fi
conf() { awk -v n="$1" '$1==n' "$top/tools/nodes.conf"; }
. "$top/tools/node_lock.sh"
node_lock "$node" 9
[ -f "$top/tools/live_key" ] || { echo "live_dirs.sh: no tools/live_key" >&2; exit 2; }
read -r _ _ _ PORT _ _ SFTPDIR _ < <(conf "$node")
[ -n "${PORT:-}" ] || { echo "live_dirs.sh: unknown node '$node'" >&2; exit 2; }
mkdir -p "$top/out/$node"
keys=$top/out/$node/live_dirs_keys.txt
pass=
cleanup() {
    rm -f "$keys"
    "$top/tools/vms.sh" "$node" dcl 'delete/nolog [.tests.vms]live_dirs_keys.txt;*' > /dev/null 2>&1 || true
}
trap cleanup EXIT
if [ -n "$ftp" ]; then
    pass_file=$top/tools/live_ftp_pass
    [ -f "$pass_file" ] || { echo "live_dirs.sh: no tools/live_ftp_pass" >&2; exit 2; }
    pass=$(head -n 1 "$pass_file")
    [ -n "$pass" ] || { echo "live_dirs.sh: tools/live_ftp_pass is empty" >&2; exit 2; }
    rdir=${VFTP_FTP_DIR:-'USER$ROOT:[VFTPTEST]'}
    sdir=$rdir                                    # the same directory, as DCL sees it
    tag=dirs-ftp-$node
    umask 077
    pesc=$(printf '%s' "$pass" | sed -e 's/[\\&|]/\\&/g')
    sed -e "s|@PASS@|$pesc|" -e "s|@RDIR@|$rdir|" "$top/tests/vms/live_dirs_ftp.tpl" > "$keys"
    umask 022
else
    rdir=$SFTPDIR/live/remote
    sdir=                                         # test_live.com: [.LIVE.REMOTE]
    tag=dirs-$node
    sed -e "s/@PORT@/$PORT/" -e "s|@RDIR@|$rdir|" "$top/tests/vms/live_dirs.tpl" > "$keys"
fi
"$top/tools/build.sh" "$node" TEST
"$top/tools/vms.sh" "$node" run tests/vms/test_plat.com > "$top/out/plat-$node.txt" 2>&1
grep -E 'PLAT:|FAIL' "$top/out/plat-$node.txt"
srv=$top/out/server-$tag.txt
if [ -n "$ftp" ]; then
    "$top/tools/vms.sh" "$node" run tests/vms/live_server.com SETUP "$rdir" > "$srv" 2>&1
else
    "$top/tools/vms.sh" "$node" run tests/vms/live_server.com SETUP > "$srv" 2>&1
fi
grep -aq '^LIVE-MD5 big.bin ' "$srv" || { echo "live_dirs.sh: server setup failed" >&2; cat "$srv" >&2; exit 1; }
"$top/tools/vms.sh" "$node" put "$keys" -- tests/vms > /dev/null
rm -f "$keys"
"$top/tools/vms.sh" "$node" put "$top/tools/live_key" -- tests/vms > /dev/null   # test_live.com moves it
out=$top/out/live-$tag.txt
"$top/tools/vms.sh" "$node" run tests/vms/test_live.com '[.tests.vms]live_dirs_keys.txt' DIRS "$sdir" > "$out" 2>&1
cleanup
grep -aE '^LIVE' "$out" || true
# DIFFERENCES' count after each "LIVE-CMP <what>": 0, or the line is missing (a file was)
same() {
    awk -v w="$1" '$0 == "LIVE-CMP " w { on = 1; next }
                   /^LIVE-CMP / { on = 0 }
                   on && /difference records found: 0/ { ok = 1 }
                   END { exit !ok }' "$out"
}
ok() { if eval "$2"; then echo "DIRS: $1"; else echo "DIRS: FAILED: $1"; fi; }
ok "Put: the highest A.TXT on the server, the same records" 'same "server a.txt"'
ok "Put: DIRS/SUB/DEEP/C.TXT three levels down" 'same "server c.txt"'
ok "Put: the empty directory made" 'grep -aq "^LIVE-DIRS server empty yes" "$out"'
ok "Put again: new versions on the server, the directories reused" 'grep -aq "^LIVE-DIRS server versions yes" "$out"'
ok "Get: A.TXT and C.TXT back as the same records" 'same "back a.txt" && same "back c.txt"'
ok "Get: B.BIN back byte for byte" 'same "back b.bin"'
ok "Get: one version of each file from the server's two" 'grep -aq "^LIVE-DIRS back one version yes" "$out"'
ok "Get: the empty directory made" 'grep -aq "^LIVE-DIRS back empty yes" "$out"'
ok "Sites: the connection saved as livesite, no password line" \
   'grep -aq "^LIVE-SITES saved yes" "$out" && ! grep -aiq "^password" "$out"'
if [ -n "$ftp" ]; then login='LOGIN vftptest@localhost:21 by password$'; else login="LOGIN [^ ]*@localhost:$PORT by key\$"; fi
ok "Sites: \"vftp livesite\" logged in again from it" '[ "$(grep -ac "$login" "$out")" = 2 ]'
if [ -n "$ftp" ]; then
    ok "the password is in no output" '! grep -aqF -- "$pass" "$out" "$srv" "$top/out/plat-$node.txt"'
fi
sed -n '/^---- startup/,/^---- end/p' "$out" > "${out%.txt}.pty"
args=()
for s in "$@"; do args+=(--step "$s"); done
python3 "$top/tools/vtscreen.py" "${args[@]}" "${out%.txt}.pty"
