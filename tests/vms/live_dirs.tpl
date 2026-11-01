# PTYDRIVE script for test_live.com DIRS over SFTP: whole directories (D13) to this
# node's own sshd (tools/live_dirs.sh fills in @PORT@ and @RDIR@, [.LIVE.REMOTE]),
# logged in with the test key. "<secs> <label> | <keys>". PTYDRIVE knows \r \e \\
# \xHH only: Tab is \x09. In [.LIVE] the order is [-], BACK, DIRS, REMOTE, SSH;
# on the server .., then the directory put. Then the connection is saved as the
# site livesite (D14) and VFTP started again as "vftp livesite". Up a directory is
# Backspace (DEL, \x7f) and the Get is F5 (Copy, from the server pane) here; the FTP
# script uses Left and G.
# Last, a password login (keyboard-interactive or password) as VFTPTEST (D11);
# tools/live_dirs.sh fills in @PASS@ and deletes the filled file after the run.
2 dcl | set terminal/width=80/page=24\r
8 start | vftp\r
2 host | \x05\x15localhost
2 to port | \e[B
2 port | \x05\x15@PORT@
2 to dir | \e[B\e[B\e[B\e[B\e[B
2 rdir | \x05\x15@RDIR@
10 connect | \e[29~
8 trust | y
2 to local | \x09
2 local top | \eOP\e[A
2 local dirs | \e[B\e[B
8 put dirs | p
8 put dirs again | p
2 to remote | \x09
2 remote top | \eOP\e[A
2 remote dirs | \e[B
4 enter remote dirs | \r
4 leave remote dirs | \x7f
2 remote top again | \eOP\e[A
2 remote dirs again | \e[B
2 to local again | \x09
2 local jump | j
3 local back | \x05\x15[.BACK]\r
2 to remote last | \x09
8 get dirs | \e[15~
2 remote last file | \eOP\e[B
2 chmod | \eOPa
4 chmod mode | \x05\x15640\r
2 sites | l
2 site add | a
2 site name | \x05\x15livesite\r
3 sites back | \e[21~
3 exit | \e[21~
8 start site | vftp livesite\r
10 connect site | \e[29~
5 site in | \x17
3 exit again | \e[21~
8 start password | vftp vftptest@localhost:@PORT@\r
2 password | @PASS@
10 connect password | \e[29~
5 password in | \x17
3 exit password | \e[21~
2 logout check | show default\r
