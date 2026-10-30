$! test_live.com - VFTP end to end on a pseudo-terminal (PTYDRIVE plays the user),
$! in [.LIVE] (test_plat.com's files), with VFTP$LOG there too. No password is
$! typed; nothing outside [.LIVE] is changed. Print with tools/vtscreen.py.
$! SYS$LOGIN is [.LIVE] while VFTP runs, so its SSH directory is [.LIVE.SSH]: the
$! test key (tools/live.sh puts it in [.TESTS.VMS]) and a copy of known_hosts, which
$! VFTP adds this node's key to. The server side is [.LIVE.REMOTE].
$ set noon
$ here = f$environment("DEFAULT")
$ keys = p1
$ if keys .eqs. "" then keys = "[.tests.vms]live_keys.txt"
$ keys = f$parse(keys)
$ if f$search("ptydrive.exe") .eqs. ""
$ then
$   cc/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/INCLUDE=[.probes] -
       /object=ptydrive.obj [.tests.vms]ptydrive.c
$   link ptydrive
$ endif
$ live = here - "]" + ".LIVE]"
$ ssh = live - "]" + ".SSH]"
$ remote = live - "]" + ".REMOTE]"
$ create/directory/protection=(s:rwe,o:rwe,g,w) 'ssh'
$ if f$search("[.tests.vms]live_key.") .eqs. "" then write sys$output "LIVE: no test key"
$ rename [.tests.vms]live_key. 'ssh'id_ed25519.
$ set security/protection=(s:rwd,o:rwd,g,w) 'ssh'id_ed25519.
$ real = f$trnlnm("SYS$LOGIN") - "]" + ".SSH]KNOWN_HOSTS."
$ if f$search(real) .nes. "" then copy/protection=(s:rwd,o:rwd,g,w) 'real' 'ssh'known_hosts.
$ create/directory 'remote'
$ create 'remote'r.txt
A variable-length text file on the server.
Its second line.
$ copy [.live]f.bin 'remote'r.bin
$ define/job sys$login 'live'
$ define/job vftp$log 'live'vftp.log
$ build = here - "]" + ".BUILD]"
$ vftp :== $'build'vftp.exe
$ drive := $'here'ptydrive.exe
$ set default 'live'
$ drive 'keys'
$ set default 'here'
$ deassign/job vftp$log
$ deassign/job sys$login
$ delete/nolog 'ssh'id_ed25519.;*
$ tree = live - "]" + "...]"
$ if f$search(tree + "id_ed25519*.*") .nes. "" then write sys$output "LIVE: a copy of the test key!"
$ if f$search(tree + "id_ed25519*.*") .nes. "" then delete/log 'tree'id_ed25519*.*;*
$ write sys$output "==== [.LIVE.SSH] and [.LIVE.REMOTE]"
$ directory/size/date 'ssh','remote'
$ write sys$output "==== downloads against their sources"
$ differences/mode=hex 'live'r.bin 'live'f.bin
$ differences 'live'r.txt 'remote'r.txt
$ write sys$output "==== known_hosts: this node added?"
$ search/statistics 'ssh'known_hosts. "localhost"
$ write sys$output "==== VFTP log"
$ if f$search(live + "vftp.log") .nes. "" then type 'live'vftp.log
$ write sys$output "==== [.LIVE]"
$ directory/size 'live'
