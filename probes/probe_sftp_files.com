$! probe_sftp_files.com - make test files in [.PROBE] for the SFTP server probe
$! (NOTES N2): a variable-record text file in two versions, and a file of
$! fixed 512-byte records. Then fetch them from Linux with sftp.
$ set noon
$ if f$search("PROBE.DIR") .eqs. "" then create/directory [.PROBE]
$ open/write f [.PROBE]VARTEXT.TXT
$ write f "first line"
$ write f ""
$ write f "third line, after an empty one"
$ close f
$ copy/nolog [.PROBE]VARTEXT.TXT [.PROBE]VARTEXT.TXT
$ create/fdl=sys$input [.PROBE]FIX512.BIN
RECORD
	FORMAT	fixed
	SIZE	512
$ open/append f [.PROBE]FIX512.BIN
$ write f f$fao("!512*A")
$ close f
$ directory/size=all/full [.PROBE]
