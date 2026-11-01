$! test_live.com - VFTP end to end on a pseudo-terminal (PTYDRIVE plays the user),
$! in [.LIVE] (test_plat.com's files), with VFTP$LOG there too. No password is
$! typed; nothing outside [.LIVE] is changed. Print with tools/vtscreen.py.
$! SYS$LOGIN is [.LIVE] while VFTP runs, so its SSH directory is [.LIVE.SSH]: the
$! test key (tools/live.sh puts it in [.TESTS.VMS]) and a copy of known_hosts, which
$! VFTP adds this node's key to. The server side is [.LIVE.REMOTE].
$! P2 UNIX or VMS: the server is elsewhere, OpenSSH on Linux (tools/live_unix.sh) or
$! the other node (live.sh --server, tests/vms/live_server.com), and that side is
$! checked there: no [.LIVE.REMOTE] here, a U.TXT to put; P3 is the server's name
$! in known_hosts.
$! P2 DIRS: whole directories (D13) to a server on this node, whose directory is P3
$! ([.LIVE.REMOTE] if none; tools/live_dirs.sh, live_server.com SETUP before): VFTP
$! puts [.LIVE.DIRS] there twice and gets it back into [.LIVE.BACK].
$ set noon
$ dirs = p2 .eqs. "DIRS"
$ away = p2 .nes. "" .and. .not. dirs
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
$ if dirs then goto dirs_setup
$ if away then goto away_setup
$ create/directory 'remote'
$ create 'remote'r.txt
A variable-length text file on the server.
Its second line.
$ copy [.live]f.bin 'remote'r.bin
$ goto setup_done
$ away_setup:
$ create [.live]u.txt
Written on VMS, put to another server.
Its second line.
$ setup_done:
$ if dirs then goto big_done
$! BIG.BIN: F.BIN (fixed 512, 5 blocks) doubled 12 times, 10 MB, on both sides, to
$! see progress and cancel (and that a cancelled Put leaves the server's BIG.BIN;1)
$! (COPY A,A A would read the new, empty A as its second input: use two names)
$ copy [.live]f.bin [.live]big1.bin
$ n = 0
$ double:
$ copy [.live]big1.bin,[.live]big1.bin [.live]big2.bin
$ delete/nolog [.live]big1.bin;*
$ rename [.live]big2.bin [.live]big1.bin
$ n = n + 1
$ if n .lt. 12 then goto double
$ rename [.live]big1.bin [.live]big.bin
$ if .not. away then copy [.live]big.bin 'remote'big.bin
$ checksum/algorithm=md5 [.live]big.bin
$ big_md5 = checksum$checksum
$ big_done:
$ define/job sys$login 'live'
$ define/job vftp$log 'live'vftp.log
$ build = here - "]" + ".BUILD]"
$ vftp :== $'build'vftp.exe
$ drive := $'here'ptydrive.exe
$! no VFTP.EXE (a build under way): PTYDRIVE would type the script at the DCL
$! prompt, a test password and all (N12)
$ if f$search(build + "vftp.exe") .eqs. ""
$ then
$   write sys$output "LIVE: no ''build'VFTP.EXE: nothing typed"
$   goto driven
$ endif
$ set default 'live'
$ drive 'keys'
$ driven:
$ set default 'here'
$ deassign/job vftp$log
$ deassign/job sys$login
$ delete/nolog 'ssh'id_ed25519.;*
$ tree = live - "]" + "...]"
$ if f$search(tree + "id_ed25519*.*") .nes. "" then write sys$output "LIVE: a copy of the test key!"
$ if f$search(tree + "id_ed25519*.*") .nes. "" then delete/log 'tree'id_ed25519*.*;*
$ if dirs then goto dirs_check
$ if away then goto away_check
$ write sys$output "==== [.LIVE.SSH] and [.LIVE.REMOTE]"
$ directory/size/date 'ssh','remote'
$ write sys$output "==== downloads against their sources"
$ differences/mode=hex 'live'r.bin 'live'f.bin
$ differences 'live'r.txt 'remote'r.txt
$ write sys$output "==== BIG.BIN after the cancelled Get and Put: one version each, as before"
$ directory/size=all/date 'live'big.bin;*,'remote'big.bin;*
$ checksum/algorithm=md5 'remote'big.bin;1
$ if checksum$checksum .eqs. big_md5 then write sys$output "LIVE: server BIG.BIN;1 unchanged"
$ if checksum$checksum .nes. big_md5 then write sys$output "LIVE: server BIG.BIN;1 CHANGED"
$ write sys$output "==== known_hosts: this node added?"
$ search/statistics 'ssh'known_hosts. "localhost"
$ goto log
$ away_check:
$ write sys$output "==== [.LIVE.SSH]"
$ directory/size/date 'ssh'
$ write sys$output "==== downloads from the server (compared on that side)"
$ directory/size=all/full 'live'r.bin;*,'live'r.txt;*,'live'big.bin;*
$ if f$search(live + "r.bin") .eqs. "" then write sys$output "LIVE-MD5 r.bin missing"
$ if f$search(live + "r.bin") .eqs. "" then goto away_txt
$ checksum/algorithm=md5 'live'r.bin
$ write sys$output "LIVE-MD5 r.bin ''checksum$checksum'"
$ away_txt:
$ type 'live'r.txt
$ write sys$output "==== known_hosts: the server added?"
$ search/statistics 'ssh'known_hosts. "''p3'"
$ goto log
$! ---- P2 DIRS
$ dirs_setup:
$! a tree of three levels, an empty directory, two versions of A.TXT; [.LIVE.BACK]
$! empty. Both made again each run.
$ call wipe 'live' DIRS
$ call wipe 'live' BACK
$ create/directory [.live.dirs.sub.deep]
$ create/directory [.live.dirs.empty]
$ create/directory [.live.back]
$ create [.live.dirs]a.txt
First version of A.
$ create [.live.dirs]a.txt
Second version of A, the highest.
Its second line.
$ copy [.live]f.bin [.live.dirs.sub]b.bin
$ create [.live.dirs.sub.deep]c.txt
Three levels down.
$ goto setup_done
$ dirs_check:
$ srv = p3
$ if srv .eqs. "" then srv = remote
$ sd = srv - "]" + ".DIRS]"
$ sdeep = srv - "]" + ".DIRS.SUB.DEEP]"
$ bk = live - "]" + ".BACK.DIRS]"
$ bsub = live - "]" + ".BACK.DIRS.SUB]"
$ bdeep = live - "]" + ".BACK.DIRS.SUB.DEEP]"
$! full specs: DIFFERENCES takes what the second lacks from the first, and the
$! server's directory may be on another disk ([VFTPTEST] on x86)
$ ld = live - "]" + ".DIRS]"
$ lsub = live - "]" + ".DIRS.SUB]"
$ ldeep = live - "]" + ".DIRS.SUB.DEEP]"
$ write sys$output "==== the tree on the server (put twice), and got back into [.LIVE.BACK]"
$ stree = srv - "]" + ".DIRS...]"
$ btree = live - "]" + ".BACK...]"
$ directory/size/date 'stree'
$ directory/size/date 'btree'
$! each "LIVE-CMP <what>" is followed by DIFFERENCES' count (tools/live_dirs.sh)
$ write sys$output "LIVE-CMP server a.txt"
$ differences 'sd'a.txt 'ld'a.txt;2
$ write sys$output "LIVE-CMP server c.txt"
$ differences 'sdeep'c.txt 'ldeep'c.txt
$ write sys$output "LIVE-CMP back a.txt"
$ differences 'bk'a.txt 'ld'a.txt;2
$ write sys$output "LIVE-CMP back b.bin"
$ differences/mode=hex 'bsub'b.bin 'lsub'b.bin
$ write sys$output "LIVE-CMP back c.txt"
$ differences 'bdeep'c.txt 'ldeep'c.txt
$ yes = "no"
$ if f$search(sd + "empty.dir") .nes. "" then yes = "yes"
$ write sys$output "LIVE-DIRS server empty ''yes'"
$ yes = "no"
$ if f$search(sd + "a.txt;2") .nes. "" then yes = "yes"
$ write sys$output "LIVE-DIRS server versions ''yes'"
$ yes = "no"
$ if f$search(bk + "a.txt;1") .nes. "" .and. f$search(bk + "a.txt;2") .eqs. "" then yes = "yes"
$ write sys$output "LIVE-DIRS back one version ''yes'"
$ yes = "no"
$ if f$search(bk + "empty.dir") .nes. "" then yes = "yes"
$ write sys$output "LIVE-DIRS back empty ''yes'"
$ log:
$ write sys$output "==== VFTP log"
$ if f$search(live + "vftp.log") .nes. "" then type 'live'vftp.log
$ write sys$output "==== [.LIVE]"
$ directory/size 'live'
$ exit
$!
$! wipe <dir> <name>: delete <dir>'s subdirectory <name> and all in it (only ever
$! [.LIVE.DIRS] and [.LIVE.BACK], made by this procedure)
$ wipe: subroutine
$ tree = p1 - "]" + "." + p2 + "...]"
$ if f$search(p1 + p2 + ".dir") .eqs. "" then exit
$ i = 0
$ wipe_again:
$ define/user sys$output nl:
$ define/user sys$error nl:
$ set security/protection=o:rwed 'tree'*.dir;*
$ define/user sys$output nl:
$ define/user sys$error nl:
$ delete 'tree'*.*;*
$ i = i + 1
$ if i .lt. 8 then goto wipe_again
$ set security/protection=o:rwed 'p1''p2'.dir;*
$ delete/nolog 'p1''p2'.dir;*
$ exit
$ endsubroutine
