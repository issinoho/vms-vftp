$! probe_rename.com - probe_rename.c in a scratch directory [.PROBE_RENAME], then
$! what DCL sees there (versions, sizes, Q.BIN's end of file and its records' letters),
$! and the scratch directory removed.
$ set noon
$ here = f$environment("DEFAULT")
$ cc/STANDARD=C99/POINTER_SIZE=32/INCLUDE=[.probes]/object=[.build]probe_rename.obj [.probes]probe_rename.c
$ link/executable=[.build]probe_rename.exe [.build]probe_rename.obj
$ p = "$" + f$parse("[.build]probe_rename.exe")
$ if f$search("PROBE_RENAME.DIR") .eqs. "" then create/directory [.PROBE_RENAME]
$ set default [.PROBE_RENAME]
$ p
$ directory/size=all
$ write sys$output "Q.BIN EOF block ", f$file_attributes("Q.BIN","EOF"), " first free byte ", f$file_attributes("Q.BIN","FFB"), " allocated ", f$file_attributes("Q.BIN","ALQ")
$ open/read q Q.BIN
$ r = 0
$ loop:
$ read/end=done q l
$ r = r + 1
$ write sys$output "Q.BIN record ", r, ": ", f$length(l), " bytes of ", f$extract(0,1,l)
$ goto loop
$ done:
$ close q
$ set default 'here'
$ delete/nolog [.PROBE_RENAME]*.*;*
$ set protection=o:rwed PROBE_RENAME.DIR
$ delete/nolog PROBE_RENAME.DIR;
