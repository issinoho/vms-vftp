/* vf_draw.c - draw VFTP's screens into the screen model, in VTREE's style: title
   line with the clock, two framed panes side by side (local, remote), a strip
   with the statistics or a transfer's progress, two menu lines (D2). */
#include <stdio.h>
#include <string.h>
#include "vf_app.h"
#include "vf_util.h"

/* A menu line: '&' marks the next character as a key (bold). */
static void menu_line(vf_screen *s, int row, const char *label, const char *text, unsigned char base)
{
    int col = 1, n;
    vf_role(s, VF_R_MENU);
    vf_fill(s, row, 0, s->cols, ' ', base);
    vf_role(s, VF_R_MENU_LABEL);
    n = vf_put(s, row, col, label, (unsigned char) (base | VF_A_REV), -1);
    vf_role(s, VF_R_MENU);
    col += n + 2;
    for (; *text && col < s->cols; text++) {
        if (*text == '&' && text[1]) {
            text++;
            vf_fill(s, row, col++, 1, (unsigned char) *text, (unsigned char) (base | VF_A_BOLD));
        } else vf_fill(s, row, col++, 1, (unsigned char) *text, base);
    }
    vf_role(s, VF_R_AUTO);
}

static void draw_title(vf_app *a, const char *what)
{
    vf_screen *s = &a->scr;
    char clock[32], t[600];
    vf_when w;
    int cl;
    vfp_now(&w);
    vf_fmt_when(&w, clock, sizeof clock);
    cl = (int) strlen(clock);
    vf_role(s, VF_R_TITLE);
    vf_fill(s, 0, 0, s->cols, ' ', VF_A_REV);
    snprintf(t, sizeof t, "       %s", what);
    vf_put(s, 0, 0, t, VF_A_REV, s->cols - cl - 3);
    vf_put(s, 0, 1, "VFTP", VF_A_REV | VF_A_BOLD, -1);
    vf_put(s, 0, s->cols - cl - 1, clock, VF_A_REV, -1);
    vf_role(s, VF_R_AUTO);
}

static void draw_bottom(vf_app *a, const char *l1label, const char *l1, const char *l2)
{
    vf_screen *s = &a->scr;
    int r1 = s->rows - 2, r2 = s->rows - 1;
    if (a->prompt != P_NONE) {
        int n;
        vf_fill(s, r1, 0, s->cols, ' ', 0);
        n = vf_put(s, r1, 1, a->prompt_label, VF_A_BOLD, -1);
        if (a->prompt == P_ASK) {
            s->cursor_on = 1; s->cursor_row = r1; s->cursor_col = 1 + n;
            menu_line(s, r2, "CHOOSE", "&Y do it   any other key: do nothing   Up/Down scroll", VF_A_REV);
            return;
        }
        {
            int width = s->cols - 2 - n, start = 0;
            if (width < 10) width = 10;
            if (a->pedit.pos >= width) start = a->pedit.pos - width + 1;
            vf_put(s, r1, 1 + n, a->pedit.buf + start, 0, width);
            s->cursor_on = 1; s->cursor_row = r1; s->cursor_col = 1 + n + a->pedit.pos - start;
        }
        menu_line(s, r2, "EDIT", "Return accept   F10 cancel   Ctrl/U erase   Ctrl/H start   Ctrl/E end", VF_A_REV);
        return;
    }
    if (a->msg[0]) {
        vf_fill(s, r1, 0, s->cols, ' ', 0);
        vf_put(s, r1, 1, a->msg, a->msg_err ? (VF_A_BOLD | VF_A_REV) : VF_A_BOLD, s->cols - 2);
    } else menu_line(s, r1, l1label, l1, 0);
    menu_line(s, r2, "COMMANDS", l2, VF_A_REV);
}

/* ---------- the panes ---------- */

/* A frame title on the top line: " LOCAL DKA0:[IAIN] ", cut from the left so the
   end of a long directory shows. */
static void pane_title(vf_app *a, int which, int c1, int c2)
{
    vf_screen *s = &a->scr;
    const vf_pane *p = &a->pane[which];
    char t[VF_SPEC_MAX + 64];
    const char *dir = p->dir;
    int room = c2 - c1 - 2, n;
    if (which == PANE_REMOTE && !a->rem) dir = "(not connected)";
    snprintf(t, sizeof t, " %s %s ", which == PANE_LOCAL ? "LOCAL" : "REMOTE", dir);
    n = (int) strlen(t);
    if (n > room && room > 8) {
        char cut[VF_SPEC_MAX + 64];
        snprintf(cut, sizeof cut, " %s ...%s ", which == PANE_LOCAL ? "LOCAL" : "REMOTE",
                 dir + strlen(dir) - (size_t) (room - 14 > 0 ? room - 14 : 0));
        vf_strlcpy(t, cut, sizeof t);
    }
    vf_put(s, 1, c1 + 1, t, a->act == which ? (VF_A_BOLD | VF_A_REV) : VF_A_BOLD, room);
}

static void draw_pane(vf_app *a, int which, int r1, int r2, int c1, int c2)
{
    vf_screen *s = &a->scr;
    vf_pane *p = &a->pane[which];
    int width = c2 - c1 + 1, rows = r2 - r1 + 1, i;
    char buf[600];
    if (which == PANE_REMOTE && !a->rem) {
        vf_put(s, r1 + 1, c1 + 1, "Not connected.", 0, width - 1);
        vf_put(s, r1 + 3, c1 + 1, "O connects: SFTP, SCP or FTP.", 0, width - 1);
        return;
    }
    if (!p->nvis || (p->nvis == 1 && p->e[p->vis[0]].is_parent)) {
        const char *why = (p->status & 1) ? (p->n > 1 ? "No file matches the filter" : "Empty directory")
                                          : "The directory could not be read";
        vf_put(s, r1 + (p->nvis ? 1 : 0), c1 + 1, why, 0, width - 1);
        if (!p->nvis) return;
    }
    if (p->cur < p->top) p->top = p->cur;
    if (p->cur >= p->top + rows) p->top = p->cur - rows + 1;
    if (p->top > p->nvis - rows) p->top = p->nvis - rows;
    if (p->top < 0) p->top = 0;
    for (i = 0; i < rows && p->top + i < p->nvis; i++) {
        const vf_entry *e = &p->e[p->vis[p->top + i]];
        int row = r1 + i;
        unsigned char at = e->is_dir ? VF_A_BOLD : 0;
        if (e->tagged) vf_gfx(s, row, c1, VF_G_DIAMOND, VF_A_BOLD);
        vf_pane_row(p, e, width - 1, a->year, buf, sizeof buf);
        if (p->top + i == p->cur) {
            at = a->act == which ? VF_A_REV : (VF_A_REV | VF_A_UNDER);   /* the other pane's cursor dimmed */
            vf_fill(s, row, c1 + 1, width - 1, ' ', at);
        }
        vf_put(s, row, c1 + 1, buf, at, width - 1);
    }
}

static void draw_strip(vf_app *a, int row, int c1, int c2)
{
    vf_screen *s = &a->scr;
    char left[400], right[200], b1[16], b2[16];
    int w = c2 - c1 + 1;
    if (a->xfer_on) {
        double secs = vfp_clock() - a->xfer_t0, rate = secs > 0.2 ? (double) a->batch_done / secs : 0;
        int pct = a->xfer_total ? (int) (a->xfer_done * 100 / a->xfer_total) : 0, bar = 20, fill, i;
        char barb[32], left2[200];
        if (pct > 100) pct = 100;       /* a VMS text file is listed larger than it travels (N4) */
        fill = pct * bar / 100;
        for (i = 0; i < bar; i++) barb[i] = i < fill ? '#' : '.';
        barb[bar] = '\0';
        vf_fmt_bytes((unsigned long long) rate, b1, sizeof b1);
        vf_fmt_bytes(a->xfer_done, b2, sizeof b2);
        snprintf(left2, sizeof left2, "%s %d/%d %s", a->xfer_get ? "Get" : "Put", a->xfer_i, a->xfer_n, a->xfer_name);
        snprintf(right, sizeof right, "[%s] %3d%% %6s %6s/s %s  F10 cancels", barb, pct, b2, b1,
                 a->xfer_text ? "ASCII" : "BINARY");
        vf_put(s, row, c1, left2, VF_A_BOLD, w - (int) strlen(right) - 2);
        vf_put(s, row, c2 - (int) strlen(right) + 1, right, 0, -1);
        return;
    }
    {
        const vf_pane *p = &a->pane[a->act];
        int files, dirs, tn;
        unsigned long long by, tby;
        unsigned int bl, tbl;
        vf_pane_totals(p, &files, &dirs, &by, &bl);
        vf_pane_tagged(p, &tn, &tby, &tbl);
        if (p->local) {
            snprintf(left, sizeof left, "%d file%s %u blocks", files, files == 1 ? "" : "s", bl);
            if (tn) snprintf(left + strlen(left), sizeof left - strlen(left), "  tagged %d (%u blocks)", tn, tbl);
        } else {
            vf_fmt_bytes(by, b1, sizeof b1);
            snprintf(left, sizeof left, "%d file%s %s bytes", files, files == 1 ? "" : "s", b1);
            if (tn) { vf_fmt_bytes(tby, b2, sizeof b2); snprintf(left + strlen(left), sizeof left - strlen(left), "  tagged %d (%s)", tn, b2); }
        }
        if (strcmp(p->filter, "*") && p->filter[0])
            snprintf(left + strlen(left), sizeof left - strlen(left), "  showing %s", p->filter);
        if (a->rem) snprintf(right, sizeof right, "Mode %s   %s %s@%s", vf_xmode_name(a->xmode),
                             vf_proto_name(a->proto), a->user, a->host);
        else snprintf(right, sizeof right, "Mode %s   not connected", vf_xmode_name(a->xmode));
        vf_put(s, row, c1, left, 0, w - (int) strlen(right) - 2);
        vf_put(s, row, c2 - (int) strlen(right) + 1, right, VF_A_BOLD, w);
    }
}

static void draw_main(vf_app *a)
{
    vf_screen *s = &a->scr;
    int top = 1, mid = s->cols / 2, sep = s->rows - 5, bot = s->rows - 3;
    char title[600];
    vf_box(s, top, 0, bot, s->cols - 1);
    vf_vline(s, mid, top + 1, sep - 1);
    vf_gfx(s, top, mid, VF_G_TTEE, 0);
    vf_hline(s, sep, 1, s->cols - 2);
    vf_gfx(s, sep, 0, VF_G_LTEE, 0);
    vf_gfx(s, sep, mid, VF_G_BTEE, 0);
    vf_gfx(s, sep, s->cols - 1, VF_G_RTEE, 0);
    pane_title(a, PANE_LOCAL, 0, mid);
    pane_title(a, PANE_REMOTE, mid, s->cols - 1);
    draw_pane(a, PANE_LOCAL, top + 1, sep - 1, 1, mid - 1);
    draw_pane(a, PANE_REMOTE, top + 1, sep - 1, mid + 1, s->cols - 2);
    draw_strip(a, sep + 1, 2, s->cols - 3);
    if (a->rem) snprintf(title, sizeof title, "%s %s@%s:%s",
                         a->proto == VFR_FTP ? "ftp" : a->proto == VFR_SCP ? "scp" : "sftp",
                         a->user, a->host, a->pane[PANE_REMOTE].dir);
    else snprintf(title, sizeof title, "%s on %s", a->sess.user, a->sess.node);
    draw_title(a, title);
    if (a->gold)
        draw_bottom(a, "GOLD", "&O close  &Tag all  &Untag all  &Versions  &W read again  Up/Down: ends",
                    "PF1 cancel GOLD");
    else
        draw_bottom(a, "FILE", "&Open &Get &Put &Copy &Delete &Rename &Mkdir &Tag &View &Attr &X-mode &Sort &Filter",
                    "PF1 GOLD  Tab other pane  Return open  Left up  &Jump  Help  F10 exit");
}

/* ---------- text screens ---------- */

static void draw_text(vf_app *a, const char *label, const char *l1, const char *l2)
{
    vf_screen *s = &a->scr;
    int rows = s->rows - 5, i;
    draw_title(a, a->text_title);
    vf_box(s, 1, 0, s->rows - 3, s->cols - 1);
    if (a->ttop > a->ntext - rows) a->ttop = a->ntext - rows;
    if (a->ttop < 0) a->ttop = 0;
    for (i = 0; i < rows && a->ttop + i < a->ntext; i++) {
        const char *t = a->text[a->ttop + i];
        unsigned char at = (a->mode == M_HELP && t[0] >= 'A' && t[0] <= 'Z' && t[1] >= 'A' && t[1] <= 'Z')
                           ? VF_A_BOLD : 0;
        if (a->mode == M_CONFIRM && t[0] == ' ' && t[1] == ' ' && t[2] != ' ') at = VF_A_BOLD;
        vf_put(s, 2 + i, 2, t, at, s->cols - 4);
    }
    if (a->ttop + rows < a->ntext) vf_put(s, s->rows - 3, s->cols - 10, " more ", VF_A_BOLD, -1);
    draw_bottom(a, label, l1, l2);
}

/* ---------- the Open form ---------- */

static void draw_form(vf_app *a)
{
    vf_screen *s = &a->scr;
    int i, r = 2, lw = 18, vc = 2 + lw + 1, vw = s->cols - vc - 3;
    int ftp = !vf_strcasecmp(a->fcur == FF_PROTO ? a->fedit.buf : a->fv[FF_PROTO], "FTP");
    char stars[VF_FIELD];
    {
        char t[VF_SITE_NAME + 40];
        snprintf(t, sizeof t, a->site_name[0] ? "Open a connection: the site %s" : "Open a connection", a->site_name);
        draw_title(a, t);
    }
    vf_box(s, 1, 0, s->rows - 3, s->cols - 1);
    for (i = 0; i < FF_COUNT && r < s->rows - 6; i++, r++) {
        const char *v = i == a->fcur ? a->fedit.buf : a->fv[i];
        int secret = i == FF_PASSWORD || i == FF_PHRASE, n = (int) strlen(v), start = 0;
        if (secret) {
            memset(stars, '*', (size_t) n);
            stars[n] = '\0';
            v = stars;
        }
        vf_put(s, r, 2, vf_field_label(i), i == a->fcur ? VF_A_BOLD : 0, lw);
        if (i == a->fcur) {
            if (a->fedit.pos >= vw) start = a->fedit.pos - vw + 1;
            vf_fill(s, r, vc, vw, ' ', VF_A_REV);
            vf_put(s, r, vc, v + start, VF_A_REV, vw);
            s->cursor_on = 1; s->cursor_row = r; s->cursor_col = vc + a->fedit.pos - start;
        } else {
            vf_fill(s, r, vc, vw, ' ', VF_A_UNDER);
            vf_put(s, r, vc, v, VF_A_UNDER, vw);
        }
    }
    r = s->rows - 5;
    if (a->form_err[0]) {
        /* two lines: an error names a fingerprint or the server's methods */
        int w = s->cols - 4, n = (int) strlen(a->form_err), cut = n > w ? w : n;
        if (n > w) { while (cut > w / 2 && a->form_err[cut] != ' ') cut--; if (cut <= w / 2) cut = w; }
        vf_put(s, r, 2, a->form_err, VF_A_BOLD | VF_A_REV, cut);
        if (n > cut) vf_put(s, r + 1, 2, a->form_err + cut + (a->form_err[cut] == ' '), VF_A_BOLD | VF_A_REV, w);
    } else vf_put(s, r, 2, vf_field_hint(a->fcur), 0, s->cols - 4);
    vf_put(s, r - 1, 2, ftp ? "FTP: TLS if the server has it; if not, you are asked before a password goes."
                            : "The server's key is checked against known_hosts in your SSH directory.",
           0, s->cols - 4);
    menu_line(s, s->rows - 2, "FORM", "Up/Down Tab Return: field to field   Ctrl/U erase   Ctrl/A overstrike", 0);
    menu_line(s, s->rows - 1, "COMMANDS", "Do or Enter: connect   F10 cancel   Help", VF_A_REV);
}

/* ---------- the Sites screen (D14) ---------- */

static void draw_sites(vf_app *a)
{
    vf_screen *s = &a->scr;
    int rows = s->rows - 6, i, w = s->cols - 4;
    char t[VF_SPEC_MAX + 32], line[700], uhp[400];
    snprintf(t, sizeof t, "Saved sites   %s", a->sites_file);
    draw_title(a, t);
    vf_box(s, 1, 0, s->rows - 3, s->cols - 1);
    vf_put(s, 2, 2, "Site              Proto User@host:port                Remote directory", VF_A_BOLD, w);
    if (!a->sites.n)
        vf_put(s, 4, 2, "No sites saved yet. Fill the Open form (O), then here press A.", 0, w);
    if (a->site_cur < a->site_top) a->site_top = a->site_cur;
    if (a->site_cur >= a->site_top + rows) a->site_top = a->site_cur - rows + 1;
    for (i = 0; i < rows && a->site_top + i < a->sites.n; i++) {
        const vf_site *e = &a->sites.s[a->site_top + i];
        int cur = a->site_top + i == a->site_cur;
        snprintf(uhp, sizeof uhp, "%s%s%s%s%s", e->user, e->user[0] ? "@" : "", e->host,
                 e->port[0] ? ":" : "", e->port);
        snprintf(line, sizeof line, "%-17.17s %-5s %-29.29s %s", e->name, e->proto, uhp, e->remote);
        if (cur) vf_fill(s, 3 + i, 2, w, ' ', VF_A_REV);
        vf_put(s, 3 + i, 2, line, cur ? VF_A_REV : 0, w);
    }
    draw_bottom(a, "SITES", "Return: the Open form, filled   &Add the Open form's   &Delete",
                "Up/Down choose   No password is ever saved   F10 back");
}

void vf_app_draw(vf_app *a)
{
    vf_screen_clear(&a->scr);
    switch (a->mode) {
    case M_HELP: draw_text(a, "HELP", "Up/Down Prev/Next scroll", "Any other key returns"); break;
    case M_VIEW: draw_text(a, "VIEW", "Up/Down Prev/Next scroll", "Any other key returns"); break;
    case M_INFO: draw_text(a, "ATTRIBUTES", "", "Any key returns"); break;
    case M_CONFIRM: draw_text(a, "CONFIRM", "", ""); break;
    case M_FORM: draw_form(a); break;
    case M_SITES: draw_sites(a); break;
    default: draw_main(a); break;
    }
}
