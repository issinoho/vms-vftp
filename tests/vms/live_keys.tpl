# PTYDRIVE script for test_live.com (tools/live.sh fills in @PORT@, this node's
# own sshd). "<secs> <label> | <keys>". No password is ever typed: the only
# login is the account's key to GitHub, which has no SFTP (an error path).
2 dcl | set terminal/width=80/page=24\r
8 start | vftp\r
3 form closed | \e[21~
3 filter | f
2 filter txt | \x05\x15*.TXT\r
2 down | \e[B\e[B
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
10 connect | \e[29~
3 not trusted | n
3 open github | \e[21~o
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
