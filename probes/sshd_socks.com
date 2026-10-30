$! sshd_socks.com - the sockets (BG devices) VSI sshd's master holds, to see whether
$! client sessions are left behind (NOTES N6). Needs WORLD to see SSH$SSH's process.
$ set noon
$ ctx = ""
$ loop:
$ pid = f$pid(ctx)
$ if pid .eqs. "" then exit
$ name = f$getjpi(pid, "PRCNAM")
$ if f$extract(0, 8, name) .nes. "SSH$SSH_" then goto loop
$ write sys$output "sshd ", pid, " ", name, " since ", f$getjpi(pid, "LOGINTIM")
$ pipe show process/id='pid'/all | search sys$pipe "BG"
$ goto loop
