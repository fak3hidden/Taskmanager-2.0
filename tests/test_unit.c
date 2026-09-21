/* test_unit.c - unit tests for formatting, gfx, and the UI driven by synthetic events */
#include "tm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define STREQ(a, b) (strcmp((a), (b)) == 0)

static void test_fmt(void)
{
    char b[32];
    fmt_bytes(b, 32, 0); CHECK(STREQ(b, "0 B"));
    fmt_bytes(b, 32, 999); CHECK(STREQ(b, "999 B"));
    fmt_bytes(b, 32, 1536); CHECK(STREQ(b, "1.50 KB"));
    fmt_bytes(b, 32, 50.0 * 1024 * 1024); CHECK(STREQ(b, "50.0 MB"));
    fmt_bytes(b, 32, 3.85 * 1024 * 1024 * 1024); CHECK(STREQ(b, "3.85 GB"));
    fmt_rate(b, 32, 0); CHECK(STREQ(b, "0 B/s"));
    fmt_rate(b, 32, 2.5 * 1024 * 1024); CHECK(STREQ(b, "2.5 MB/s"));
    fmt_time(b, 32, 0); CHECK(STREQ(b, "0:00:00"));
    fmt_time(b, 32, 3661); CHECK(STREQ(b, "1:01:01"));
    fmt_time(b, 32, 90061); CHECK(STREQ(b, "1:01:01:01"));
}

static void test_gfx(void)
{
    uint32_t px[16 * 16]; Gfx g; gfx_init(&g, px, 16, 16, 1);
    gfx_fill(&g, 0, 0, 16, 16, 0xff000000);
    gfx_fill(&g, 2, 2, 4, 4, 0xffff0000);
    CHECK(px[2 * 16 + 2] == 0xffff0000); CHECK(px[5 * 16 + 5] == 0xffff0000);
    CHECK(px[6 * 16 + 6] == 0xff000000); CHECK(px[1 * 16 + 2] == 0xff000000);
    /* clipping */
    gfx_clip(&g, 0, 0, 8, 8); gfx_fill(&g, 6, 6, 8, 8, 0xff00ff00); gfx_noclip(&g);
    CHECK(px[7 * 16 + 7] == 0xff00ff00); CHECK(px[8 * 16 + 8] == 0xff000000);
    /* out of bounds must not crash */
    gfx_fill(&g, -100, -100, 500, 500, 0xff0000ff); gfx_line(&g, -5, -5, 40, 40, 0xffffffff);
    gfx_text(&g, -3, -3, "Hello", 0xffffffff); gfx_text(&g, 14, 14, "xyz", 0xffffffff);
    CHECK(gfx_textw(&g, "abc") == 3 * FONT_ADV);
    CHECK(gfx_lerp(0xff000000, 0xffffffff, 0.5f) == 0xff7f7f7f);
    CHECK(gfx_lerp(0xffff0000, 0xff0000ff, 0.0f) == 0xffff0000);
    CHECK(gfx_lerp(0xffff0000, 0xff0000ff, 1.0f) == 0xff0000ff);
    CHECK(gfx_lerp(0xfffff5df, 0xffffd07a, 0.3f) != 0xffffffff);
    /* every glyph has 45 cells */
    for (int i = 0; i < 95; i++) CHECK(strlen(tm_font[i]) == FONT_W * FONT_H);
    /* font renders something for every printable char */
    for (int c = 33; c < 127; c++) {
        gfx_fill(&g, 0, 0, 16, 16, 0); char s[2] = { (char)c, 0 }; gfx_text(&g, 1, 1, s, 0xffffffff);
        int lit = 0; for (int i = 0; i < 256; i++) lit += px[i] != 0;
        CHECK(lit > 0);
    }
}

static void test_sys(void)
{
    Sys s; CHECK(sys_init(&s) == 0);
    CHECK(s.ncpu >= 1); CHECK(s.mem_total > 0); CHECK(s.mem_used <= s.mem_total);
    CHECK(s.cpu_total[0] > 0);
    Proc *p = NULL; int n = 0, cap = 0;
    CHECK(sys_procs(&p, &n, &cap) == 0); CHECK(n > 1);
    int found_self = 0; for (int i = 0; i < n; i++) if (p[i].pid == getpid()) { found_self = 1; CHECK(STREQ(p[i].name, "test_unit")); CHECK(p[i].threads >= 1); CHECK(p[i].rss > 0); }
    CHECK(found_self);
    free(p);
    char u[32]; sys_username(0, u, 32); CHECK(STREQ(u, "root"));
    CHECK(sys_now_ns() > 0);
}

static Event ev_mouse(int type, int x, int y, int btn) { Event e; memset(&e, 0, sizeof e); e.type = type; e.x = x; e.y = y; e.button = btn; return e; }
static Event ev_key(int key, int mods) { Event e; memset(&e, 0, sizeof e); e.type = EV_KEY; e.key = key; e.mods = mods; return e; }
static Event ev_char(int ch, int mods) { Event e; memset(&e, 0, sizeof e); e.type = EV_CHAR; e.ch = ch; e.mods = mods; return e; }
static void click(UI *u, int x, int y, int btn) { Event e = ev_mouse(EV_MOUSE_MOVE, x, y, 0); ui_event(u, &e); e = ev_mouse(EV_MOUSE_DOWN, x, y, btn); ui_event(u, &e); e = ev_mouse(EV_MOUSE_UP, x, y, btn); ui_event(u, &e); }
static void click_rect(UI *u, Rect r, int btn) { click(u, r.x + r.w / 2, r.y + r.h / 2, btn); }
static void type_str(UI *u, const char *s) { for (; *s; s++) { Event e = ev_char(*s, 0); ui_event(u, &e); } }

static void test_ui(void)
{
    /* spawn a sleeping child with children so we have a known tree */
    pid_t child = fork();
    if (child == 0) { execl("/bin/sh", "sh", "-c", "sleep 30 & sleep 30 & wait", (char *)NULL); _exit(1); }
    usleep(200000);

    UI *u = ui_create(1);
    ui_resize(u, 900, 600);
    ui_tick(u);
    usleep(150000);
    ui_force_sample(u);
    ui_draw(u);
    UIState st; ui_state(u, &st);
    CHECK(st.tab == 0); CHECK(st.nproc > 2); CHECK(st.nview > 0); CHECK(st.nview <= st.nproc);
    CHECK(st.sort_col == 3 /* CPU */ && st.sort_dir == -1);

    Rect tabs[3], endb, treeb, hdr, body; int rh;
    ui_hit_rects(u, tabs, &endb, &treeb, &hdr, &body, &rh);

    /* tab switching by click and Tab key */
    click_rect(u, tabs[1], 1); ui_state(u, &st); CHECK(st.tab == 1);
    click_rect(u, tabs[2], 1); ui_state(u, &st); CHECK(st.tab == 2);
    Event e = ev_key(K_TAB, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.tab == 0);
    e = ev_key(K_TAB, KM_SHIFT); ui_event(u, &e); ui_state(u, &st); CHECK(st.tab == 2);
    e = ev_char('1', KM_CTRL); ui_event(u, &e); ui_state(u, &st); CHECK(st.tab == 0);

    /* select first row with click; keyboard nav */
    ui_draw(u); ui_hit_rects(u, tabs, &endb, &treeb, &hdr, &body, &rh);
    click(u, body.x + 50, body.y + rh / 2, 1); ui_state(u, &st); CHECK(st.sel_pid == ui_view_pid(u, 0));
    e = ev_key(K_DOWN, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.sel_pid == ui_view_pid(u, 1));
    e = ev_key(K_END, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.sel_pid == ui_view_pid(u, st.nview - 1));
    e = ev_key(K_HOME, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.sel_pid == ui_view_pid(u, 0));
    e = ev_key(K_ESC, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.sel_pid == -1);

    /* sorting: click PID header (2nd column, x = 230 + 27) -> ascending by pid */
    click(u, hdr.x + 230 + 27, hdr.y + hdr.h - 8, 1); ui_state(u, &st); CHECK(st.sort_col == 1 && st.sort_dir == -1);
    click(u, hdr.x + 230 + 27, hdr.y + hdr.h - 8, 1); ui_state(u, &st); CHECK(st.sort_col == 1 && st.sort_dir == 1);
    CHECK(ui_view_pid(u, 0) < ui_view_pid(u, 1));
    /* name header -> ascending alpha */
    click(u, hdr.x + 20, hdr.y + hdr.h - 8, 1); ui_state(u, &st); CHECK(st.sort_col == 0 && st.sort_dir == 1);

    /* search filters to our child "sh" + sleeps */
    type_str(u, "sleep"); ui_state(u, &st); CHECK(STREQ(st.search, "sleep")); CHECK(st.nview >= 2 && st.nview < st.nproc);
    e = ev_key(K_BS, 0); ui_event(u, &e); ui_state(u, &st); CHECK(STREQ(st.search, "slee"));
    e = ev_key(K_ESC, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.search[0] == 0);
    char pidstr[16]; snprintf(pidstr, sizeof pidstr, "%d", (int)child);
    type_str(u, pidstr); ui_state(u, &st); CHECK(st.nview == 1); CHECK(ui_view_pid(u, 0) == (int)child);
    e = ev_key(K_ESC, 0); ui_event(u, &e);

    /* tree view: child (sh) must have depth+1 sleeps directly after it */
    e = ev_char('t', KM_CTRL); ui_event(u, &e); ui_state(u, &st); CHECK(st.tree == 1);
    int row = -1; for (int i = 0; i < st.nview; i++) if (ui_view_pid(u, i) == (int)child) row = i;
    CHECK(row >= 0);
    if (row >= 0) {
        int d = ui_view_depth(u, row);
        CHECK(ui_view_depth(u, row + 1) == d + 1); CHECK(ui_view_depth(u, row + 2) == d + 1);
        /* collapse it with Left key */
        for (int i = 0; i < st.nview; i++) { } /* select via search+enter would open props; select directly */
        ui_draw(u); ui_hit_rects(u, tabs, &endb, &treeb, &hdr, &body, &rh);
        /* scroll so row is visible then click it */
        e = ev_key(K_HOME, 0); ui_event(u, &e);
        for (int i = 0; i < row; i++) { e = ev_key(K_DOWN, 0); ui_event(u, &e); }
        ui_state(u, &st); CHECK(st.sel_pid == (int)child);
        int before = st.nview;
        e = ev_key(K_LEFT, 0); ui_event(u, &e); ui_state(u, &st); if (st.nview != before - 2) fprintf(stderr, "  before=%d after=%d sel=%d child=%d depth=%d\n", before, st.nview, st.sel_pid, (int)child, ui_view_depth(u, row)); CHECK(st.nview == before - 2);
        e = ev_key(K_RIGHT, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.nview == before);
    }
    e = ev_char('t', KM_CTRL); ui_event(u, &e); ui_state(u, &st); CHECK(st.tree == 0);

    /* menu: open View, navigate, choose "Paused" */
    e = ev_key(K_F10, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.menu_open == 0);
    e = ev_key(K_RIGHT, 0); ui_event(u, &e); e = ev_key(K_RIGHT, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.menu_open == 2);
    for (int i = 0; i < 11; i++) { e = ev_key(K_DOWN, 0); ui_event(u, &e); }
    e = ev_key(K_ENTER, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.menu_open == -1); CHECK(st.paused == 1);
    e = ev_char(' ', 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.paused == 0);
    e = ev_key(K_F10, 0); ui_event(u, &e); e = ev_key(K_ESC, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.menu_open == -1);

    /* context menu + properties dialog + end task dialog on our child */
    type_str(u, pidstr); ui_draw(u); ui_hit_rects(u, tabs, &endb, &treeb, &hdr, &body, &rh);
    click(u, body.x + 50, body.y + rh / 2, 3); ui_state(u, &st); CHECK(st.ctx_open == 1); CHECK(st.sel_pid == (int)child);
    e = ev_key(K_ESC, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.ctx_open == 0);
    e = ev_key(K_ENTER, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 3 /* PROPS */); ui_draw(u);
    e = ev_key(K_ESC, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 0);
    e = ev_key(K_F1, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 4 /* ABOUT */); ui_draw(u);
    e = ev_key(K_ESC, 0); ui_event(u, &e);
    /* End task button -> dialog -> Enter kills child tree */
    click_rect(u, endb, 1); ui_state(u, &st); CHECK(st.dlg == 1 /* END */); ui_draw(u);
    e = ev_key(K_ESC, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 0);
    e = ev_key(K_DEL, KM_SHIFT); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 2 /* END_TREE */);
    e = ev_key(K_ENTER, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 0);
    int status = 0; usleep(100000);
    CHECK(waitpid(child, &status, WNOHANG) == child);
    CHECK(WIFSIGNALED(status));
    ui_force_sample(u); ui_state(u, &st); CHECK(st.nview == 0);   /* search still active; child gone */
    e = ev_key(K_ESC, 0); ui_event(u, &e);

    /* performance tab pages & per-core toggle */
    e = ev_char('2', KM_CTRL); ui_event(u, &e); ui_state(u, &st); CHECK(st.tab == 1);
    e = ev_key(K_DOWN, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.perf_page == 1);
    e = ev_key(K_DOWN, 0); ui_event(u, &e); e = ev_key(K_DOWN, 0); ui_event(u, &e); e = ev_key(K_DOWN, 0); ui_event(u, &e);
    ui_state(u, &st); CHECK(st.perf_page == 0);
    for (int pg = 0; pg < 4; pg++) { ui_set_tab(u, 1, pg); ui_draw(u); }
    e = ev_char('l', KM_CTRL); ui_event(u, &e); ui_set_tab(u, 1, 0); ui_draw(u);

    /* resize to tiny / large must not crash */
    ui_resize(u, 480, 320); ui_set_tab(u, 0, 0); ui_draw(u); ui_set_tab(u, 2, 0); ui_draw(u); ui_set_tab(u, 1, 0); ui_draw(u);
    ui_resize(u, 1920, 1080); ui_draw(u);
    ui_resize(u, 900, 600); ui_set_tab(u, 2, 0); ui_draw(u);
    /* wheel scroll on details */
    ui_hit_rects(u, tabs, &endb, &treeb, &hdr, &body, &rh);
    Event w = ev_mouse(EV_WHEEL, body.x + 10, body.y + 10, 0); w.delta = -1; ui_event(u, &w); ui_state(u, &st); CHECK(st.scroll >= 0);
    w.mods = KM_SHIFT; ui_event(u, &w); ui_draw(u);

    /* quit paths */
    e = ev_char('q', KM_CTRL); CHECK(ui_event(u, &e) == -1);
    Event q; memset(&q, 0, sizeof q); q.type = EV_QUIT; CHECK(ui_event(u, &q) == -1);

    /* pixels sanity: frame has the accent colour somewhere (active tab marker) */
    ui_set_tab(u, 0, 0); ui_draw(u);
    int pw, ph; const uint32_t *px = ui_pixels(u, &pw, &ph); int accent = 0;
    for (int i = 0; i < pw * ph; i++) accent += px[i] == 0xff0078d4;
    CHECK(accent > 10);
    ui_destroy(u);
}

static void test_bmp(void)
{
    uint32_t px[4] = { 0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff };
    CHECK(bmp_write("/tmp/tm_test.bmp", px, 2, 2) == 0);
    FILE *f = fopen("/tmp/tm_test.bmp", "rb"); CHECK(f != NULL);
    if (f) { unsigned char h[54 + 16]; size_t n = fread(h, 1, sizeof h, f); fclose(f);
        CHECK(n == 54 + 16); CHECK(h[0] == 'B' && h[1] == 'M'); CHECK(h[54] == 255 && h[55] == 0 && h[56] == 0); /* bottom-left = blue, BGR order */ }
    unlink("/tmp/tm_test.bmp");
}

int main(void)
{
    test_fmt(); test_gfx(); test_sys(); test_bmp(); test_ui();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
