# PTYDRIVE script for test_live.com UNIX: VFTP over FTP to pyftpdlib on Linux
# (tools/unix_ftpd.py; tools/live_unix_ftp.sh fills in @HOST@ @PORT@ @PASS@ and
# @RDIR@, and deletes the filled file from the node after the run). Unix paths, no
# versions: a Put of a name the server has asks first (default No), and goes to a
# temporary name renamed over the old file, so a cancel leaves big.bin as it was.
# No TLS: VFTP asks before the password goes (D10). PTYDRIVE knows \r \e \\ \xHH.
2 dcl | set terminal/width=80/page=24\r
8 start | vftp\r
2 to protocol | \e[A
2 protocol | \x05\x15FTP
2 to host | \e[B
2 host | \x05\x15@HOST@
2 port | \e[B\x05\x15@PORT@
2 user | \e[B\x05\x15vftptest
2 password | \e[B\x05\x15@PASS@
2 to dir | \e[B\e[B\e[B
2 rdir | \x05\x15@RDIR@
6 connect | \e[29~
3 clear question | y
4 logged in | \x17
2 remote down | \e[B
3 remote attributes | a
2 remote attributes back | \x20
0.3 get big | g
4 cancel get | \x03
2 remote down1 | \e[B
6 get bin | g
2 remote down2 | \e[B
6 get txt | g
4 view remote | v
2 view remote back | \x20
2 to local | \x09
2 local big | f\x05\x15BIG.*\r
2 local big last | \eOP\e[B
3 put big | p
0.3 replace big | y
4 cancel put | \x03
2 local u | f\x05\x15U.TXT\r
2 local u last | \eOP\e[B
6 put u | p
4 put u again | p
3 replace no | n
4 put u third | p
6 replace yes | y
2 to remote | \x09
3 remote mkdir | m
5 remote mkdir name | SUBDIR\r
2 remote top | \eOP\e[A
2 remote subdir | \e[B
3 enter subdir | \r
3 leave subdir | \e[D
2 remote last | \eOP\e[B
3 remote rename | r
5 rename name | \x05\x15moved.txt\r
2 remote last again | \eOP\e[B
3 remote delete | d
5 yes remote delete | y
4 close | \eOPo
3 exit | \e[21~
2 logout check | show default\r
