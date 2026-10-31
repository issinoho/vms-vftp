$! live_server.com SETUP|CHECK - the server side of the cross-node live test
$! (tools/live.sh <client> <server>), run on the server node after test_plat.com.
$! SETUP makes [.LIVE.REMOTE] as test_live.com does for its own node: R.TXT, R.BIN
$! (a copy of [.LIVE]F.BIN) and a 10 MB BIG.BIN; CHECK shows what the client left.
$! The MD5s are printed as "LIVE-MD5 <file> <hex>" for live.sh to compare.
$ set noon
$ here = f$environment("DEFAULT")
$ live = here - "]" + ".LIVE]"
$ remote = live - "]" + ".REMOTE]"
$ if p1 .eqs. "CHECK" then goto check
$ create/directory 'remote'
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
$ rename 'live'big1.bin 'remote'big.bin
$ checksum/algorithm=md5 'remote'r.bin
$ write sys$output "LIVE-MD5 r.bin ''checksum$checksum'"
$ checksum/algorithm=md5 'remote'big.bin;1
$ write sys$output "LIVE-MD5 big.bin ''checksum$checksum'"
$ exit
$ check:
$ write sys$output "==== [.LIVE.REMOTE] on the server"
$ directory/size=all/date 'remote'
$ checksum/algorithm=md5 'remote'big.bin;1
$ write sys$output "LIVE-MD5 big.bin ''checksum$checksum'"
$ write sys$output "==== the files the client put"
$ type 'remote'*.txt;*
