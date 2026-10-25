$ set noon
$ cc/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/NAMES=(AS_IS,SHORTENED)/object=[.build]probe_rms_read.obj [.probes]probe_rms_read.c
$ link/executable=[.build]probe_rms_read.exe [.build]probe_rms_read.obj
$ p = "$" + f$parse("[.build]probe_rms_read.exe")
$ p [.live]t.txt;2
