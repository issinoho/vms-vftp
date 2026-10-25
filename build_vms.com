$! build_vms.com - build VFTP on OpenVMS (x86-64 or IA64), always from clean.
$!   @build_vms          build the core (Phase 0: no application yet)
$!   @build_vms TEST     also build and run the unit tests (TEST-OK)
$! Any compiler warning stops the build (as -Werror does on Linux).
$ set noon
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ if f$search("[.build]*.*") .nes. "" then delete/nolog [.build]*.*;*
$ if f$search("build.dir") .eqs. "" then create/directory [.build]
$! Under /STANDARD=C99 only ANSI names get the DECC$ prefix: ALL_ENTRIES maps the rest.
$ cflags = "/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES" -
         + "/INCLUDE=([.src.core],[.src.app],[.src.plat],[.src.plat.vms],[.src.plat.stub])"
$ core = "vf_util,vf_edit,vf_screen,vf_ansi"
$ i = 0
$ core_loop:
$   m = f$element(i, ",", core)
$   if m .eqs. "," then goto core_done
$   cc'cflags'/object=[.build]'m'.obj [.src.core]'m'.c
$   if $severity .ne. 1 then goto failed
$   i = i + 1
$   goto core_loop
$ core_done:
$ write sys$output "build: core"
$ if p1 .nes. "TEST" then exit 1
$!
$ cc'cflags'/object=[.build]test_core.obj [.tests.unit]test_core.c
$ if $severity .ne. 1 then goto failed
$ set default [.build]
$ link/executable=test_core.exe test_core.obj,'core'
$ if $severity .ne. 1 then goto failed
$ set default [-]
$ run [.build]test_core
$ if .not. $status then goto failed
$ write sys$output "TEST-OK"
$ exit 1
$ failed:
$ write sys$output "BUILD-FAILED"
$ exit 44
