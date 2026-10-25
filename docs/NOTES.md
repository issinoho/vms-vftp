# Notes: what was observed on the nodes

## N1. What the nodes have (2026-10-25, x86 E9.2-4 and IA64 V8.4-2L3)

Read-only probe (PRODUCT SHOW, logical names, SYS$SYSTEM and SYS$SHARE). Both
nodes: VSI TCP/IP Services V6.0-31 (with `TCPIP$FTP_CLIENT.EXE` and the FTP
server), VSI OpenSSH V9.9-2C (`SSH$ROOT` = `SYS$SYSDEVICE:[VMS$COMMON.OPENSSH.]`,
with `[BIN]`, `[ETC]`, `[LIBEXEC]`, `[SBIN]`), SSL3 (x86 V3.0-21, IA64 V3.0-22)
and SSL31 V3.1-4, with `SSL3$LIBCRYPTO_SHR32.EXE` and `SSL3$LIBSSL_SHR32.EXE`
(32-bit pointer interfaces) in SYS$SHARE. No `SSH`, `SFTP` or `SCP` global
symbols are defined. From the laptop: the IA64 node answers on ports 21 and 22;
the x86 node's sshd is reached on another port and its FTP port does not answer.
The Linux laptop has no libssh2 or OpenSSL headers.
