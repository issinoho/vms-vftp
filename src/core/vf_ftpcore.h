/* vf_ftpcore.h - the FTP protocol's text, with no I/O (D4): replies, the PASV,
   EPSV and PWD replies, and LIST lines from VMS, Unix and Windows servers into
   vf_entry. src/net/vf_ftp.c does the sockets. What a VMS server sends is in
   NOTES N8. */
#ifndef VF_FTPCORE_H
#define VF_FTPCORE_H

#include <stddef.h>
#include "vf_pane.h"

/* A reply, one line at a time. A multi-line reply starts "ddd-" and ends at a
   line starting "ddd ". */
typedef struct {
    int code;                /* 0 until the first line */
    int done;
    int multi;
    char text[2048];         /* every line's text, without the codes, '\n' between */
} vf_ftp_reply;

void vf_ftp_reply_init(vf_ftp_reply *r);
/* Feed one line, without its CRLF. 1 when the reply is complete. A line that
   starts no reply ("hello") is kept as text and leaves code 0. */
int  vf_ftp_reply_line(vf_ftp_reply *r, const char *line);
/* The reply as one line for a message: the last line's text, and the first's in
   parentheses when they differ ("file not found (Failed to open X; for input.)"). */
void vf_ftp_reply_msg(const vf_ftp_reply *r, char *out, size_t size);

/* "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)": the address and port. 1 if read. */
int vf_ftp_pasv(const char *text, char *host, size_t hsize, int *port);
/* "229 Entering Extended Passive Mode (|||port|)": the port. 1 if read. */
int vf_ftp_epsv(const char *text, int *port);
/* "257 \"USER$ROOT:[iain]\" is current directory.": the quoted path ("" doubled
   inside it is one "). 1 if read. */
int vf_ftp_pwd(const char *text, char *out, size_t size);
/* FEAT's reply text: does a line name feature feat ("REST STREAM"), case-blind,
   as the line's first words? */
int vf_ftp_feat(const char *text, const char *feat);
/* "150 Opening ... (74 bytes)": the size the server announces. 1 if there is one. */
int vf_ftp_size150(const char *text, unsigned long long *bytes);

/* A name from a VMS server's listing, for a command: the ODS-5 escapes undone
   ("My^_File^.tar.gz;1" -> "My File.tar.gz;1"), case and version kept. The
   server escapes again what it needs (N8). */
void vf_ftp_unescape(const char *name, char *out, size_t size);

/* LIST output, one line at a time. The style is found from the lines. */
enum { VF_LS_UNKNOWN, VF_LS_VMS, VF_LS_UNIX, VF_LS_DOS };

typedef struct {
    int style;
    int year, month;         /* today: for Unix dates with no year */
    char held[256];          /* a VMS name too long for its column, waiting for its line */
    char again[512];         /* a line still to read after the held name was given out */
} vf_ftp_list;

void vf_ftp_list_init(vf_ftp_list *l, int year, int month);
/* 1 and *e filled when the line completes an entry; 0 for headers, totals, blank
   lines, "." and "..", and lines that are not understood. After each line call
   vf_ftp_list_more until it gives 0 (a line can complete two entries), and at the
   end vf_ftp_list_end (a long name may still be held). */
int  vf_ftp_list_line(vf_ftp_list *l, const char *line, vf_entry *e);
int  vf_ftp_list_more(vf_ftp_list *l, vf_entry *e);
int  vf_ftp_list_end(vf_ftp_list *l, vf_entry *e);

#endif
