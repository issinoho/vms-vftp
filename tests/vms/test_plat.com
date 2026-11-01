$! test_plat.com - build and run test_plat.c (VFTP's RMS file code) in a fresh
$! [.LIVE], then show what RMS recorded about the files it wrote.
$ set noon
$ if f$search("live.dir") .eqs. "" then create/directory [.live]
$! empty [.LIVE]: directories first (they deny their owner delete), deepest first
$ clean:
$ d = f$search("[.live...]*.dir;1")
$ if d .eqs. "" then goto clean_files
$ set security/protection=o:rwed 'd'
$ goto clean
$ clean_files:
$! one pass per level, as a directory goes once it is empty (the DIRS test's trees
$! are four deep). A fixed count: F$SEARCH asked the same again goes on from its
$! last answer instead of looking afresh, and stops too soon.
$ i = 0
$ clean_pass:
$ define/user sys$output nl:
$ define/user sys$error nl:
$ delete [.live...]*.*;*
$ i = i + 1
$ if i .lt. 8 then goto clean_pass
$ cleaned:
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
