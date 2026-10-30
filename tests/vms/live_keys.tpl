# PTYDRIVE script for test_live.com (tools/live.sh fills in @PORT@, this node's
# own sshd, and @RDIR@, the server-side scratch directory [.LIVE.REMOTE]).
# "<secs> <label> | <keys>". No password is ever typed: the logins are the test
# key (to this node) and the account's key to GitHub, which has no SFTP.
# PTYDRIVE knows \r \e \\ \xHH only: Tab is \x09. Pick files by filter and GOLD
# Down (the last entry), not by counting rows: [.LIVE] holds directories too.
2 dcl | set terminal/width=80/page=24\r
8 start | vftp\r
3 form closed | \e[21~
3 filter | f
2 filter txt | \x05\x15*.TXT\r
2 last | \eOP\e[B
3 view | \r
2 view back | \x20
3 attributes | a
2 attributes back | \x20
2 filter all | f\x05\x15*\r
3 mkdir | m
4 mkdir name | NEWDIR\r
3 delete dir | d
4 yes delete | y
3 open | o
2 host | \x05\x15localhost
2 to port | \e[B
2 port | \x05\x15@PORT@
2 to dir | \e[B\e[B\e[B\e[B\e[B
2 rdir | \x05\x15@RDIR@
10 connect | \e[29~
3 not trusted | n
10 connect again | \e[29~
8 trust | y
2 remote down | \e[B
6 get bin | g
2 remote down2 | \e[B
6 get txt | g
4 view remote | v
2 view remote back | \x20
2 to local | \x09
2 local filter | f\x05\x15*.TXT\r
2 local last | \eOP\e[B
6 put txt | p
6 put again | p
2 to remote | \x09
3 remote mkdir | m
5 remote mkdir name | SUBDIR\r
2 remote last | \eOP\e[B
3 remote rename | r
5 rename name | \x05\x15moved.txt\r
2 remote last again | \eOP\e[B
3 remote delete | d
5 yes remote delete | y
4 close | \eOPo
3 open github | o
2 gh host | \x05\x15github.com
2 gh port | \e[B\x05\x1522
2 gh user | \e[B\x05\x15git
2 gh key | \e[B\e[B\x05\x15USER$ROOT:[IAIN.SSH]ID_ED25519.
15 gh connect | \e[29~
3 cancel form | \e[21~
3 help | \e[28~
2 help back | \x20
3 exit | \e[21~
2 logout check | show default\r
