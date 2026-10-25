$! test_plat.com - build and run test_plat.c (VFTP's RMS file code) in a fresh
$! [.LIVE], then show what RMS recorded about the files it wrote.
$ set noon
$ if f$search("live.dir") .eqs. "" then create/directory [.live]
$ if f$search("[.live.sub]*.*;*") .nes. "" then delete/nolog [.live.sub]*.*;*
$ if f$search("[.live]sub.dir") .nes. "" then set security/protection=o:rwed [.live]sub.dir
$ if f$search("[.live]*.*;*") .nes. "" then delete/nolog [.live]*.*;*
$ cflags = "/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/NAMES=(AS_IS,SHORTENED)" -
         + "/INCLUDE=([.src.core],[.src.app],[.src.net],[.src.plat],[.src.plat.vms])"
$ cc'cflags'/object=[.build]test_plat.obj [.tests.vms]test_plat.c
$ if $severity .ne. 1 then exit 44
$ define/user vftp$log [.live]vftp.log
$ link/executable=[.build]test_plat.exe [.build]test_plat.obj,vf_plat_vms.obj,vf_util.obj,vf_ansi.obj,vf_screen.obj
$ if $severity .ne. 1 then exit 44
$ run [.build]test_plat
$ directory/full [.live]*.bin;*,*.txt;* /select=file=nodir
$ dump/record=count=1 [.live]w.bin
