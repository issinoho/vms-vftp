$! pcsi_remove.com - remove the PCSI-installed VFTP and check that nothing is left.
$! SYSTEM-WIDE: run only with the user's agreement.
$ set noon
$ old_privs = f$setprv("SYSPRV,SYSNAM,CMKRNL,BYPASS")
$ product remove VFTP /producer=ISSINOHO /options=noconfirm /log
$ write sys$output "=== remove status ", $status
$ write sys$output "VFTP$ROOT after removal: [", f$trnlnm("VFTP$ROOT"), "]"
$ write sys$output "VFTP after removal: [", f$trnlnm("VFTP"), "]"
$ write sys$output "files after removal: [", f$search("SYS$COMMON:[VFTP...]*.*"), "]"
$ write sys$output "startup after removal: [", f$search("SYS$STARTUP:VFTP$STARTUP.COM"), "]"
$ dummy = f$setprv(old_privs)
$ exit 1
