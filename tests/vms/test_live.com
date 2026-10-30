$! test_live.com - VFTP end to end on a pseudo-terminal (PTYDRIVE plays the user),
$! in [.LIVE] (test_plat.com's files), with VFTP$LOG there too. No password is
$! typed; nothing outside [.LIVE] is changed. Print with tools/vtscreen.py.
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
$ define/job vftp$log 'live'vftp.log
$ build = here - "]" + ".BUILD]"
$ vftp :== $'build'vftp.exe
$ drive := $'here'ptydrive.exe
$ set default 'live'
$ drive 'keys'
$ set default 'here'
$ deassign/job vftp$log
$ write sys$output "==== VFTP log"
$ if f$search(live + "vftp.log") .nes. "" then type 'live'vftp.log
$ write sys$output "==== [.LIVE]"
$ directory/size 'live'
