$! live_server.com SETUP|CHECK [dir] - the server side of a live test, run on the
$! server node after test_plat.com: the cross-node test (tools/live.sh --server,
$! dir [.LIVE.REMOTE]) and the FTP test (tools/live_ftp.sh, dir USER$ROOT:[VFTPTEST],
$! the test account's login directory, D11). SETUP empties dir and makes R.TXT,
$! R.BIN (a copy of [.LIVE]F.BIN) and a 10 MB BIG.BIN; CHECK shows what the client
$! left. The MD5s are printed as "LIVE-MD5 <file> <hex>" for the harness.
$ set noon
$ here = f$environment("DEFAULT")
$ live = here - "]" + ".LIVE]"
$ remote = live - "]" + ".REMOTE]"
$ if p2 .nes. "" then remote = p2
$! SETUP empties remote: only ever the test's own directories
$ dirname = f$parse(remote,,,"DIRECTORY")
$ if f$locate(".LIVE.REMOTE]", f$edit(dirname,"UPCASE")) .eq. f$length(dirname) .and. -
     f$edit(dirname,"UPCASE") .nes. "[VFTPTEST]"
$ then
$   write sys$output "LIVE: not a test directory: ''remote'"
$   exit 44
$ endif
$ if p1 .eqs. "CHECK" then goto check
$ create/directory 'remote'
$! empty it: a run before may have left files and directories (SUBDIR)
$ tree = remote - "]" + "...]"
$ i = 0
$ empty:
$ if f$search(tree + "*.dir;*") .nes. "" then set security/protection=o:rwed 'tree'*.dir;*
$ if f$search(remote + "*.*;*") .nes. "" then delete/nolog 'tree'*.*;*
$ i = i + 1
$ if i .lt. 3 .and. f$search(remote + "*.*;*") .nes. "" then goto empty
$ create 'remote'r.txt
A variable-length text file on the server.
Its second line.
$ copy 'live'f.bin 'remote'r.bin
$ copy 'live'f.bin 'live'big1.bin
$ n = 0
$ double:
$ copy 'live'big1.bin,'live'big1.bin 'live'big2.bin
$ delete/nolog 'live'big1.bin;*
$ rename 'live'big2.bin 'live'big1.bin
$ n = n + 1
$ if n .lt. 12 then goto double
$! COPY, not RENAME: remote may be on another disk (x86: [VFTPTEST] is)
$ copy 'live'big1.bin 'remote'big.bin
$ delete/nolog 'live'big1.bin;*
$! every file there, or no MD5 at all (CHECKSUM would leave the last one's)
$ if f$search(remote + "r.txt") .eqs. "" .or. f$search(remote + "r.bin") .eqs. "" .or. -
     f$search(remote + "big.bin") .eqs. ""
$ then
$   write sys$output "LIVE: SETUP failed: a file is missing in ''remote'"
$   exit 44
$ endif
$ checksum/algorithm=md5 'remote'r.bin
$ write sys$output "LIVE-MD5 r.bin ''checksum$checksum'"
$ checksum/algorithm=md5 'remote'big.bin;1
$ write sys$output "LIVE-MD5 big.bin ''checksum$checksum'"
$ exit
$ check:
$ write sys$output "==== [.LIVE.REMOTE] on the server"
$ directory/size=all/date 'remote'
$ if f$search(remote + "big.bin;1") .eqs. "" then write sys$output "LIVE-MD5 big.bin missing"
$ if f$search(remote + "big.bin;1") .eqs. "" then goto put
$ checksum/algorithm=md5 'remote'big.bin;1
$ write sys$output "LIVE-MD5 big.bin ''checksum$checksum'"
$ put:
$ write sys$output "==== the files the client put"
$ type 'remote'*.txt;*
