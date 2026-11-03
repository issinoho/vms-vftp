$! probe_alloc.com - probe_alloc.c in a scratch directory [.PROBE_ALLOC], which is
$! removed after.
$ set noon
$ here = f$environment("DEFAULT")
$ cc/STANDARD=C99/POINTER_SIZE=32/INCLUDE=[.probes]/object=[.build]probe_alloc.obj [.probes]probe_alloc.c
$ link/executable=[.build]probe_alloc.exe [.build]probe_alloc.obj
$ p = "$" + f$parse("[.build]probe_alloc.exe")
$ if f$search("PROBE_ALLOC.DIR") .eqs. "" then create/directory [.PROBE_ALLOC]
$ set default [.PROBE_ALLOC]
$ p
$ set default 'here'
$ delete/nolog [.PROBE_ALLOC]*.*;*
$ set protection=o:rwed PROBE_ALLOC.DIR
$ delete/nolog PROBE_ALLOC.DIR;
