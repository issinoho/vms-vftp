$! VFTP$SETUP.COM - define the VFTP command for a user
$!
$! Add to LOGIN.COM (or SYS$MANAGER:SYLOGIN.COM for everyone):
$!     $ @VFTP$ROOT:[000000]VFTP$SETUP.COM
$!
$ if f$trnlnm("VFTP$ROOT") .eqs. ""
$ then
$   write sys$error "VFTP$SETUP: VFTP$ROOT is not defined; run VFTP$STARTUP.COM first"
$   exit 44
$ endif
$ vftp :== $VFTP$ROOT:[000000]VFTP.EXE
$ exit 1
