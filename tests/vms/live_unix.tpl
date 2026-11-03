# PTYDRIVE script for test_live.com UNIX: VFTP against an OpenSSH server on Linux
# (tools/unix_sftpd.sh; tools/live_unix.sh fills in @HOST@ @PORT@ @RUSER@ @RDIR@).
# "<secs> <label> | <keys>". The login is the test key; no password is typed.
# The server has no versions, so a Put of a name it has asks first (default No),
# and a replacing Put that is cancelled must leave the old file (big.bin) as it was.
# PTYDRIVE knows \r \e \\ \xHH only: Tab is \x09. Pick files by filter and GOLD Down.
# The second connect waits 20s: on IA64 its trust question once took over 18s to show,
# and a key typed before it is lost, so the next one answered it No.
# A wait is at most 30 seconds: PTYDRIVE takes a longer one as 1.5.
2 dcl | set terminal/width=80/page=24\r
8 start | vftp\r
3 form closed | \e[21~
3 open | o
2 host | \x05\x15@HOST@
2 to port | \e[B
2 port | \x05\x15@PORT@
2 user | \e[B\x05\x15@RUSER@
2 to dir | \e[B\e[B\e[B\e[B
2 rdir | \x05\x15@RDIR@
10 connect | \e[29~
3 not trusted | n
20 connect again | \e[29~
8 trust | y
2 remote down | \e[B
3 remote attributes | a
2 remote attributes back | \x20
1 get big | g
4 cancel get | \x03
2 remote down1 | \e[B
6 get bin | g
2 remote down2 | \e[B
6 get txt | g
4 view remote | v
2 view remote back | \x20
2 to local | \x09
2 local big | f\x05\x15BIG.BIN\r
2 local big last | \eOP\e[B
3 put big | p
1 replace big | y
4 cancel put | \x03
# resume (D16; not by SCP: live_unix.sh --scp takes these lines out). The cancels
# above left big.bin_VFTP-PART here and big.bin.vftp-part there. The Get goes on
# from its half (Return); the Put is of the download now, not what its half came
# from, so N starts it again (the half deleted) and Y replaces big.bin with it.
2 resume to remote | \x09
2 resume remote top | \eOP\e[A
2 resume remote big | \e[B
4 resume get big | g
30 resume get go on | \r
2 resume to local | \x09
2 resume local big | f\x05\x15BIG.BIN\r
2 resume local big last | \eOP\e[B
4 resume put big | p
4 resume put again | n
30 resume replace big | y
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
2 chmod | \eOPa
4 chmod mode | \x05\x15go-r\r
2 remote last again | \eOP\e[B
3 remote delete | d
5 yes remote delete | y
4 close | \eOPo
3 exit | \e[21~
2 logout check | show default\r
