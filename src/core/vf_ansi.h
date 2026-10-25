/* vf_ansi.h - turn the screen model into VT100/VT220 sequences (D11).

   Colour mode uses the XTree Gold palette (blue screen, white text, yellow keys
   and labels, cyan frames, a cyan highlight bar) through ANSI SGR colours, which
   DECterm, xterm and PuTTY show and a real VT220/VT320 ignores. Monochrome mode
   uses bold, underline and reverse only, as SMG$ would. Line drawing uses the DEC
   Special Graphics set (ESC ( 0). Only cells that differ from prev are sent. */
#ifndef VF_ANSI_H
#define VF_ANSI_H

#include "vf_screen.h"

typedef void (*vf_emit_fn)(void *ctx, const char *bytes, int len);

/* Send cur. prev is what the terminal shows now (cur's size), or 0 for a full
   repaint; it is updated to cur. */
void vf_ansi_render(const vf_screen *cur, vf_cell *prev, int colour, vf_emit_fn emit, void *ctx);
/* Before the first frame (colour background, cursor off) and at exit (normal
   rendition, ASCII, cursor on, screen cleared). */
void vf_ansi_begin(int colour, vf_emit_fn emit, void *ctx);
void vf_ansi_end(vf_emit_fn emit, void *ctx);
/* The SGR parameters a cell gets, e.g. "0;30;46". For tests. */
const char *vf_ansi_sgr(const vf_cell *c, int colour);

#endif
