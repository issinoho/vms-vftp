/* vf_path.h - names and paths on both sides. No I/O.

   Remote paths are Unix paths ("/home/iain/notes.txt"); a VMS server shows its
   own files as "/USER$ROOT/iain/notes.txt;1" (NOTES N2). Local directories are
   "DEV:[A.B]" as the platform normalises them (no rooted or concealed forms). */
#ifndef VF_PATH_H
#define VF_PATH_H

#include <stddef.h>

/* Remote */
void vf_rjoin(const char *dir, const char *name, char *out, size_t size);
/* "/a/b" -> "/a", "/a" -> "/", "/" -> "/". Returns 0 if dir is already the top. */
int  vf_rparent(const char *dir, char *out, size_t size);
/* The last element of a path ("/a/b" -> "b"). */
const char *vf_rbase(const char *path);

/* Local */
/* "DEV:[A]" + "B" -> "DEV:[A.B]"; "DEV:[000000]" + "B" -> "DEV:[B]". */
void vf_lsubdir(const char *dir, const char *name, char *out, size_t size);
/* "DEV:[A.B]" -> "DEV:[A]", "DEV:[A]" -> "DEV:[000000]". Returns 0 at the top. */
int  vf_lparent(const char *dir, char *out, size_t size);
/* "DEV:[A]" + "NOTES.TXT;1" -> "DEV:[A]NOTES.TXT;1". */
void vf_lfile(const char *dir, const char *name, char *out, size_t size);
/* "NOTES.TXT;1" -> "NOTES"; "SRC.DIR;1" -> "SRC" (a directory entry's name). */
void vf_ldirname(const char *entry, char *out, size_t size);

/* Names across (D8). */
/* A remote name as a local file name (no version), for a disk that is ODS-5 or
   not. ODS-5: characters RMS needs escaped get a "^" ("^_" for a space), every
   dot but the last becomes "^.". ODS-2: upper case, anything outside A-Z 0-9 _ $ -
   becomes "_", extra dots too, name and type cut to 39. A ";n" version from a
   VMS server is dropped. Returns 1 if the name had to change (beyond case). */
int  vf_local_name(const char *remote, int ods5, char *out, size_t size);
/* A local file name as a remote one: the version and an empty type's dot go,
   "^" escapes are undone, and a name with no lower-case letters is lower-cased
   (as VMS FTP clients do). */
void vf_remote_name(const char *local, char *out, size_t size);

/* D6: is a remote name text by its type? list: types separated by blanks or
   commas (".TXT .C"), or 0 for the built-in list. */
int  vf_text_type(const char *name, const char *list);
extern const char vf_default_text_types[];

#endif
