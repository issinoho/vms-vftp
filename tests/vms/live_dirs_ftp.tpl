# PTYDRIVE script for test_live.com DIRS over FTP: whole directories (D13) to this
# node's own FTP server as VFTPTEST (D11). tools/live_dirs.sh --ftp fills in @PASS@
# (tools/live_ftp_pass; the filled file is deleted from the node after the run) and
# @RDIR@ (the account's login directory). "<secs> <label> | <keys>". No TLS: the
# D10 question is answered Y. In [.LIVE] the order is [-], BACK, DIRS, REMOTE,
# SSH; on the server .., then the directory put.
2 dcl | set terminal/width=80/page=24\r
8 start | vftp\r
2 to protocol | \e[A
2 protocol | \x05\x15FTP
2 to host | \e[B
2 host | \x05\x15localhost
2 port follows | \e[B
2 user | \e[B\x05\x15vftptest
2 password | \e[B\x05\x15@PASS@
2 to dir | \e[B\e[B\e[B
2 rdir | \x05\x15@RDIR@
6 connect | \e[29~
3 clear question | y
5 logged in | \x17
2 to local | \x09
2 local top | \eOP\e[A
2 local dirs | \e[B\e[B
8 put dirs | p
8 put dirs again | p
2 to remote | \x09
2 remote top | \eOP\e[A
2 remote dirs | \e[B
4 enter remote dirs | \r
4 leave remote dirs | \e[D
2 remote top again | \eOP\e[A
2 remote dirs again | \e[B
2 to local again | \x09
2 local jump | j
3 local back | \x05\x15[.BACK]\r
2 to remote last | \x09
8 get dirs | g
3 exit | \e[21~
2 logout check | show default\r
