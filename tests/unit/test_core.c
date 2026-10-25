/* test_core.c - the pure core: strings and dates, wildcards, the line editor,
   the screen model and its colour rendering. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vf_util.h"
#include "vf_edit.h"
#include "vf_screen.h"
#include "vf_ansi.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKS(a, b) do { checks++; if (strcmp((a), (b))) { fails++; \
    printf("FAIL %s:%d: %s\n   got  [%s]\n   want [%s]\n", __FILE__, __LINE__, #a, (a), (b)); } } while (0)

static void test_util(void)
{
    long d;
    char b[64];
    vf_when w = { 2026, 10, 25, 9, 5 };
    CHECK(vf_parse_date("30-APR-2027", &d) && d == vf_days(2027, 4, 30));
    CHECK(!vf_parse_date("29-FEB-2027", 0));
    CHECK(vf_parse_date("29-FEB-2028", 0));
    CHECK(vf_days(1970, 1, 1) == 0);
    CHECKS(vf_fmt_when(&w, b, sizeof b), "25-OCT-2026 09:05");
    CHECK(vf_match("*.TXT", "notes.txt"));
    CHECK(vf_match("LOGIN.%OM", "LOGIN.COM"));
    CHECK(!vf_match("*.C", "main.cpp"));
    vf_strlcpy(b, "  data  ", sizeof b);
    CHECKS(vf_trim(b), "data");
}

static void test_edit(void)
{
    vf_edit e;
    vf_edit_init(&e, "put");
    CHECK(vf_edit_key(&e, ' ') == VF_EDIT_MORE);
    vf_edit_key(&e, 'x');
    CHECKS(e.buf, "put x");
    vf_edit_key(&e, VK_DEL);
    vf_edit_key(&e, VK_CTRL_H);
    vf_edit_key(&e, 'g');
    CHECKS(e.buf, "gput ");
    CHECK(vf_edit_key(&e, VK_RETURN) == VF_EDIT_DONE);
    vf_edit_init(&e, "x");
    CHECK(vf_edit_key(&e, VK_F10) == VF_EDIT_CANCEL);
}

static void test_screen(void)
{
    vf_screen s;
    char text[512];
    vf_cell bar = { 'A', VF_A_REV, 0 }, txt = { 'A', 0, 0 };
    CHECK(vf_screen_init(&s, 4, 12));
    vf_box(&s, 0, 0, 3, 11);
    vf_vline(&s, 5, 0, 3);
    vf_gfx(&s, 0, 5, VF_G_TTEE, 0);       /* the app joins the lines itself */
    vf_gfx(&s, 3, 5, VF_G_BTEE, 0);
    vf_put(&s, 1, 1, "LOCAL", 0, 4);
    vf_put(&s, 1, 6, "REMOTE", 0, -1);
    vf_screen_text(&s, text, sizeof text);
    CHECKS(text, "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac"
                 "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90\n"
                 "\xe2\x94\x82LOCA\xe2\x94\x82REMOTE\n"
                 "\xe2\x94\x82    \xe2\x94\x82     \xe2\x94\x82\n"
                 "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4"
                 "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98\n");
    vf_screen_free(&s);
    /* the bar is reverse video in both modes, so it shows without colour (VLMF D10) */
    CHECKS(vf_ansi_sgr(&bar, 1), "0;36;40;7");
    CHECKS(vf_ansi_sgr(&bar, 0), "0;7");
    CHECKS(vf_ansi_sgr(&txt, 1), "0;37;44");
}

int main(void)
{
    test_util();
    test_edit();
    test_screen();
    printf("test_core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
