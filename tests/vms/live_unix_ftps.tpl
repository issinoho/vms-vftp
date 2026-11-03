# PTYDRIVE script for test_live.com UNIX: VFTP over FTPS to pyftpdlib on Linux,
# which requires TLS on the control and data connections (tools/live_unix_ftp.sh
# --tls fills in @HOST@ @PORT@ @PASS@ @RDIR@, and deletes the filled file from the
# node after the run). The certificate is asked about as an SSH key is (D12): N
# first, then Y; no question about the password, which goes over TLS; and at the
# end a second login, which must not ask again. The rest as live_unix_ftp.tpl.
# PTYDRIVE knows \r \e \\ \xHH.
# A wait is at most 30 seconds: PTYDRIVE takes a longer one as 1.5.
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
3 cert question | n
6 connect again | \e[29~
3 cert question again | y
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
2 local big | f\x05\x15BIG.BIN\r
2 local big last | \eOP\e[B
3 put big | p
0.3 replace big | y
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
3 open again | o
2 to password again | \e[B\e[B\e[B
2 password again | \x05\x15@PASS@
6 reconnect | \e[29~
4 logged in again | \x17
4 close again | \eOPo
3 exit | \e[21~
2 logout check | show default\r
