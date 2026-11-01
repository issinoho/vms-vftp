#!/usr/bin/env bash
# live_unix_ftp.sh <node> [--tls] [steps...] - VFTP on <node> over FTP (FTPS with
# --tls: a test-only self-signed certificate in out/unixftp, TLS required on control
# and data, D12) to pyftpdlib on this
# Linux machine (tools/unix_ftpd.py in out/ftpd-venv, started here and stopped at
# the end): one login, vftptest with tools/live_ftp_pass (D11), connections only
# from the nodes in nodes.conf, files only in out/unixftp/root. The server's
# directory /remote starts with r.txt, r.bin and a random 10 MB big.bin. Then the
# server-side checks, and the screens after the named steps (all if none).
# Run it yourself: it starts a listening server. Output in out/live-unixftp-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: live_unix_ftp.sh <node> [--tls] [step...]}; shift
tls=
if [ "${1:-}" = --tls ]; then tls=1; shift; fi
d=$top/out/unixftp
. "$top/tools/node_lock.sh"
node_lock "$node" 9
port=${VFTP_UNIX_FTP_PORT:-2121}
py=$top/out/ftpd-venv/bin/python
[ -x "$py" ] || { echo "live_unix_ftp.sh: no out/ftpd-venv (python3 -m venv out/ftpd-venv; out/ftpd-venv/bin/pip install pyftpdlib==2.0.1)" >&2; exit 2; }
[ -f "$top/tools/live_ftp_pass" ] || { echo "live_unix_ftp.sh: no tools/live_ftp_pass" >&2; exit 2; }
[ -f "$top/tools/live_key" ] || { echo "live_unix_ftp.sh: no tools/live_key" >&2; exit 2; }
if [ -f "$d/ftpd.pid" ] && kill -0 "$(cat "$d/ftpd.pid")" 2>/dev/null; then
    echo "live_unix_ftp.sh: the test FTP server is running already (another run?)" >&2; exit 1
fi
pass=$(head -n 1 "$top/tools/live_ftp_pass")
nodes=$(awk '!/^#/ && NF {print $3}' "$top/tools/nodes.conf" | paste -sd, -)
first=$(awk '!/^#/ && NF {print $3; exit}' "$top/tools/nodes.conf")
listen=$(ip -4 route get "$first" | sed -n 's/.* src \([0-9.]*\).*/\1/p')
mkdir -p "$d"
chmod 700 "$d"
rm -rf "$d/root" "$d/ftpd.log"
mkdir -p "$d/root/remote"
printf 'A text file on the Unix server.\nIts second line.\n' > "$d/root/remote/r.txt"
head -c 3000 /dev/urandom > "$d/root/remote/r.bin"
head -c 10485760 /dev/urandom > "$d/root/remote/big.bin"
big=$(md5sum < "$d/root/remote/big.bin" | cut -d' ' -f1)
certarg=() fp=
if [ -n "$tls" ]; then
    # made for the test, never committed (out/ is git-ignored)
    openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 30 \
        -subj "/CN=vftp-test-ftps" -keyout "$d/test_cert.pem" -out "$d/test_cert.crt" 2> /dev/null
    cat "$d/test_cert.crt" >> "$d/test_cert.pem"
    chmod 600 "$d/test_cert.pem"
    fp=$(openssl x509 -noout -fingerprint -sha256 -in "$d/test_cert.crt" | sed 's/.*=//')
    certarg=("$d/test_cert.pem")
fi
rbin=$(md5sum < "$d/root/remote/r.bin" | cut -d' ' -f1)

keys=$top/out/$node/live_unix_ftp_keys.txt
stop() {
    if [ -f "$d/ftpd.pid" ]; then kill "$(cat "$d/ftpd.pid")" 2>/dev/null || true; rm -f "$d/ftpd.pid"; fi
    rm -f "$keys"
    "$top/tools/vms.sh" "$node" dcl 'delete/nolog [.tests.vms]live_unix_ftp_keys.txt;*' > /dev/null 2>&1 || true
}
trap stop EXIT
"$py" -I "$top/tools/unix_ftpd.py" "$listen" "$port" "$d/root" "$nodes" "${certarg[@]}" > /dev/null 2>&1 &
echo $! > "$d/ftpd.pid"
sleep 1
kill -0 "$(cat "$d/ftpd.pid")" 2>/dev/null || { echo "live_unix_ftp.sh: the FTP server did not start" >&2; exit 1; }

mkdir -p "$top/out/$node"
umask 077
pesc=$(printf '%s' "$pass" | sed -e 's/[\\&|]/\\&/g')
sed -e "s|@HOST@|$listen|" -e "s|@PORT@|$port|" -e "s|@PASS@|$pesc|" -e "s|@RDIR@|/remote|" \
    "$top/tests/vms/live_unix_ftp${tls:+s}.tpl" > "$keys"
umask 022
"$top/tools/build.sh" "$node" TEST
"$top/tools/vms.sh" "$node" put "$keys" -- tests/vms > /dev/null
rm -f "$keys"
"$top/tools/vms.sh" "$node" run tests/vms/test_plat.com > "$top/out/plat-$node.txt" 2>&1
grep -E 'PLAT:|FAIL' "$top/out/plat-$node.txt"
"$top/tools/vms.sh" "$node" put "$top/tools/live_key" -- tests/vms > /dev/null   # test_live.com moves it
out=$top/out/live-unixftp${tls:+s}-$node.txt
"$top/tools/vms.sh" "$node" run tests/vms/test_live.com '[.tests.vms]live_unix_ftp_keys.txt' UNIX "$listen" > "$out" 2>&1
stop
grep -aE '^LIVE' "$out" || true

r=$d/root/remote
echo "==== the server's /remote"
ls -la "$r"
echo "==== the server's writes (its log)"
grep -E '<- (STOR|RNFR|RNTO|DELE|MKD|RMD|APPE) ' "$d/ftpd.log" | sed 's/^.*<- //'
ok() { if eval "$2"; then echo "UNIXFTP: $1"; else echo "UNIXFTP: FAILED: $1"; fi; }
if [ -n "$tls" ]; then
    ok "FTPS: the certificate asked about, N then Y, and pinned as openssl shows it" \
       'grep -aq "host key not trusted" "$out" && grep -aqF "TLS certificate $fp" "$out"'
    ok "FTPS: no question about the password (it went over TLS)" '! grep -aq "CLEAR PASSWORD" "$out"'
    ok "FTPS: AUTH TLS, PBSZ 0, PROT P on the server" \
       'grep -q "<- AUTH TLS" "$d/ftpd.log" && grep -q "<- PBSZ 0" "$d/ftpd.log" && grep -q "<- PROT P" "$d/ftpd.log"'
    ok "FTPS: the second login asked nothing (known_tls)" \
       '[ "$(grep -ac "LOGIN vftptest@.*by password" "$out")" = 2 ] && [ "$(grep -ac "HOSTKEY ADDED" "$out")" = 1 ]'
else
    ok "logged in after the D10 question" 'grep -aq "CLEAR PASSWORD agreed for vftptest@" "$out" && grep -q "USER .vftptest. logged in" "$d/ftpd.log"'
fi
ok "big.bin unchanged after the cancelled replacing Put" '[ "$(md5sum < "$r/big.bin" | cut -d" " -f1)" = "$big" ]'
ok "big.bin itself never opened to write" '! grep -q "<- STOR big.bin$" "$d/ftpd.log"'
ok "no temporary .vftp-part file left" '! ls "$r" | grep -q vftp-part'
ok "r.bin downloaded byte for byte (TYPE I)" 'grep -aqi "^LIVE-MD5 r.bin $rbin" "$out"'
ok "r.txt downloaded as text lines (TYPE A)" 'grep -aqx "A text file on the Unix server." "$out" && grep -aqx "Its second line." "$out"'
ok "U.TXT put as LF lines, then renamed moved.txt" \
   '[ "$(cat "$r/moved.txt" 2>/dev/null)" = "$(printf "Written on VMS, put to another server.\nIts second line.")" ]'
ok "the replace: u.txt.vftp-part renamed over u.txt" 'grep -q "<- RNFR u.txt.vftp-part" "$d/ftpd.log" && grep -q "<- RNTO u.txt" "$d/ftpd.log"'
ok "SUBDIR made, r.txt deleted" '[ -d "$r/SUBDIR" ] && [ ! -e "$r/r.txt" ]'
ok "the password is in no output" '! grep -aqF -- "$pass" "$out" "$d/ftpd.log" "$top/out/plat-$node.txt"'
sed -n '/^---- startup/,/^---- end/p' "$out" > "${out%.txt}.pty"
args=()
for s in "$@"; do args+=(--step "$s"); done
python3 "$top/tools/vtscreen.py" "${args[@]}" "${out%.txt}.pty"
