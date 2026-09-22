/* test_unit.c - unit tests for formatting, gfx, and the UI driven by synthetic events */
#include "tm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>

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
    CHECK(gfx_textw(&g, "abc") > 0); CHECK(gfx_textw(&g, "iii") < gfx_textw(&g, "WWW"));   /* proportional */
    CHECK(gfx_textw(&g, "") == 0); CHECK(gfx_fonth(&g) >= 12);
    gfx_font(&g, F_BIG); CHECK(gfx_fonth(&g) > 16); gfx_font(&g, F_BOLD); CHECK(gfx_textw(&g, "Hello") >= 20); gfx_font(&g, F_UI);
    /* rounded rect: corners transparent-ish, centre solid */
    gfx_fill(&g, 0, 0, 16, 16, 0xff000000); gfx_rrect(&g, 0, 0, 16, 16, 6, 0xffffffff);
    CHECK(px[0] == 0xff000000); CHECK(px[8 * 16 + 8] == 0xffffffff); CHECK(px[8] == 0xffffffff);
    gfx_line_aa(&g, -10, -10, 30, 30, 0xffff0000);  /* off-canvas must not crash */
    gfx_rrect_a(&g, -5, -5, 30, 30, 8, 0xff00ff00, 128);
    CHECK(gfx_lerp(0xff000000, 0xffffffff, 0.5f) == 0xff7f7f7f);
    CHECK(gfx_lerp(0xffff0000, 0xff0000ff, 0.0f) == 0xffff0000);
    CHECK(gfx_lerp(0xffff0000, 0xff0000ff, 1.0f) == 0xff0000ff);
    CHECK(gfx_lerp(0xfffff5df, 0xffffd07a, 0.3f) != 0xffffffff);
    /* font renders something for every printable char, at both scales */
    for (int sc = 1; sc <= 2; sc++) {
        uint32_t big[64 * 64]; Gfx g2; gfx_init(&g2, big, 64, 64, sc);
        for (int c = 33; c < 127; c++) {
            gfx_fill(&g2, 0, 0, 64, 64, 0); char s[2] = { (char)c, 0 }; gfx_text(&g2, 2, 2, s, 0xffffffff);
            int lit = 0; for (int i = 0; i < 64 * 64; i++) lit += big[i] != 0;
            CHECK(lit > 0);
        }
    }
    /* ellipsis clipping */
    gfx_fill(&g, 0, 0, 16, 16, 0); gfx_text_clip(&g, 0, 0, 10, "a very long string", 0xffffffff);
    CHECK(px[15] == 0);
}

static void test_safety(void)
{
    /* 1. sys_kill refuses a wrong start stamp (pid reuse) and refuses pid 1 / self */
    pid_t c = fork(); if (c == 0) { execl("/bin/sleep", "sleep", "30", (char *)NULL); _exit(1); }
    usleep(100000);
    CHECK(sys_kill((int)c, 123456789ull) == -2);                 /* wrong stamp: refused, child still alive */
    CHECK(kill(c, 0) == 0);
    CHECK(sys_kill(1, 0) == -1);
    CHECK(sys_kill(getpid(), 0) == -1);
    Proc *p = NULL; int n = 0, cap = 0; sys_procs(&p, &n, &cap);
    uint64_t st = 0; for (int i = 0; i < n; i++) if (p[i].pid == (int)c) st = p[i].start;
    CHECK(st != 0);
    CHECK(sys_kill((int)c, st) == 0);                              /* right stamp: killed */
    int status; usleep(50000); CHECK(waitpid(c, &status, WNOHANG) == c);
    free(p);
    /* 2. protected names */
    Proc q; memset(&q, 0, sizeof q); q.pid = 1234;
    snprintf(q.name, sizeof q.name, "explorer.exe"); CHECK(proc_protected(&q));
    snprintf(q.name, sizeof q.name, "Explorer.EXE"); CHECK(proc_protected(&q));
    snprintf(q.name, sizeof q.name, "dwm.exe"); CHECK(proc_protected(&q));
    snprintf(q.name, sizeof q.name, "gnome-shell"); CHECK(proc_protected(&q));
    snprintf(q.name, sizeof q.name, "WindowServer"); CHECK(proc_protected(&q));
    snprintf(q.name, sizeof q.name, "notepad.exe"); CHECK(!proc_protected(&q));
    snprintf(q.name, sizeof q.name, "sleep"); CHECK(!proc_protected(&q));
    snprintf(q.name, sizeof q.name, "sleep"); q.pid = 1; CHECK(proc_protected(&q));
    /* 3. parent link rejected when the "parent" is younger than the child (recycled pid) */
    Proc par, ch; memset(&par, 0, sizeof par); memset(&ch, 0, sizeof ch);
    par.pid = 500; par.start = 1000; ch.pid = 600; ch.ppid = 500; ch.start = 2000;
    CHECK(proc_is_child_of(&ch, &par));
    par.start = 3000; CHECK(!proc_is_child_of(&ch, &par));
    ch.ppid = 501; par.start = 1000; CHECK(!proc_is_child_of(&ch, &par));
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

    /* sorting: click PID header (2nd column, x = 260 + 30) -> descending then ascending by pid */
    click(u, hdr.x + 260 + 30, hdr.y + hdr.h - 8, 1); ui_state(u, &st); CHECK(st.sort_col == 1 && st.sort_dir == -1);
    click(u, hdr.x + 260 + 30, hdr.y + hdr.h - 8, 1); ui_state(u, &st); CHECK(st.sort_col == 1 && st.sort_dir == 1);
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

    /* a protected (shell-like) child must survive "End process tree" and get the red dialog on direct end */
    {
        extern void ui_test_rename(UI *, int, const char *);
        pid_t c2 = fork();
        if (c2 == 0) { execl("/bin/sh", "sh", "-c", "sleep 30 & sleep 30 & wait", (char *)NULL); _exit(1); }
        usleep(200000); ui_force_sample(u);
        Proc *pl = NULL; int pn = 0, pc = 0; sys_procs(&pl, &pn, &pc);
        int kid = -1; for (int i = 0; i < pn; i++) if (pl[i].ppid == (int)c2) { kid = pl[i].pid; break; }
        free(pl); CHECK(kid > 0);
        ui_test_rename(u, kid, "explorer.exe");
        e = ev_key(K_ESC, 0); ui_event(u, &e);
        char ps[16]; snprintf(ps, sizeof ps, "%d", (int)c2); type_str(u, ps); ui_state(u, &st); CHECK(st.nview == 1);
        e = ev_key(K_DOWN, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.sel_pid == (int)c2);
        e = ev_key(K_DEL, KM_SHIFT); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 2);
        e = ev_key(K_ENTER, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 0);
        usleep(100000);
        CHECK(waitpid(c2, &status, WNOHANG) == c2);        /* the sh parent died... */
        CHECK(kill(kid, 0) == 0);                          /* ...but the "explorer.exe" child was spared */
        e = ev_key(K_ESC, 0); ui_event(u, &e); ui_force_sample(u); ui_test_rename(u, kid, "explorer.exe");
        snprintf(ps, sizeof ps, "%d", kid); type_str(u, ps); ui_state(u, &st); CHECK(st.nview == 1);
        e = ev_key(K_DOWN, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.sel_pid == kid);
        e = ev_key(K_DEL, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 6 /* END_CRIT */); ui_draw(u);
        e = ev_key(K_ENTER, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 0);
        CHECK(kill(kid, 0) == 0);                          /* plain Enter cancels, does not kill */
        e = ev_key(K_DEL, 0); ui_event(u, &e); e = ev_key(K_ENTER, KM_CTRL); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 0);
        usleep(100000); CHECK(kill(kid, 0) != 0);         /* Ctrl+Enter = End anyway */
        e = ev_key(K_ESC, 0); ui_event(u, &e);
    }
    /* Run new task dialog */
    e = ev_char('n', KM_CTRL); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 7 /* RUN */); ui_draw(u);
    type_str(u, "definitely-not-a-program-xyz"); e = ev_key(K_ENTER, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 5 /* ERROR */); ui_draw(u);
    e = ev_key(K_ESC, 0); ui_event(u, &e); ui_state(u, &st); CHECK(st.dlg == 0);

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
    ui_set_theme(u, TM_THEME_DARK); ui_draw(u); px = ui_pixels(u, &pw, &ph); CHECK(px[0] == 0xff202020);
    ui_set_theme(u, TM_THEME_LIGHT); ui_draw(u); px = ui_pixels(u, &pw, &ph); CHECK(px[0] == 0xfff3f3f3);
    ui_set_theme(u, TM_THEME_SYSTEM);
    ui_destroy(u);
}

static uint32_t *load_png(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc(n); size_t r = fread(d, 1, n, f); fclose(f);
    uint32_t *px = NULL; int rc = r == (size_t)n ? png_decode(d, r, &px, w, h) : -1; free(d);
    return rc == 0 ? px : NULL;
}

static void test_png(void)
{
    int w, h; uint32_t *px;
    px = load_png("tests/png/rgba8.png", &w, &h); CHECK(px != NULL);
    if (px) { CHECK(w == 4 && h == 2); CHECK(px[0] == 0xffff0000); CHECK(px[1] == 0xff00ff00); CHECK(px[2] == 0xff0000ff); CHECK((px[3] >> 24) == 0);
              CHECK(px[4] == 0xffffffff); CHECK(px[5] == 0xff000000); CHECK(px[6] == 0x80808080); CHECK(px[7] == 0xffffff00); free(px); }
    px = load_png("tests/png/rgba16.png", &w, &h); CHECK(px != NULL);
    if (px) { CHECK(w == 4 && h == 2); CHECK(px[0] == 0xffff0000); CHECK(px[6] == 0x80808080); CHECK(px[7] == 0xffffff00); free(px); }
    px = load_png("tests/png/rgb8_filters.png", &w, &h); CHECK(px != NULL);
    if (px) { CHECK(w == 2 && h == 4); for (int i = 0; i < 8; i++) CHECK(px[i] == 0xff0a141e); free(px); }
    px = load_png("tests/png/pal2.png", &w, &h); CHECK(px != NULL);
    if (px) { CHECK(w == 4 && h == 1); CHECK(px[0] == 0xffff0000); CHECK(px[1] == 0xff00ff00); CHECK(px[2] == 0xff0000ff); CHECK((px[3] >> 24) == 0); free(px); }
    px = load_png("tests/png/gray1.png", &w, &h); CHECK(px != NULL);
    if (px) { CHECK(w == 8); CHECK(px[0] == 0xffffffff); CHECK(px[1] == 0xff000000); CHECK(px[7] == 0xff000000); free(px); }
    px = load_png("tests/png/ga8.png", &w, &h); CHECK(px != NULL);
    if (px) { CHECK(px[0] == 0xffc8c8c8); CHECK(px[1] == 0x00323232); free(px); }
    /* garbage must be rejected, not crash */
    unsigned char junk[64] = { 137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 4, 0, 0, 0, 4, 8, 6, 0, 0, 0 };
    CHECK(png_decode(junk, sizeof junk, &px, &w, &h) != 0);
    CHECK(png_decode(junk, 3, &px, &w, &h) != 0);
    /* scaler: 4x4 solid -> 2x2 solid; checkerboard alpha averages */
    uint32_t src[16]; for (int i = 0; i < 16; i++) src[i] = (i % 2) ? 0xffff0000 : 0x00000000;
    uint32_t dst[4]; icon_scale(src, 4, 4, dst, 2);
    for (int i = 0; i < 4; i++) { CHECK((dst[i] >> 24) == 127 || (dst[i] >> 24) == 128); CHECK((dst[i] & 0xffffff) == 0xff0000); }
}

static void test_icons(void)
{
    Proc p; memset(&p, 0, sizeof p);
    strcpy(p.name, "bash"); strcpy(p.cmd, "/bin/bash"); CHECK(icon_generic_kind(&p) == IC_TERMINAL);
    strcpy(p.name, "firefox"); strcpy(p.cmd, "/usr/lib/firefox/firefox"); CHECK(icon_generic_kind(&p) == IC_BROWSER);
    strcpy(p.name, "python3"); CHECK(icon_generic_kind(&p) == IC_PYTHON);
    strcpy(p.name, "kworker/0:1"); strcpy(p.cmd, "[kworker/0:1]"); CHECK(icon_generic_kind(&p) == IC_KERNEL);
    strcpy(p.name, "systemd"); strcpy(p.cmd, "/sbin/init"); p.pid = 1; CHECK(icon_generic_kind(&p) == IC_SYSTEM);
    strcpy(p.name, "sshd"); CHECK(icon_generic_kind(&p) == IC_SHIELD);
    strcpy(p.name, "myapp"); strcpy(p.cmd, "/opt/myapp"); strcpy(p.user, "alice"); p.pid = 500; CHECK(icon_generic_kind(&p) == IC_APP);
    strcpy(p.name, "cupsd"); strcpy(p.user, "root"); CHECK(icon_generic_kind(&p) == IC_SERVICE);
    /* every generic kind renders with some opaque pixels at both sizes, and cache returns stable pointers */
    uint32_t px[64 * 64]; Gfx g; gfx_init(&g, px, 64, 64, 1);
    for (int k = 0; k < IC_COUNT; k++) {
        for (int sz = 16; sz <= 32; sz += 16) {
            gfx_fill(&g, 0, 0, 64, 64, 0xff000000);
            Icon ic = { sz, NULL, k }; icon_draw(&g, 0, 0, &ic);
            int lit = 0; for (int i = 0; i < 64 * 64; i++) lit += px[i] != 0xff000000;
            CHECK(lit > sz * sz / 6);
            CHECK(px[63 * 64 + 63] == 0xff000000);    /* stays inside its box */
        }
    }
    strcpy(p.name, "bash");
    const Icon *a = icon_for(&p, 16), *b = icon_for(&p, 16), *c = icon_for(&p, 32);
    CHECK(a == b); CHECK(a != c); CHECK(a->size == 16 && c->size == 32); CHECK(a->kind == IC_TERMINAL);
    /* real icon from theme dir if present on this machine */
    strcpy(p.name, "debian-logo"); strcpy(p.cmd, "/usr/bin/debian-logo");
    FILE *f = fopen("/usr/share/pixmaps/debian-logo.png", "rb");
    if (f) { fclose(f); const Icon *d = icon_for(&p, 16); CHECK(d->px != NULL); }
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
    test_fmt(); test_gfx(); test_png(); test_icons(); test_sys(); test_safety(); test_bmp(); test_ui();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
