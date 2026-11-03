$! VFTP$STARTUP.COM - system startup for VFTP (full-screen FTP, SFTP and SCP client)
$!
$! Installed into SYS$STARTUP. Defines the system logical names VFTP$ROOT (the
$! installed [VFTP] directory) and VFTP (its top level, where VFTP.EXE is). To
$! define them at every boot, add to SYS$MANAGER:SYSTARTUP_VMS.COM:
$!
$!     $ @SYS$STARTUP:VFTP$STARTUP.COM
$!
$! Users get the VFTP command from VFTP$SETUP.COM, for example in SYLOGIN.COM:
$!     $ @VFTP$ROOT:[000000]VFTP$SETUP.COM
$!
$! P1 = "INSTALL": also print the post-installation tasks (PCSI runs it so).
$! P1 = "REMOVE":  deassign the logical names instead (PCSI runs it so at removal).
$!
$ set noon
$ mode = f$edit(p1, "UPCASE")
$ if mode .eqs. "REMOVE"
$ then
$   if f$trnlnm("VFTP", "LNM$SYSTEM_TABLE") .nes. "" then -
        deassign/system/executive_mode VFTP
$   if f$trnlnm("VFTP$ROOT", "LNM$SYSTEM_TABLE") .nes. "" then -
        deassign/system/executive_mode VFTP$ROOT
$   exit 1
$ endif
$!
$! This procedure sits in <destination>[SYS$STARTUP]; the product is in
$! <destination>[VFTP]. Rooted logicals need the physical form.
$ proc = f$environment("PROCEDURE")
$ dev = f$parse(proc,,,"DEVICE","NO_CONCEAL")
$ dir = f$edit(f$parse(proc,,,"DIRECTORY","NO_CONCEAL"), "UPCASE") - "]["
$ root = dir - "SYS$STARTUP]" + "VFTP.]"
$ if root .eqs. dir + "VFTP.]"
$ then
$   write sys$error "VFTP$STARTUP: expected to be in a [SYS$STARTUP] directory, not ''dir'"
$   exit 44
$ endif
$ root = root - ".000000"
$ define/system/executive_mode/translation_attributes=concealed VFTP$ROOT 'dev''root'
$ define/system/executive_mode VFTP VFTP$ROOT:[000000]
$ if f$search("VFTP:VFTP.EXE") .eqs. ""
$ then
$   write sys$error "VFTP$STARTUP: VFTP.EXE not found under ''dev'''root'"
$   exit 44
$ endif
$ if mode .nes. "INSTALL" then exit 1
$ say = "write sys$output"
$ say ""
$ say "    Post-installation tasks for VFTP"
$ say ""
$ say "    At system startup: to define VFTP$ROOT and VFTP at every boot, add to"
$ say "    SYS$MANAGER:SYSTARTUP_VMS.COM:"
$ say "    $ @SYS$STARTUP:VFTP$STARTUP.COM"
$ say "    For users (in SYS$MANAGER:SYLOGIN.COM, or a user's LOGIN.COM), to define"
$ say "    the VFTP command:"
$ say "    $ @VFTP$ROOT:[000000]VFTP$SETUP.COM"
$ say ""
$ say "    VFTP needs no privileges and installs no privileged image. It uses"
$ say "    VSI's SSL3 (libcrypto, and libssl for FTPS); libssh2 is linked into it."
$ say "    PRODUCT REMOVE VFTP removes the product and deassigns the logical names."
$ say ""
$ exit 1
