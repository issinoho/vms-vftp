/* vf_screen.c - the screen model. No I/O. */
#include <stdlib.h>
#include <string.h>
#include "vf_screen.h"

int vf_screen_init(vf_screen *s, int rows, int cols)
{
    memset(s, 0, sizeof *s);
    s->cell = (vf_cell *) malloc((size_t) rows * (size_t) cols * sizeof (vf_cell));
    if (!s->cell) return 0;
    s->rows = rows; s->cols = cols;
    vf_screen_clear(s);
    return 1;
}

void vf_screen_free(vf_screen *s) { free(s->cell); s->cell = 0; }

void vf_screen_clear(vf_screen *s)
{
    int i;
    for (i = 0; i < s->rows * s->cols; i++) { s->cell[i].ch = ' '; s->cell[i].attr = 0; s->cell[i].role = 0; }
    s->cursor_on = 0;
    s->role = VF_R_AUTO;
}

void vf_role(vf_screen *s, unsigned char role) { s->role = role; }

vf_cell *vf_at(vf_screen *s, int row, int col)
{
    if (row < 0 || col < 0 || row >= s->rows || col >= s->cols) return 0;
    return &s->cell[row * s->cols + col];
}

int vf_put(vf_screen *s, int row, int col, const char *text, unsigned char attr, int max)
{
    int n = 0;
    vf_cell *c;
    for (; *text && (max < 0 || n < max); text++, n++) {
        unsigned char ch = (unsigned char) *text;
        if (!(c = vf_at(s, row, col + n))) break;
        if (ch < 32 || ch == 127 || (ch >= 128 && ch < 160)) ch = '?';
        c->ch = ch; c->attr = attr; c->role = s->role;
    }
    return n;
}

void vf_fill(vf_screen *s, int row, int col, int count, unsigned char ch, unsigned char attr)
{
    int i;
    vf_cell *c;
    for (i = 0; i < count; i++)
        if ((c = vf_at(s, row, col + i))) { c->ch = ch; c->attr = attr; c->role = s->role; }
}

void vf_gfx(vf_screen *s, int row, int col, unsigned char g, unsigned char attr)
{
    vf_cell *c = vf_at(s, row, col);
    if (c) { c->ch = g; c->attr = (unsigned char) (attr | VF_A_GFX); c->role = s->role; }
}

void vf_hline(vf_screen *s, int row, int c1, int c2)
{
    int c;
    for (c = c1; c <= c2; c++) vf_gfx(s, row, c, VF_G_HLINE, 0);
}

void vf_vline(vf_screen *s, int col, int r1, int r2)
{
    int r;
    for (r = r1; r <= r2; r++) vf_gfx(s, r, col, VF_G_VLINE, 0);
}

void vf_box(vf_screen *s, int r1, int c1, int r2, int c2)
{
    vf_hline(s, r1, c1 + 1, c2 - 1);
    vf_hline(s, r2, c1 + 1, c2 - 1);
    vf_vline(s, c1, r1 + 1, r2 - 1);
    vf_vline(s, c2, r1 + 1, r2 - 1);
    vf_gfx(s, r1, c1, VF_G_TL, 0); vf_gfx(s, r1, c2, VF_G_TR, 0);
    vf_gfx(s, r2, c1, VF_G_BL, 0); vf_gfx(s, r2, c2, VF_G_BR, 0);
}

void vf_attr_run(vf_screen *s, int row, int col, int count, unsigned char attr)
{
    int i;
    vf_cell *c;
    for (i = 0; i < count; i++)
        if ((c = vf_at(s, row, col + i)))
            c->attr = (unsigned char) ((c->attr & VF_A_GFX) | attr);
}

static const char *gfx_utf8(unsigned char g)
{
    switch (g) {
    case 'q': return "\xe2\x94\x80"; case 'x': return "\xe2\x94\x82";
    case 'l': return "\xe2\x94\x8c"; case 'k': return "\xe2\x94\x90";
    case 'm': return "\xe2\x94\x94"; case 'j': return "\xe2\x94\x98";
    case 't': return "\xe2\x94\x9c"; case 'u': return "\xe2\x94\xa4";
    case 'w': return "\xe2\x94\xac"; case 'v': return "\xe2\x94\xb4";
    case 'n': return "\xe2\x94\xbc"; case '`': return "\xe2\x97\x86";
    default: return "?";
    }
}

int vf_screen_text(const vf_screen *s, char *out, int size)
{
    int r, c, o = 0;
    for (r = 0; r < s->rows; r++) {
        int last = s->cols - 1;
        while (last >= 0 && s->cell[r * s->cols + last].ch == ' '
               && !(s->cell[r * s->cols + last].attr & VF_A_GFX)) last--;
        for (c = 0; c <= last; c++) {
            const vf_cell *cl = &s->cell[r * s->cols + c];
            if (cl->attr & VF_A_GFX) {
                const char *u = gfx_utf8(cl->ch);
                int n = (int) strlen(u);
                if (o + n >= size - 2) return o;
                memcpy(out + o, u, (size_t) n); o += n;
            } else {
                if (o >= size - 2) return o;
                out[o++] = (char) cl->ch;
            }
        }
        out[o++] = '\n';
    }
    out[o] = '\0';
    return o;
}
