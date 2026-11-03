$! test_installed.com - the PCSI-installed VFTP under PTYDRIVE, with the command
$! from VFTP$SETUP.COM: it starts (SSL3 and the linked-in libssh2 resolve at
$! image activation), the Open form shows, help shows the kit's version, it exits.
$! VFTP$LOG goes to [.LIVE], not the user's SYS$LOGIN. Connects to nothing.
$ set noon
$ here = f$environment("DEFAULT")
$ if f$search("ptydrive.exe") .eqs. ""
$ then
$   cc/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/INCLUDE=[.probes] -
       /object=ptydrive.obj [.tests.vms]ptydrive.c
$   link ptydrive
$ endif
$ @VFTP$ROOT:[000000]VFTP$SETUP.COM
$ show symbol vftp
$ live = here - "]" + ".LIVE]"
$ if f$search("LIVE.DIR") .eqs. "" then create/directory 'live'
$ define/job vftp$log 'live'vftp_installed.log
$ drive := $'here'ptydrive.exe
$ drive [.tests.vms]installed_keys.txt
$ deassign/job vftp$log
$ if f$search(live + "vftp_installed.log") .nes. "" then delete/nolog 'live'vftp_installed.log;*
$ exit 1
