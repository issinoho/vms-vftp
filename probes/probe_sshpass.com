$! probe_sshpass.com <port> <libssh2 tree> - probe_sshpass.c as VFTPTEST to localhost:<port>. The
$! password is in [.probes]sshpass.tmp (pushed for the run, from the git-ignored
$! tools/live_ftp_pass); it is deleted at the end, whatever happens.
$ set noon
$ ssh2 = p2
$ define/process/nolog OPENSSL SSL3$INCLUDE:
$ inc = ssh2 - "]" + ".INCLUDE]"
$ opt = ssh2 - "]" + ".LIB]LIBSSH2.OPT"
$ cc/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/NAMES=(AS_IS,SHORTENED)/INCLUDE=('inc')/object=[.build]probe_sshpass.obj [.probes]probe_sshpass.c
$ link/executable=[.build]probe_sshpass.exe [.build]probe_sshpass.obj,'opt'/options
$ p = "$" + f$parse("[.build]probe_sshpass.exe")
$ p localhost 'p1' vftptest [.probes]sshpass.tmp
$ if f$search("[.probes]sshpass.tmp") .nes. "" then delete/nolog [.probes]sshpass.tmp;*
