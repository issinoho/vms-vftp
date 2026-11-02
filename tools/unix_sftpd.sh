#!/usr/bin/env bash
# unix_sftpd.sh start [scp]|stop - a private OpenSSH SFTP server on this Linux
# machine, for the Unix-server live test (tools/live_unix.sh). It runs as you, not
# root, in the foreground of its own process group, and serves SFTP only
# (internal-sftp), only to the test key tools/live_key.pub, only from the nodes in
# tools/nodes.conf, and only until "stop". No password login. With "scp" (D15) it
# runs no SFTP: every command goes through tools/unix_scp_gate.py, which runs only
# VFTP's SCP commands on names inside remote/, and logs them to scp-gate.log. Everything is in out/unix (git-ignored): the
# server's host key, its authorized_keys, its log, and remote/, the scratch directory
# the test works in.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
d=$top/out/unix
port=${VFTP_UNIX_PORT:-2299}
op=${1:?usage: unix_sftpd.sh start [scp]|stop}
mode=${2:-sftp}

stop() {
    if [ -f "$d/sshd.pid" ]; then kill "$(cat "$d/sshd.pid")" 2>/dev/null || true; rm -f "$d/sshd.pid"; fi
}

case $op in
stop) stop; exit 0 ;;
start) ;;
*) echo "usage: unix_sftpd.sh start [scp]|stop" >&2; exit 2 ;;
esac

[ -f "$top/tools/live_key.pub" ] || { echo "unix_sftpd.sh: no tools/live_key.pub" >&2; exit 2; }
# one test at a time: a second start would reset the files under the first
if [ -f "$d/sshd.pid" ] && kill -0 "$(cat "$d/sshd.pid")" 2>/dev/null; then
    echo "unix_sftpd.sh: already running (another live_unix.sh?); run one node at a time" >&2; exit 1
fi
stop
mkdir -p "$d/remote"
chmod 700 "$d"
[ -f "$d/host_key" ] || ssh-keygen -q -t ed25519 -N '' -C vftp-unix-test-host -f "$d/host_key"
nodes=$(awk '!/^#/ && NF {print $3}' "$top/tools/nodes.conf" | paste -sd, -)
# the address the nodes reach this machine on
first=$(awk '!/^#/ && NF {print $3; exit}' "$top/tools/nodes.conf")
listen=$(ip -4 route get "$first" | sed -n 's/.* src \([0-9.]*\).*/\1/p')
echo "restrict,from=\"$nodes\" $(cat "$top/tools/live_key.pub")" > "$d/authorized_keys"
chmod 600 "$d/authorized_keys"
if [ "$mode" = scp ]; then
    force="/usr/bin/python3 -I $top/tools/unix_scp_gate.py $d/remote"
    rm -f "$d/scp-gate.log"
else
    force="internal-sftp -l INFO -d $d/remote"
fi
cat > "$d/sshd_config" <<EOF
Port $port
ListenAddress $listen
HostKey $d/host_key
PidFile $d/sshd.pid
AuthorizedKeysFile $d/authorized_keys
AllowUsers $(id -un)
PubkeyAuthentication yes
PasswordAuthentication no
KbdInteractiveAuthentication no
UsePAM no
StrictModes no
AllowTcpForwarding no
X11Forwarding no
PermitTTY no
ForceCommand $force
Subsystem sftp internal-sftp
LogLevel VERBOSE
EOF
/usr/sbin/sshd -t -f "$d/sshd_config"
/usr/sbin/sshd -f "$d/sshd_config" -E "$d/sshd.log"
sleep 1
[ -f "$d/sshd.pid" ] || { echo "unix_sftpd.sh: sshd did not start" >&2; cat "$d/sshd.log" >&2; exit 1; }
echo "$listen $port $d/remote"
