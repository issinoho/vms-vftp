$! build_vms.com - build VFTP on OpenVMS (x86-64 or IA64), always from clean.
$!   @build_vms [TEST] libssh2
$!     TEST     also build and run the unit and snapshot tests (TEST-OK)
$!     libssh2  the vms-libssh2 install tree, e.g. DEV:[DIR.INSTALL_X86_64]
$!              (its [.INCLUDE] and [.LIB]LIBSSH2.OPT); default VFTP_LIBSSH2
$! Any compiler warning stops the build (as -Werror does on Linux).
$ set noon
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ if f$search("[.build]*.*") .nes. "" then delete/nolog [.build]*.*;*
$ if f$search("build.dir") .eqs. "" then create/directory [.build]
$! Under /STANDARD=C99 only ANSI names get the DECC$ prefix: ALL_ENTRIES maps the rest.
$ ssh2 = p2
$ if ssh2 .eqs. "" then ssh2 = f$trnlnm("VFTP_LIBSSH2")
$ if ssh2 .eqs. "" then goto no_ssh2
$ ssh2inc = ssh2 - "]" + ".INCLUDE]"
$ ssh2opt = ssh2 - "]" + ".LIB]LIBSSH2.OPT"
$ if f$search(ssh2opt) .eqs. "" then goto no_ssh2
$! FTPS (vf_ftp.c) uses SSL3's libssl too; <openssl/ssl.h> through OPENSSL, as
$! vms-libssh2 does. libcrypto comes with libssh2's options file.
$ if f$trnlnm("SSL3$INCLUDE") .eqs. "" then goto no_ssl
$ define/process/nolog OPENSSL SSL3$INCLUDE:
$! libssh2's API is mixed case, some names longer than 31: every module is
$! compiled /NAMES=(AS_IS,SHORTENED) so that they all agree.
$ cflags = "/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES" -
         + "/NAMES=(AS_IS,SHORTENED)" -
         + "/INCLUDE=([.src.core],[.src.app],[.src.net],[.src.plat],[.src.plat.vms],[.src.plat.stub],''ssh2inc')"
$ core = "vf_util,vf_edit,vf_screen,vf_ansi,vf_pane,vf_path,vf_text,vf_ftpcore,vf_sites"
$ app = "vf_app,vf_draw"
$ i = 0
$ core_loop:
$   m = f$element(i, ",", core)
$   if m .eqs. "," then goto core_done
$   cc'cflags'/object=[.build]'m'.obj [.src.core]'m'.c
$   if $severity .ne. 1 then goto failed
$   i = i + 1
$   goto core_loop
$ core_done:
$ i = 0
$ app_loop:
$   m = f$element(i, ",", app)
$   if m .eqs. "," then goto app_done
$   cc'cflags'/object=[.build]'m'.obj [.src.app]'m'.c
$   if $severity .ne. 1 then goto failed
$   i = i + 1
$   goto app_loop
$ app_done:
$! libssh2 is built with these (vms-libssh2): scp's struct stat must be the same
$ cc'cflags'/DEFINE=(_USE_STD_STAT=1,_LARGEFILE)/object=[.build]vf_sftp.obj [.src.net]vf_sftp.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]vf_ftp.obj [.src.net]vf_ftp.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]vf_remote.obj [.src.net]vf_remote.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]vf_plat_vms.obj [.src.plat.vms]vf_plat_vms.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]vf_main.obj [.src.plat.vms]vf_main.c
$ if $severity .ne. 1 then goto failed
$ set default [.build]
$ open/write o ssl.opt
$ write o "SYS$SHARE:SSL3$LIBSSL_SHR32/SHAREABLE"
$ close o
$ link/executable=vftp.exe vf_main.obj,vf_plat_vms.obj,vf_remote.obj,vf_sftp.obj,vf_ftp.obj,'core','app','ssh2opt'/options,SYS$DISK:[]ssl.opt/options
$ s = $severity
$ set default [-]
$! an undefined symbol is only a warning on VMS (VTREE CLAUDE.md)
$ if s .ne. 1 then goto failed
$ write sys$output "build: [.build]vftp.exe"
$ if p1 .nes. "TEST" then exit 1
$!
$ cc'cflags'/object=[.build]test_core.obj [.tests.unit]test_core.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]test_ftp.obj [.tests.unit]test_ftp.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]test_app.obj [.tests.unit]test_app.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]vf_plat_stub.obj [.src.plat.stub]vf_plat_stub.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]vf_remote_stub.obj [.src.plat.stub]vf_remote_stub.c
$ if $severity .ne. 1 then goto failed
$ set default [.build]
$ link/executable=test_core.exe test_core.obj,'core'
$ if $severity .ne. 1 then goto failed_up
$ link/executable=test_ftp.exe test_ftp.obj,'core'
$ if $severity .ne. 1 then goto failed_up
$ link/executable=test_app.exe test_app.obj,vf_plat_stub.obj,vf_remote_stub.obj,'core','app'
$ if $severity .ne. 1 then goto failed_up
$ set default [-]
$ run [.build]test_core
$ if .not. $status then goto failed
$ run [.build]test_ftp
$ if .not. $status then goto failed
$ run [.build]test_app
$ if .not. $status then goto failed
$ write sys$output "TEST-OK"
$ exit 1
$ no_ssl:
$ write sys$output "build: VSI's SSL3 kit (SSL3$INCLUDE) is not installed"
$ goto failed
$ no_ssh2:
$ write sys$output "build: no libssh2 install tree (P2 or VFTP_LIBSSH2): build vms-libssh2 first"
$ goto failed
$ failed_up:
$ set default [-]
$ failed:
$ write sys$output "BUILD-FAILED"
$ exit 44
