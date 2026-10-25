/* vf_screen.h - the screen model: a grid of cells, each a character, a rendition
   and a character set. The app draws into it; a platform renders it (SMG$ on VMS,
   text for the tests). */
#ifndef VF_SCREEN_H
#define VF_SCREEN_H

#define VF_A_BOLD  1
#define VF_A_REV   2
#define VF_A_UNDER 4
#define VF_A_GFX   8      /* ch is a DEC Special Graphics character */

/* DEC Special Graphics characters (shown as these ASCII codes with VF_A_GFX). */
#define VF_G_DIAMOND '`'
#define VF_G_HLINE   'q'
#define VF_G_VLINE   'x'
#define VF_G_TL      'l'
#define VF_G_TR      'k'
#define VF_G_BL      'm'
#define VF_G_BR      'j'
#define VF_G_LTEE    't'   /* |- */
#define VF_G_RTEE    'u'   /* -| */
#define VF_G_TTEE    'w'
#define VF_G_BTEE    'v'
#define VF_G_CROSS   'n'

/* What a cell is, for the colour palette (D11). VF_R_AUTO: decided by its
   rendition (plain text, bold key, highlight bar, line drawing...). */
enum { VF_R_AUTO, VF_R_TITLE, VF_R_MENU, VF_R_MENU_LABEL };

typedef struct { unsigned char ch, attr, role; } vf_cell;

typedef struct {
    int rows, cols;
    vf_cell *cell;           /* rows * cols */
    int cursor_row, cursor_col, cursor_on;
    unsigned char role;      /* given to every cell written from now on */
} vf_screen;

int  vf_screen_init(vf_screen *s, int rows, int cols);   /* 0 on no memory */
void vf_screen_free(vf_screen *s);
void vf_screen_clear(vf_screen *s);
void vf_role(vf_screen *s, unsigned char role);              /* VF_R_AUTO after a clear */
vf_cell *vf_at(vf_screen *s, int row, int col);           /* 0 when outside */

/* Text at (row, col), cut at max columns (max < 0: to the edge). Control
   characters show as '?'. Returns the columns written. */
int  vf_put(vf_screen *s, int row, int col, const char *text, unsigned char attr, int max);
/* Fill count cells with ch. */
void vf_fill(vf_screen *s, int row, int col, int count, unsigned char ch, unsigned char attr);
void vf_gfx(vf_screen *s, int row, int col, unsigned char g, unsigned char attr);
/* Box with corners at (r1,c1) and (r2,c2). */
void vf_box(vf_screen *s, int r1, int c1, int r2, int c2);
void vf_hline(vf_screen *s, int row, int c1, int c2);
void vf_vline(vf_screen *s, int col, int r1, int r2);
/* Set attributes on a run of cells (e.g. the highlight bar). */
void vf_attr_run(vf_screen *s, int row, int col, int count, unsigned char attr);

/* The grid as UTF-8 text, one line per row, trailing blanks kept off; DEC graphics
   become box-drawing characters. For snapshot tests. Returns bytes written. */
int  vf_screen_text(const vf_screen *s, char *out, int size);

#endif
