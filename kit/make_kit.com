$! MAKE_KIT.COM - build the VFTP PCSI kit for this node's architecture
$!
$! Usage:  @[.KIT]MAKE_KIT          (from the work directory, after @BUILD_VMS)
$! Needs [.BUILD]VFTP.EXE. The product description, text module, KIT.ENV and
$! README.VMS were put in [.KIT] by tools/kit.sh. Writes the kit to [.KIT_<arch>].
$! (After vms-steward's MAKE_KIT.COM.)
$!
$ set noon
$ set process/parse_style=extended
$ status = 44
$ saved_default = f$environment("DEFAULT")
$ proc = f$environment("PROCEDURE")
$ set default 'f$parse(proc,,,"DEVICE")''f$parse(proc,,,"DIRECTORY")'
$ set default [-]
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ base = "I64VMS"
$ if arch .eqs. "X86_64" then base = "X86VMS"
$!
$! KIT_PRODUCER, KIT_PRODUCT, PCSI_VERSION, KIT_VERSION from KIT.ENV
$ open/read env [.KIT]KIT.ENV
$env_loop:
$ read/end=env_done env line
$ name = f$element(0, "=", line)
$ 'name' = f$element(1, "=", line)
$ goto env_loop
$env_done:
$ close env
$!
$ if f$search("[.BUILD]VFTP.EXE") .eqs. ""
$ then
$   write sys$error "MAKE_KIT: no [.BUILD]VFTP.EXE; @BUILD_VMS first"
$   goto done
$ endif
$! The image must report the version the kit claims.
$ vftp_v = "$" + f$search("[.BUILD]VFTP.EXE")
$ define/user sys$output [.KIT]VERSION.TMP
$ vftp_v "-v"
$ open/read v [.KIT]VERSION.TMP
$ read v got
$ close v
$ delete/nolog [.KIT]VERSION.TMP;*
$ if got .nes. "vftp " + KIT_VERSION
$ then
$   write sys$error "MAKE_KIT: VFTP.EXE says """, got, """, the kit is ", KIT_VERSION, "; rebuild"
$   goto done
$ endif
$!
$! Gather the files flat in [.KIT_<arch>.MAT]: PRODUCT PACKAGE looks each one up by
$! name in the material directory (destinations are in the PDF).
$ mat = "[.KIT_''arch'.MAT]"
$ out = "[.KIT_''arch']"
$ if f$search("KIT_''arch'.DIR") .eqs. "" then create/directory 'out'
$ if f$search("[.KIT_''arch']MAT.DIR") .eqs. "" then create/directory 'mat'
$ if f$search("''out'*.PCSI;*") .nes. "" then delete/nolog 'out'*.PCSI;*
$ if f$search("''mat'*.*;*") .nes. "" then delete/nolog 'mat'*.*;*
$ copy/nolog [.BUILD]VFTP.EXE 'mat'
$ copy/nolog [.KIT]VFTP$STARTUP.COM 'mat'
$ copy/nolog [.KIT]VFTP$SETUP.COM 'mat'
$ copy/nolog [.KIT]README.VMS 'mat'
$ copy/nolog []LICENSE. 'mat'LICENSE.
$! libssh2 is linked into VFTP.EXE: its licence goes with it.
$ copy/nolog [.KIT]LIBSSH2-COPYING. 'mat'LIBSSH2-COPYING.
$ matspec = f$parse(mat,,,"DEVICE","NO_CONCEAL") + f$parse(mat,,,"DIRECTORY","NO_CONCEAL")
$!
$ write sys$output "MAKE_KIT: ''KIT_PRODUCER' ''base' ''KIT_PRODUCT' ''PCSI_VERSION' (VFTP ''KIT_VERSION')"
$ product package 'KIT_PRODUCT' -
    /producer='KIT_PRODUCER' /base_system='base' /version='PCSI_VERSION' -
    /source=[.KIT]PRODUCT-'base'.PCSI$DESC -
    /material='matspec' -
    /destination='out' -
    /format=sequential -
    /options=noconfirm /log
$ status = $status
$ kit = f$search("''out'*.PCSI")
$ if kit .nes. "" then write sys$output "MAKE_KIT: kit ", kit
$ if kit .eqs. "" then status = 44
$done:
$ set default 'saved_default'
$ exit status
