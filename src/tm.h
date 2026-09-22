/*
 * tm.h - shared types for the Task Manager
 *
 * The program is split in three layers:
 *   sys_*.c   platform data collection (processes, CPU, memory, disk, net)
 *   win_*.c   a tiny windowing shim (open window, poll events, blit pixels)
 *   gfx.c/ui.c software renderer + the actual task manager user interface
 */
#ifndef TM_H
#define TM_H

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
#if !defined(_WIN32) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#include <stdint.h>
#include <stddef.h>

#define TM_VERSION "2.0.0"
#define MAX_CPUS   256
#define HIST       60             /* samples kept per graph (Windows shows 60 s) */

/* ------------------------------------------------------------------ */
/* Platform data                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    int      pid, ppid;
    char     name[64];
    char     user[32];
    char     state;               /* R S D Z T I ... */
    int      nice, prio;
    int      threads;
    int      handles;
    uint64_t cpu_time;            /* cumulative CPU time (ns) */
    uint64_t rss, vsz;            /* bytes */
    uint64_t rd, wr;              /* cumulative I/O bytes */
    uint64_t start;               /* start stamp (detects pid reuse) */
    char     cmd[192];
    /* derived by the UI */
    float    cpu;                 /* % of all cores */
    double   rd_rate, wr_rate;    /* bytes / s */
    int      depth;               /* tree indentation */
    int      nchild;
} Proc;

typedef struct {
    int      ncpu;
    uint64_t cpu_busy[MAX_CPUS + 1];   /* [0] = aggregate, [1..] = per core */
    uint64_t cpu_total[MAX_CPUS + 1];
    uint64_t mem_total, mem_avail, mem_used, mem_cached;
    uint64_t mem_committed, mem_commit_limit;
    uint64_t swap_total, swap_used;
    uint64_t disk_rd, disk_wr;    /* cumulative bytes */
    uint64_t net_rx, net_tx;      /* cumulative bytes */
    uint64_t uptime;              /* seconds */
    int      nproc, nthreads, nhandles;
    double   mhz, base_mhz;
    double   load[3];
    int      sockets, cores;
    char     cpu_model[80];
    char     os[64];
    char     host[64];
    uint64_t t_ns;                /* monotonic time of sample */
} Sys;

int      sys_init(Sys *s);                         /* static info, once */
void     sys_sample(Sys *s);                       /* dynamic counters */
int      sys_procs(Proc **arr, int *n, int *cap);  /* fresh process list */
int      sys_kill(int pid, uint64_t start);        /* 0 = ok; refuses if the pid's start stamp != start (pid reuse) */
int      sys_self_pid(void);
int      sys_spawn(const char *cmdline);            /* start a detached program; 0 = ok */
int      sys_theme_dark(void);                      /* current OS appearance: 1 dark, 0 light/unknown */
uint64_t sys_now_ns(void);
int      sys_username(uint32_t uid, char *buf, int n);

/* ------------------------------------------------------------------ */
/* Window shim                                                         */
/* ------------------------------------------------------------------ */

enum { EV_NONE, EV_QUIT, EV_KEY, EV_CHAR, EV_MOUSE_MOVE, EV_MOUSE_DOWN,
       EV_MOUSE_UP, EV_WHEEL, EV_RESIZE, EV_PAINT };

enum { K_NONE = 0, K_UP = 0x100, K_DOWN, K_LEFT, K_RIGHT, K_PGUP, K_PGDN,
       K_HOME, K_END, K_DEL, K_BS, K_ENTER, K_ESC, K_TAB,
       K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12 };

enum { KM_SHIFT = 1, KM_CTRL = 2, KM_ALT = 4 };

typedef struct {
    int type;
    int key, ch, mods;
    int x, y, button, delta;
    int w, h;
} Event;

int  win_open(const char *title, int w, int h, int *scale);
int  win_poll(Event *ev, int timeout_ms);          /* 1 = got event */
void win_present(const uint32_t *px, int w, int h);
void win_close(void);

/* headless backend used for testing / screenshots */
void headless_config(const char *bmp_path, int w, int h);
int  bmp_write(const char *path, const uint32_t *px, int w, int h);

/* ------------------------------------------------------------------ */
/* Graphics                                                            */
/* ------------------------------------------------------------------ */

typedef struct { int x, y, w, h; } Rect;

typedef struct {
    uint32_t *px;
    int w, h;
    int cx0, cy0, cx1, cy1;       /* clip rect (exclusive max) */
    int s;                        /* ui scale */
    int face;                     /* current font (FontId) */
} Gfx;

enum { F_UI, F_BOLD, F_BIG, F_MID, F_COUNT };

void gfx_init(Gfx *g, uint32_t *px, int w, int h, int scale);
void gfx_clip(Gfx *g, int x, int y, int w, int h);
void gfx_noclip(Gfx *g);
void gfx_fill(Gfx *g, int x, int y, int w, int h, uint32_t c);
void gfx_blend(Gfx *g, int x, int y, int w, int h, uint32_t c, int a);
void gfx_rect(Gfx *g, int x, int y, int w, int h, uint32_t c);
void gfx_rrect(Gfx *g, int x, int y, int w, int h, int r, uint32_t c);
void gfx_rrect_a(Gfx *g, int x, int y, int w, int h, int r, uint32_t c, int a);
void gfx_rrect_b(Gfx *g, int x, int y, int w, int h, int r, uint32_t fill, uint32_t border);
void gfx_hline(Gfx *g, int x, int y, int w, uint32_t c);
void gfx_vline(Gfx *g, int x, int y, int h, uint32_t c);
void gfx_line(Gfx *g, int x0, int y0, int x1, int y1, uint32_t c);
void gfx_line_aa(Gfx *g, float x0, float y0, float x1, float y1, uint32_t c);
void gfx_font(Gfx *g, int f);
int  gfx_fonth(Gfx *g);                       /* line height of current font */
int  gfx_fontpx(Gfx *g);
int  gfx_text(Gfx *g, int x, int y, const char *s, uint32_t c);
int  gfx_textw(Gfx *g, const char *s);
void gfx_text_r(Gfx *g, int xr, int y, const char *s, uint32_t c);
int  gfx_text_v(Gfx *g, int x, int y, int h, const char *s, uint32_t c);
void gfx_text_rv(Gfx *g, int xr, int y, int h, const char *s, uint32_t c);
void gfx_text_mid(Gfx *g, int x, int y, int w, int h, const char *s, uint32_t c);
void gfx_text_clip(Gfx *g, int x, int y, int maxw, const char *s, uint32_t c);
void gfx_tri(Gfx *g, int x, int y, int size, int up, uint32_t c);
void gfx_blit(Gfx *g, int x, int y, const uint32_t *px, int w, int h);
uint32_t gfx_lerp(uint32_t a, uint32_t b, float t);
void icon_scale(const uint32_t *src, int sw, int sh, uint32_t *dst, int size);

/* ------------------------------------------------------------------ */
/* Icons                                                               */
/* ------------------------------------------------------------------ */

enum { IC_APP, IC_TERMINAL, IC_SERVICE, IC_KERNEL, IC_BROWSER, IC_SHIELD, IC_WINDOW,
       IC_PYTHON, IC_NODE, IC_DB, IC_EDITOR, IC_MEDIA, IC_CHAT, IC_FOLDER, IC_SYSTEM, IC_COUNT };

typedef struct {
    int size;                      /* px, e.g. 16 or 32 */
    uint32_t *px;                  /* size*size straight-alpha ARGB, or NULL -> use generic kind */
    int kind;                      /* IC_* generic fallback */
} Icon;

const Icon *icon_for(const Proc *p, int size);     /* cached per process name */
void icon_draw(Gfx *g, int x, int y, const Icon *ic);
int  icon_generic_kind(const Proc *p);
int  os_icon_load(const Proc *p, int size, uint32_t *out);   /* platform: 1 if found */
int  png_decode(const unsigned char *data, size_t len, uint32_t **out, int *w, int *h);

/* ------------------------------------------------------------------ */
/* UI                                                                  */
/* ------------------------------------------------------------------ */

typedef struct UI UI;
UI  *ui_create(int scale);
void ui_destroy(UI *u);
void ui_resize(UI *u, int w, int h);
int  ui_event(UI *u, const Event *e);              /* 1 = wants redraw, -1 = quit */
int  ui_tick(UI *u);                               /* sample if due; 1 = redraw */
int  ui_next_due_ms(UI *u);
void ui_draw(UI *u);
const uint32_t *ui_pixels(UI *u, int *w, int *h);
void ui_set_tab(UI *u, int tab, int perf_page);
void ui_set_interval(UI *u, int ms);
enum { TM_THEME_SYSTEM, TM_THEME_LIGHT, TM_THEME_DARK };
void ui_set_theme(UI *u, int mode);
void ui_force_sample(UI *u);                       /* sample now, regardless of interval */
void ui_dump(UI *u);                               /* text dump to stdout */

/* introspection (tests / scripting) */
typedef struct { int tab, perf_page, nview, nproc, sel_pid, dlg, menu_open, ctx_open, tree, paused, sort_col, sort_dir, scroll; char search[64]; } UIState;
void ui_state(UI *u, UIState *st);
int  ui_view_pid(UI *u, int row);                  /* pid at visible row, -1 if none */
int  ui_view_depth(UI *u, int row);
int  proc_is_child_of(const Proc *c, const Proc *parent);   /* ppid match AND parent older than child (defeats pid reuse) */
int  proc_protected(const Proc *p);                         /* 1 = shell/session-critical (never killed implicitly) */
void ui_hit_rects(UI *u, Rect *tabs3, Rect *end_btn, Rect *tree_btn, Rect *hdr, Rect *body, int *row_h);

/* helpers */
void fmt_bytes(char *out, int n, double b);
void fmt_rate(char *out, int n, double bps);
void fmt_time(char *out, int n, uint64_t secs);

#endif
