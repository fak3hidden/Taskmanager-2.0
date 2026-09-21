/* ui.c - the Task Manager user interface (immediate-mode, software rendered) */
#include "tm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ---- palette (Windows 11 light, Mica-ish) -------------------------- */
#define C_WINBG   0xfff3f3f3
#define C_PANEL   0xffffffff
#define C_BORDER  0xffe0e0e0
#define C_LINE    0xffededed
#define C_TEXT    0xff1b1b1b
#define C_DIM     0xff616161
#define C_SEL     0xffdbeafe
#define C_SELB    0xff0078d4
#define C_HOVER   0xfff2f6fb
#define C_ACCENT  0xff0067c0
#define C_ACCENT2 0xff1a7fd6
#define C_BTN     0xfffbfbfb
#define C_BTNB    0xffd9d9d9
#define C_HDR     0xffffffff
#define C_MENU    0xfff9f9f9
#define C_SHADOW  0xff000000

#define C_CPU     0xff117dbb
#define C_CPUF    0xffe9f3fa
#define C_CPUG    0xffd0e4f2
#define C_MEM     0xff8b12ae
#define C_MEMF    0xfff4e8f8
#define C_MEMG    0xffe6cfee
#define C_DISK    0xff4da60a
#define C_DISKF   0xffebf6e2
#define C_DISKG   0xffd2e8c0
#define C_NET     0xffa74f01
#define C_NETF    0xfffbefe3
#define C_NETG    0xfff1dcc4

#define MENU_H 24
#define TAB_H 36
#define TOOL_H 40
#define STATUS_H 24
#define SB_W 12
#define ICON_SZ 16

#define P(n) ((n) * u->s)

/* ---- columns ---------------------------------------------------------- */
enum { CL_NAME, CL_PID, CL_STATUS, CL_CPU, CL_MEM, CL_DISK, CL_USER, CL_THREADS, CL_HANDLES,
       CL_CPUTIME, CL_PRIO, CL_NICE, CL_VSZ, CL_RD, CL_WR, CL_PPID, CL_CMD, CL_COUNT };

static const struct { const char *title; int right; int numeric; } coldef[CL_COUNT] = {
    [CL_NAME] = { "Name", 0, 0 },      [CL_PID] = { "PID", 1, 1 },          [CL_STATUS] = { "Status", 0, 0 },
    [CL_CPU] = { "CPU", 1, 1 },        [CL_MEM] = { "Memory", 1, 1 },       [CL_DISK] = { "Disk", 1, 1 },
    [CL_USER] = { "User name", 0, 0 }, [CL_THREADS] = { "Threads", 1, 1 },  [CL_HANDLES] = { "Handles", 1, 1 },
    [CL_CPUTIME] = { "CPU time", 1, 1 }, [CL_PRIO] = { "Base pri", 1, 1 },  [CL_NICE] = { "Nice", 1, 1 },
    [CL_VSZ] = { "Virtual", 1, 1 },    [CL_RD] = { "I/O read", 1, 1 },      [CL_WR] = { "I/O write", 1, 1 },
    [CL_PPID] = { "Parent", 1, 1 },    [CL_CMD] = { "Command line", 0, 0 },
};

typedef struct { int id, w; } Col;
typedef struct {
    Col cols[CL_COUNT]; int ncols;
    int sort_col, sort_dir;          /* dir: 1 asc, -1 desc */
    int scroll, hscroll, hdr_h, row_h, big;
    Rect body, hdr; int hover_row;
} Table;

typedef struct { const char *label; const char *key; int id; int check; int sep; } MenuItem;

enum { A_NONE, A_EXIT, A_REFRESH, A_SPEED_HIGH, A_SPEED_NORMAL, A_SPEED_LOW, A_SPEED_PAUSE, A_TREE,
       A_KERNEL, A_ONLYME, A_CORES, A_ABOUT, A_END, A_END_TREE, A_PROPS, A_GOTO_DETAILS, A_GOTO_PROC,
       A_EXPAND_ALL, A_COLLAPSE_ALL, A_TAB_PROC, A_TAB_PERF, A_TAB_DET };

enum { DLG_NONE, DLG_END, DLG_END_TREE, DLG_PROPS, DLG_ABOUT, DLG_ERROR };

struct UI {
    int s, w, h; uint32_t *px; Gfx g;
    Sys sys, prev;
    Proc *procs; int nproc, cap;
    Proc *prevp; int nprevp, prevcap;
    int *phash; int phcap;           /* pid -> index+1 into prevp */
    int *chash; int chcap;           /* pid -> index+1 into procs */
    int *view; int nview, viewcap;
    int tab, perf_page, perf_cores;
    float hcpu[HIST], hmem[HIST], hdrd[HIST], hdwr[HIST], hnrx[HIST], hntx[HIST];
    float (*hcore)[HIST]; int hpos, hcount;
    float cpu_pct, core_pct[MAX_CPUS];
    double drd, dwr, nrx, ntx;      /* current rates */
    int interval_ms, paused; uint64_t last_sample; int nsamples;
    Table tp, td; int tree, show_kernel, only_me;
    int sel_pid;
    char search[64]; int nsearch;
    int mx, my;
    int menu_open, menu_hover;
    int ctx_open, ctx_x, ctx_y, ctx_pid, ctx_hover, ctx_n;
    int dlg, dlg_pid, dlg_hover; char dlg_name[64], dlg_msg[256];
    Table *drag_tab; int drag_col, drag_x0, drag_w0;
    Table *sb_tab; int sb_y0, sb_scroll0;
    int collapsed[256]; int ncollapsed;
    Rect r_end, r_search, r_tree, r_tabs[3], r_menu[4], r_perf[4], r_cores, r_dlg_btn[2], r_dd, r_ctx;
    Rect r_expand[3];
    int dd_n; const MenuItem *dd_items;
    char me[32];
    uint64_t flash_until; char flash[96];
};

/* ---- formatting -------------------------------------------------------- */
void fmt_bytes(char *out, int n, double b)
{
    const char *unit[] = { "B", "KB", "MB", "GB", "TB" }; int i = 0;
    while (b >= 1000 && i < 4) { b /= 1024; i++; }
    if (i == 0) snprintf(out, n, "%.0f %s", b, unit[i]);
    else if (b < 10) snprintf(out, n, "%.2f %s", b, unit[i]);
    else if (b < 100) snprintf(out, n, "%.1f %s", b, unit[i]);
    else snprintf(out, n, "%.0f %s", b, unit[i]);
}
void fmt_rate(char *out, int n, double bps)
{
    const char *unit[] = { "B/s", "KB/s", "MB/s", "GB/s" }; int i = 0;
    while (bps >= 1000 && i < 3) { bps /= 1024; i++; }
    if (i == 0) snprintf(out, n, "%.0f %s", bps, unit[i]);
    else snprintf(out, n, bps < 10 ? "%.1f %s" : "%.0f %s", bps, unit[i]);
}
void fmt_time(char *out, int n, uint64_t s)
{
    uint64_t d = s / 86400; s %= 86400;
    if (d) snprintf(out, n, "%llu:%02llu:%02llu:%02llu", (unsigned long long)d, (unsigned long long)s / 3600, (unsigned long long)(s % 3600) / 60, (unsigned long long)s % 60);
    else snprintf(out, n, "%llu:%02llu:%02llu", (unsigned long long)s / 3600, (unsigned long long)(s % 3600) / 60, (unsigned long long)s % 60);
}
static const char *state_name(char c)
{
    switch (c) {
    case 'R': return "Running"; case 'S': return ""; case 'D': return "Disk wait"; case 'Z': return "Zombie";
    case 'T': case 't': return "Suspended"; case 'I': return "Idle"; case 'X': return "Dead"; default: return "";
    }
}

/* ---- hashing ------------------------------------------------------------ */
static void hash_build(int **h, int *hcap, Proc *arr, int n)
{
    int cap = 64; while (cap < n * 2) cap <<= 1;
    if (cap != *hcap) { free(*h); *h = malloc(cap * sizeof(int)); *hcap = cap; }
    memset(*h, 0, cap * sizeof(int));
    for (int i = 0; i < n; i++) {
        unsigned k = (unsigned)arr[i].pid * 2654435761u;
        while ((*h)[k & (cap - 1)]) k++;
        (*h)[k & (cap - 1)] = i + 1;
    }
}
static int hash_find(int *h, int hcap, Proc *arr, int pid)
{
    if (!h) return -1;
    unsigned k = (unsigned)pid * 2654435761u;
    for (;;) {
        int v = h[k & (hcap - 1)];
        if (!v) return -1;
        if (arr[v - 1].pid == pid) return v - 1;
        k++;
    }
}
static Proc *find_proc(UI *u, int pid)
{
    int i = hash_find(u->chash, u->chcap, u->procs, pid);
    return i < 0 ? NULL : &u->procs[i];
}

/* ---- table setup --------------------------------------------------------- */
static void table_add(Table *t, int id, int w) { t->cols[t->ncols].id = id; t->cols[t->ncols].w = w; t->ncols++; }

static void tables_init(UI *u)
{
    Table *t = &u->tp; memset(t, 0, sizeof *t);
    table_add(t, CL_NAME, 260); table_add(t, CL_PID, 60); table_add(t, CL_STATUS, 80); table_add(t, CL_CPU, 70);
    table_add(t, CL_MEM, 90); table_add(t, CL_DISK, 90); table_add(t, CL_USER, 90); table_add(t, CL_THREADS, 64);
    t->sort_col = CL_CPU; t->sort_dir = -1; t->hdr_h = 50; t->row_h = 26; t->big = 1; t->hover_row = -1;
    t = &u->td; memset(t, 0, sizeof *t);
    table_add(t, CL_NAME, 180); table_add(t, CL_PID, 56); table_add(t, CL_STATUS, 70); table_add(t, CL_USER, 80);
    table_add(t, CL_CPU, 50); table_add(t, CL_CPUTIME, 80); table_add(t, CL_MEM, 78); table_add(t, CL_VSZ, 78);
    table_add(t, CL_THREADS, 58); table_add(t, CL_HANDLES, 58); table_add(t, CL_PRIO, 58); table_add(t, CL_NICE, 44);
    table_add(t, CL_RD, 78); table_add(t, CL_WR, 78); table_add(t, CL_PPID, 56); table_add(t, CL_CMD, 460);
    t->sort_col = CL_CPU; t->sort_dir = -1; t->hdr_h = 26; t->row_h = 22; t->hover_row = -1;
}

/* ---- lifecycle ----------------------------------------------------------- */
UI *ui_create(int scale)
{
    UI *u = calloc(1, sizeof *u);
    u->s = scale < 1 ? 1 : scale;
    sys_init(&u->sys);
    u->prev = u->sys;
    u->hcore = calloc(MAX_CPUS, sizeof *u->hcore);
    u->interval_ms = 1000;
    u->menu_open = -1; u->sel_pid = -1; u->ctx_pid = -1;
    const char *me = getenv("USER"); if (!me) me = getenv("USERNAME"); if (!me) me = "";
    snprintf(u->me, sizeof u->me, "%s", me);
    tables_init(u);
    return u;
}

void ui_destroy(UI *u)
{
    free(u->px); free(u->procs); free(u->prevp); free(u->view); free(u->phash); free(u->chash); free(u->hcore); free(u);
}

void ui_resize(UI *u, int w, int h)
{
    if (w < 1 || h < 1) return;
    u->w = w; u->h = h;
    u->px = realloc(u->px, (size_t)w * h * 4);
    gfx_init(&u->g, u->px, w, h, u->s);
}

void ui_set_interval(UI *u, int ms) { u->interval_ms = ms; }
void ui_set_tab(UI *u, int tab, int perf_page) { u->tab = tab; u->perf_page = perf_page; }
const uint32_t *ui_pixels(UI *u, int *w, int *h) { *w = u->w; *h = u->h; return u->px; }

/* ---- sorting / view ------------------------------------------------------ */
static UI *g_u; static Table *g_t;
static int cmp_proc(const void *a, const void *b)
{
    const Proc *p = &g_u->procs[*(const int *)a], *q = &g_u->procs[*(const int *)b];
    int d = g_t->sort_dir; double x = 0, y = 0;
    switch (g_t->sort_col) {
    case CL_NAME: { int r = strcasecmp(p->name, q->name); if (r) return r * d; break; }
    case CL_USER: { int r = strcmp(p->user, q->user); if (r) return r * d; break; }
    case CL_STATUS: { int r = strcmp(state_name(p->state), state_name(q->state)); if (r) return r * d; break; }
    case CL_CMD: { int r = strcmp(p->cmd, q->cmd); if (r) return r * d; break; }
    case CL_PID: x = p->pid; y = q->pid; break;
    case CL_PPID: x = p->ppid; y = q->ppid; break;
    case CL_CPU: x = p->cpu; y = q->cpu; break;
    case CL_MEM: x = (double)p->rss; y = (double)q->rss; break;
    case CL_VSZ: x = (double)p->vsz; y = (double)q->vsz; break;
    case CL_DISK: x = p->rd_rate + p->wr_rate; y = q->rd_rate + q->wr_rate; break;
    case CL_RD: x = (double)p->rd; y = (double)q->rd; break;
    case CL_WR: x = (double)p->wr; y = (double)q->wr; break;
    case CL_THREADS: x = p->threads; y = q->threads; break;
    case CL_HANDLES: x = p->handles; y = q->handles; break;
    case CL_CPUTIME: x = (double)p->cpu_time; y = (double)q->cpu_time; break;
    case CL_PRIO: x = p->prio; y = q->prio; break;
    case CL_NICE: x = p->nice; y = q->nice; break;
    }
    if (x < y) return -d; if (x > y) return d;
    return p->pid - q->pid;
}

static int is_collapsed(UI *u, int pid) { for (int i = 0; i < u->ncollapsed; i++) if (u->collapsed[i] == pid) return 1; return 0; }
static void toggle_collapsed(UI *u, int pid)
{
    for (int i = 0; i < u->ncollapsed; i++) if (u->collapsed[i] == pid) { u->collapsed[i] = u->collapsed[--u->ncollapsed]; return; }
    if (u->ncollapsed < 256) u->collapsed[u->ncollapsed++] = pid;
}

static int str_icontains(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) if (!strncasecmp(hay, needle, n)) return 1;
    return 0;
}

static int proc_visible(UI *u, Proc *p)
{
    if (!u->show_kernel && p->cmd[0] == '[') return 0;
    if (u->only_me && u->me[0] && strcmp(p->user, u->me)) return 0;
    if (u->nsearch) {
        char pid[16]; snprintf(pid, sizeof pid, "%d", p->pid);
        if (!str_icontains(p->name, u->search) && !str_icontains(p->cmd, u->search) &&
            !str_icontains(p->user, u->search) && strcmp(pid, u->search)) return 0;
    }
    return 1;
}

static Table *cur_table(UI *u) { return u->tab == 2 ? &u->td : u->tab == 0 ? &u->tp : NULL; }

static void view_push(UI *u, int idx, int depth)
{
    if (u->nview >= u->viewcap) { u->viewcap = u->viewcap ? u->viewcap * 2 : 512; u->view = realloc(u->view, u->viewcap * sizeof(int)); }
    u->procs[idx].depth = depth;
    u->view[u->nview++] = idx;
}

static void rebuild_view(UI *u)
{
    Table *t = cur_table(u);
    if (!t) t = &u->tp;
    u->nview = 0;
    int n = u->nproc;
    int *order = malloc((n + 1) * sizeof(int)); int m = 0;
    int tree = u->tree && u->tab == 0 && !u->nsearch;
    for (int i = 0; i < n; i++) { u->procs[i].nchild = 0; if (tree || proc_visible(u, &u->procs[i])) order[m++] = i; }
    g_u = u; g_t = t;
    qsort(order, m, sizeof(int), cmp_proc);
    if (!tree) { for (int i = 0; i < m; i++) view_push(u, order[i], 0); free(order); return; }

    /* tree: children lists in sorted order, then DFS from roots */
    int *head = malloc(n * sizeof(int)), *tail = malloc(n * sizeof(int)), *next = malloc(n * sizeof(int));
    int *roots = malloc(n * sizeof(int)), nroots = 0; char *seen = calloc(n, 1);
    for (int i = 0; i < n; i++) head[i] = tail[i] = next[i] = -1;
    for (int k = 0; k < m; k++) {
        int i = order[k]; Proc *p = &u->procs[i];
        int par = hash_find(u->chash, u->chcap, u->procs, p->ppid);
        if (par < 0 || par == i || p->ppid == 0 || !proc_visible(u, p)) {
            if (proc_visible(u, p)) roots[nroots++] = i;
            continue;
        }
        if (tail[par] < 0) head[par] = i; else next[tail[par]] = i;
        tail[par] = i; u->procs[par].nchild++;
    }
    /* iterative DFS */
    int *stack = malloc((n + 1) * sizeof(int)), *dstack = malloc((n + 1) * sizeof(int)), sp = 0;
    for (int r = nroots - 1; r >= 0; r--) { stack[sp] = roots[r]; dstack[sp] = 0; sp++; }
    while (sp) {
        sp--; int i = stack[sp], d = dstack[sp];
        if (seen[i]) continue; seen[i] = 1;
        view_push(u, i, d);
        if (is_collapsed(u, u->procs[i].pid)) {
            /* hide the whole subtree: mark descendants seen without pushing them */
            int hs = 0, *hst = malloc((n + 1) * sizeof(int));
            for (int c = head[i]; c >= 0; c = next[c]) hst[hs++] = c;
            while (hs) { int k = hst[--hs]; if (seen[k]) continue; seen[k] = 1; for (int c = head[k]; c >= 0; c = next[c]) hst[hs++] = c; }
            free(hst);
            continue;
        }
        /* push children in reverse so first child pops first */
        int cnt = 0; for (int c = head[i]; c >= 0; c = next[c]) cnt++;
        int *tmp = malloc((cnt + 1) * sizeof(int)); cnt = 0;
        for (int c = head[i]; c >= 0; c = next[c]) tmp[cnt++] = c;
        for (int c = cnt - 1; c >= 0; c--) { stack[sp] = tmp[c]; dstack[sp] = d + 1; sp++; }
        free(tmp);
    }
    /* orphans in cycles / whose parent is hidden */
    for (int k = 0; k < m; k++) if (!seen[order[k]] && proc_visible(u, &u->procs[order[k]])) { seen[order[k]] = 1; view_push(u, order[k], 0); }
    free(order); free(head); free(tail); free(next); free(roots); free(seen); free(stack); free(dstack);
}

/* ---- sampling -------------------------------------------------------------- */
static void push_hist(float *h, int pos, float v) { h[pos] = v; }
static float hist_at(const float *h, int pos, int count, int age)
{
    if (age >= count) return 0;
    return h[(pos - 1 - age + HIST * 2) % HIST];
}

static void sample(UI *u)
{
    u->prev = u->sys;
    sys_sample(&u->sys);
    double dt = (u->sys.t_ns - u->prev.t_ns) / 1e9; if (dt <= 0) dt = 1e-3;

    for (int i = 0; i <= u->sys.ncpu; i++) {
        uint64_t dtot = u->sys.cpu_total[i] - u->prev.cpu_total[i], dbusy = u->sys.cpu_busy[i] - u->prev.cpu_busy[i];
        float pct = dtot ? 100.f * dbusy / dtot : 0;
        if (pct < 0) pct = 0; if (pct > 100) pct = 100;
        if (i == 0) u->cpu_pct = pct; else { u->core_pct[i - 1] = pct; push_hist(u->hcore[i - 1], u->hpos, pct); }
    }
    u->drd = (u->sys.disk_rd - u->prev.disk_rd) / dt; u->dwr = (u->sys.disk_wr - u->prev.disk_wr) / dt;
    u->nrx = (u->sys.net_rx - u->prev.net_rx) / dt; u->ntx = (u->sys.net_tx - u->prev.net_tx) / dt;
    if (u->sys.disk_rd < u->prev.disk_rd) u->drd = 0; if (u->sys.disk_wr < u->prev.disk_wr) u->dwr = 0;
    if (u->sys.net_rx < u->prev.net_rx) u->nrx = 0; if (u->sys.net_tx < u->prev.net_tx) u->ntx = 0;
    push_hist(u->hcpu, u->hpos, u->cpu_pct);
    push_hist(u->hmem, u->hpos, u->sys.mem_total ? 100.f * u->sys.mem_used / u->sys.mem_total : 0);
    push_hist(u->hdrd, u->hpos, (float)u->drd); push_hist(u->hdwr, u->hpos, (float)u->dwr);
    push_hist(u->hnrx, u->hpos, (float)u->nrx); push_hist(u->hntx, u->hpos, (float)u->ntx);
    u->hpos = (u->hpos + 1) % HIST; if (u->hcount < HIST) u->hcount++;

    /* processes: swap buffers, refetch, compute deltas against previous */
    Proc *tmp = u->prevp; int tc = u->prevcap; u->prevp = u->procs; u->nprevp = u->nproc; u->prevcap = u->cap;
    u->procs = tmp; u->cap = tc;
    hash_build(&u->phash, &u->phcap, u->prevp, u->nprevp);
    sys_procs(&u->procs, &u->nproc, &u->cap);
    hash_build(&u->chash, &u->chcap, u->procs, u->nproc);
    int threads = 0;
    for (int i = 0; i < u->nproc; i++) {
        Proc *p = &u->procs[i];
        int j = hash_find(u->phash, u->phcap, u->prevp, p->pid);
        if (j >= 0 && u->prevp[j].start == p->start) {
            Proc *q = &u->prevp[j];
            double dcpu = (double)(p->cpu_time - q->cpu_time) / 1e9;
            p->cpu = (float)(100.0 * dcpu / dt / (u->sys.ncpu ? u->sys.ncpu : 1));
            if (p->cpu < 0) p->cpu = 0; if (p->cpu > 100) p->cpu = 100;
            p->rd_rate = p->rd >= q->rd ? (p->rd - q->rd) / dt : 0;
            p->wr_rate = p->wr >= q->wr ? (p->wr - q->wr) / dt : 0;
        }
        threads += p->threads;
    }
    u->sys.nproc = u->nproc; u->sys.nthreads = threads;
    if (!u->sys.nhandles) { int h = 0; for (int i = 0; i < u->nproc; i++) h += u->procs[i].handles; u->sys.nhandles = h; }
    u->nsamples++;
    rebuild_view(u);
}

int ui_next_due_ms(UI *u)
{
    if (u->paused) return 250;
    uint64_t now = sys_now_ns(), due = u->last_sample + (uint64_t)u->interval_ms * 1000000ull;
    int ms = now >= due ? 0 : (int)((due - now) / 1000000ull);
    if (u->flash_until && ms > 100) ms = 100;
    return ms;
}

int ui_tick(UI *u)
{
    uint64_t now = sys_now_ns();
    int redraw = 0;
    if (u->flash_until && now > u->flash_until) { u->flash_until = 0; redraw = 1; }
    if (u->paused && u->nsamples) return redraw;
    if (now - u->last_sample >= (uint64_t)u->interval_ms * 1000000ull || !u->nsamples) {
        u->last_sample = now;
        sample(u);
        return 1;
    }
    return redraw;
}

void ui_force_sample(UI *u) { u->last_sample = sys_now_ns(); sample(u); }

static void flash(UI *u, const char *msg) { snprintf(u->flash, sizeof u->flash, "%s", msg); u->flash_until = sys_now_ns() + 2500000000ull; }

/* ---- cell content -------------------------------------------------------- */
static void cell_text(UI *u, Proc *p, int col, char *b, int n)
{
    switch (col) {
    case CL_NAME: snprintf(b, n, "%s", p->name); break;
    case CL_PID: snprintf(b, n, "%d", p->pid); break;
    case CL_PPID: snprintf(b, n, "%d", p->ppid); break;
    case CL_STATUS: snprintf(b, n, "%s", state_name(p->state)); break;
    case CL_CPU: snprintf(b, n, p->cpu >= 10 ? "%.0f%%" : "%.1f%%", p->cpu); break;
    case CL_MEM: fmt_bytes(b, n, (double)p->rss); break;
    case CL_VSZ: fmt_bytes(b, n, (double)p->vsz); break;
    case CL_DISK: fmt_rate(b, n, p->rd_rate + p->wr_rate); break;
    case CL_RD: fmt_bytes(b, n, (double)p->rd); break;
    case CL_WR: fmt_bytes(b, n, (double)p->wr); break;
    case CL_USER: snprintf(b, n, "%s", p->user); break;
    case CL_THREADS: snprintf(b, n, "%d", p->threads); break;
    case CL_HANDLES: snprintf(b, n, "%d", p->handles); break;
    case CL_CPUTIME: fmt_time(b, n, p->cpu_time / 1000000000ull); break;
    case CL_PRIO: snprintf(b, n, "%d", p->prio); break;
    case CL_NICE: snprintf(b, n, "%d", p->nice); break;
    case CL_CMD: snprintf(b, n, "%s", p->cmd); break;
    default: b[0] = 0;
    }
    (void)u;
}

static float cell_heat(UI *u, Proc *p, int col)
{
    switch (col) {
    case CL_CPU: return p->cpu / 100.f;
    case CL_MEM: return u->sys.mem_total ? (float)((double)p->rss / u->sys.mem_total) : 0;
    case CL_DISK: return (float)((p->rd_rate + p->wr_rate) / (20.0 * 1024 * 1024));
    }
    return -1;
}

static uint32_t heat_color(float t)
{
    if (t < 0) return C_PANEL;
    if (t > 1) t = 1;
    t = sqrtf(t);
    if (t < 0.5f) return gfx_lerp(0xfffff9ed, 0xffffe0a8, t * 2);
    return gfx_lerp(0xffffe0a8, 0xfff5a25a, (t - 0.5f) * 2);
}

/* ---- widgets --------------------------------------------------------------- */
static int inr(Rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
static Rect R(int x, int y, int w, int h) { Rect r = { x, y, w, h }; return r; }

static void button(UI *u, Rect r, const char *label, int primary, int hover)
{
    Gfx *g = &u->g;
    uint32_t bg = primary ? (hover ? C_ACCENT2 : C_ACCENT) : (hover ? 0xfff4f4f4 : C_BTN);
    uint32_t bd = primary ? bg : (hover ? 0xffc8c8c8 : C_BTNB);
    gfx_rrect_b(g, r.x, r.y, r.w, r.h, P(4), bg, bd);
    if (!primary) gfx_hline(g, r.x + P(3), r.y + r.h - P(1), r.w - P(6), 0xffcccccc);   /* subtle bottom edge */
    gfx_font(g, F_UI);
    gfx_text_mid(g, r.x, r.y, r.w, r.h, label, primary ? 0xffffffff : C_TEXT);
}

static void checkbox_mark(UI *u, int x, int y, uint32_t c)
{
    Gfx *g = &u->g;
    gfx_line_aa(g, x, y + P(4), x + P(3), y + P(7), c); gfx_line_aa(g, x + P(3), y + P(7), x + P(8), y + P(1), c);
    gfx_line_aa(g, x, y + P(5), x + P(3), y + P(8), c); gfx_line_aa(g, x + P(3), y + P(8), x + P(8), y + P(2), c);
}

/* time-series graph */
static void graph(UI *u, Rect r, const float *a, const float *b, float max, uint32_t line, uint32_t fill, uint32_t grid, int mini)
{
    Gfx *g = &u->g;
    gfx_fill(g, r.x, r.y, r.w, r.h, C_PANEL);
    int cells = mini ? 4 : 10;
    for (int i = 1; i < cells; i++) {
        gfx_vline(g, r.x + r.w * i / cells, r.y, r.h, grid);
        if (!mini) gfx_hline(g, r.x, r.y + r.h * i / cells, r.w, grid);
    }
    if (max <= 0) max = 1;
    int n = u->hcount;
    gfx_clip(g, r.x + P(1), r.y + P(1), r.w - P(2), r.h - P(2));
    for (int pass = 0; pass < 2; pass++) {
        const float *h = pass ? b : a;
        if (!h) continue;
        float px = -1, py = -1;
        for (int age = 0; age < HIST; age++) {
            float x = r.x + r.w - 1 - (float)age * (r.w - 1) / (HIST - 1);
            float v = age < n ? hist_at(h, u->hpos, u->hcount, age) : 0;
            if (v > max) v = max;
            float y = r.y + r.h - 1 - v / max * (r.h - 2);
            if (!pass && age < n && px >= 0) {
                int xi0 = (int)x, xi1 = (int)px;
                for (int xi = xi0; xi <= xi1; xi++) {
                    float t = px == x ? 0 : (xi - x) / (px - x), yy = y + (py - y) * t;
                    gfx_fill(g, xi, (int)yy + 1, 1, r.y + r.h - (int)yy, fill);
                }
            }
            if (px >= 0 && age <= n) {
                if (pass) { if ((age & 1) == 0) gfx_line_aa(g, px, py, x, y, line); }
                else gfx_line_aa(g, px, py, x, y, line);
            }
            px = x; py = y;
            if (age >= n) break;
        }
    }
    gfx_noclip(g);
    gfx_rect(g, r.x, r.y, r.w, r.h, gfx_lerp(line, C_PANEL, 0.35f));
}

/* ---- table drawing ---------------------------------------------------------- */
static void draw_table(UI *u, Table *t, Rect area)
{
    Gfx *g = &u->g;
    int hh = P(t->hdr_h), rh = P(t->row_h);
    t->hdr = R(area.x, area.y, area.w - P(SB_W), hh);
    t->body = R(area.x, area.y + hh, area.w - P(SB_W), area.h - hh);
    int rows = t->body.h / rh; if (rows < 1) rows = 1;
    int maxs = u->nview - rows; if (maxs < 0) maxs = 0;
    if (t->scroll > maxs) t->scroll = maxs; if (t->scroll < 0) t->scroll = 0;
    int totw = 0; for (int i = 0; i < t->ncols; i++) totw += P(t->cols[i].w);
    int hbar = totw > t->body.w;
    if (hbar) t->body.h -= P(SB_W);
    int maxh = totw - t->body.w; if (maxh < 0) maxh = 0;
    if (t->hscroll > maxh) t->hscroll = maxh; if (t->hscroll < 0) t->hscroll = 0;
    rows = t->body.h / rh; if (rows < 1) rows = 1;
    maxs = u->nview - rows; if (maxs < 0) maxs = 0;
    if (t->scroll > maxs) t->scroll = maxs;

    int icon_sz = P(ICON_SZ);
    int tree = u->tree && t == &u->tp && !u->nsearch;

    /* header */
    gfx_fill(g, t->hdr.x, t->hdr.y, t->hdr.w, hh, C_HDR);
    gfx_hline(g, t->hdr.x, t->hdr.y + hh - P(1), t->hdr.w, C_BORDER);
    int x = area.x - t->hscroll;
    gfx_clip(g, t->hdr.x, t->hdr.y, t->hdr.w, hh);
    for (int i = 0; i < t->ncols; i++) {
        Col *c = &t->cols[i]; int cw = P(c->w);
        const char *title = coldef[c->id].title;
        int th = t->big ? P(18) : hh;
        int ty = t->big ? t->hdr.y + hh - th - P(1) : t->hdr.y;
        uint32_t tc = c->id == t->sort_col ? C_TEXT : C_DIM;
        gfx_font(g, F_UI);
        if (t->big) {
            char tot[32] = ""; float heat = -1;
            if (c->id == CL_CPU) { snprintf(tot, sizeof tot, "%.0f%%", u->cpu_pct); heat = u->cpu_pct / 100; }
            else if (c->id == CL_MEM) { float pc = u->sys.mem_total ? 100.f * u->sys.mem_used / u->sys.mem_total : 0; snprintf(tot, sizeof tot, "%.0f%%", pc); heat = pc / 100; }
            else if (c->id == CL_DISK) { fmt_rate(tot, sizeof tot, u->drd + u->dwr); heat = (float)((u->drd + u->dwr) / (50.0 * 1024 * 1024)); }
            if (tot[0]) {
                gfx_fill(g, x, t->hdr.y, cw, hh - P(1), heat_color(heat));
                gfx_font(g, F_MID);
                if (gfx_textw(g, tot) > cw - P(12)) gfx_font(g, F_UI);
                gfx_text_r(g, x + cw - P(6), t->hdr.y + P(6), tot, C_TEXT);
                gfx_font(g, F_UI);
            }
        }
        if (coldef[c->id].right) gfx_text_rv(g, x + cw - P(6), ty, th, title, tc);
        else gfx_text_v(g, x + P(6) + (c->id == CL_NAME && !t->big ? 0 : 0), ty, th, title, tc);
        if (c->id == t->sort_col) {
            int ax = coldef[c->id].right ? x + cw - P(6) - gfx_textw(g, title) - P(11) : x + P(6) + gfx_textw(g, title) + P(5);
            gfx_tri(g, ax, ty + (th - P(3)) / 2, 3, t->sort_dir > 0, C_DIM);
        }
        gfx_vline(g, x + cw - P(1), t->hdr.y + P(6), hh - P(12), C_LINE);
        x += cw;
    }
    gfx_noclip(g);

    /* rows */
    gfx_fill(g, t->body.x, t->body.y, t->body.w, t->body.h, C_PANEL);
    gfx_clip(g, t->body.x, t->body.y, t->body.w, t->body.h);
    gfx_font(g, F_UI);
    for (int r = 0; r < rows + 1; r++) {
        int vi = t->scroll + r;
        if (vi >= u->nview) break;
        Proc *p = &u->procs[u->view[vi]];
        int y = t->body.y + r * rh;
        int sel = p->pid == u->sel_pid, hov = t->hover_row == vi;
        uint32_t rowbg = sel ? C_SEL : hov ? C_HOVER : C_PANEL;
        gfx_fill(g, t->body.x, y, t->body.w, rh, rowbg);
        x = area.x - t->hscroll;
        for (int i = 0; i < t->ncols; i++) {
            Col *c = &t->cols[i]; int cw = P(c->w);
            float heat = cell_heat(u, p, c->id);
            if (heat >= 0) {
                uint32_t hc = heat_color(heat);
                gfx_fill(g, x, y, cw, rh, sel ? gfx_lerp(hc, C_SEL, 0.55f) : hov ? gfx_lerp(hc, C_HOVER, 0.35f) : hc);
            }
            char b[256]; cell_text(u, p, c->id, b, sizeof b);
            uint32_t col = C_TEXT;
            if (c->id == CL_STATUS && p->state == 'Z') col = 0xffc42b1c;
            if (c->id == CL_STATUS && (p->state == 'T' || p->state == 't' || p->state == 'D')) col = 0xff9d5d00;
            if (c->id == CL_NAME) {
                int ind = tree ? p->depth * P(16) : 0;
                int cx = x + P(6) + ind;
                if (tree) {
                    if (p->nchild) {
                        int bx = cx + P(2), by = y + rh / 2;
                        uint32_t cc = C_DIM;
                        if (is_collapsed(u, p->pid)) { gfx_line_aa(g, bx, by - P(4), bx + P(4), by, cc); gfx_line_aa(g, bx + P(4), by, bx, by + P(4), cc); }
                        else { gfx_line_aa(g, bx - P(1), by - P(2), bx + P(3), by + P(2), cc); gfx_line_aa(g, bx + P(3), by + P(2), bx + P(7), by - P(2), cc); }
                    }
                    cx += P(14);
                }
                const Icon *ic = icon_for(p, icon_sz);
                icon_draw(g, cx, y + (rh - icon_sz) / 2, ic);
                cx += icon_sz + P(7);
                gfx_text_clip(g, cx, y + (rh - gfx_fonth(g)) / 2, x + cw - cx - P(6), b, col);
            } else if (coldef[c->id].right) {
                gfx_text_rv(g, x + cw - P(6), y, rh, b, col);
            } else gfx_text_clip(g, x + P(6), y + (rh - gfx_fonth(g)) / 2, cw - P(12), b, col);
            x += cw;
        }
        if (sel) gfx_fill(g, t->body.x, y + P(4), P(3), rh - P(8), C_SELB);   /* Win11 selection pill */
    }
    gfx_noclip(g);

    /* horizontal scrollbar */
    if (hbar) {
        Rect hb = R(t->body.x, t->body.y + t->body.h, t->body.w, P(SB_W));
        gfx_fill(g, hb.x, hb.y, hb.w, hb.h, C_PANEL);
        int tw = hb.w * t->body.w / totw; if (tw < P(24)) tw = P(24);
        int tx = hb.x + (int)((long)(hb.w - tw) * t->hscroll / maxh);
        gfx_rrect(g, tx, hb.y + P(3), tw, hb.h - P(6), P(3), 0xffc4c4c4);
    }

    /* vertical scrollbar */
    Rect sb = R(area.x + area.w - P(SB_W), area.y, P(SB_W), area.h);
    gfx_fill(g, sb.x, sb.y, sb.w, sb.h, C_PANEL);
    if (u->nview > rows) {
        int th = sb.h * rows / u->nview; if (th < P(24)) th = P(24);
        int ty = sb.y + (int)((long)(sb.h - th) * t->scroll / maxs);
        gfx_rrect(g, sb.x + P(3), ty, sb.w - P(6), th, P(3), 0xffc4c4c4);
    }
}

static int table_row_at(UI *u, Table *t, int x, int y)
{
    if (!inr(t->body, x, y)) return -1;
    int r = (y - t->body.y) / P(t->row_h) + t->scroll;
    return r < u->nview ? r : -1;
}
static int table_col_at(UI *u, Table *t, int x, int *border)
{
    int cx = t->hdr.x - t->hscroll; *border = -1;
    for (int i = 0; i < t->ncols; i++) {
        int cw = P(t->cols[i].w);
        if (abs(x - (cx + cw)) <= P(3)) { *border = i; return i; }
        if (x >= cx && x < cx + cw) return i;
        cx += cw;
    }
    return -1;
}
static void table_sort_click(UI *u, Table *t, int ci)
{
    int id = t->cols[ci].id;
    if (t->sort_col == id) t->sort_dir = -t->sort_dir;
    else { t->sort_col = id; t->sort_dir = coldef[id].numeric ? -1 : 1; }
    rebuild_view(u);
}
static void ensure_visible(UI *u, Table *t, int vi)
{
    int rows = t->body.h / P(t->row_h); if (rows < 1) rows = 1;
    if (vi < t->scroll) t->scroll = vi;
    if (vi >= t->scroll + rows) t->scroll = vi - rows + 1;
}
static int sel_index(UI *u) { for (int i = 0; i < u->nview; i++) if (u->procs[u->view[i]].pid == u->sel_pid) return i; return -1; }

/* ---- performance tab ------------------------------------------------------------ */
static void kv(UI *u, int x, int y, const char *k, const char *v, int big)
{
    Gfx *g = &u->g;
    gfx_font(g, F_UI); gfx_text(g, x, y, k, C_DIM);
    gfx_font(g, big ? F_BIG : F_UI); gfx_text(g, x, y + P(15), v, C_TEXT); gfx_font(g, F_UI);
}

static void draw_perf(UI *u, Rect a)
{
    Gfx *g = &u->g;
    gfx_fill(g, a.x, a.y, a.w, a.h, C_WINBG);
    int lw = P(236);
    gfx_font(g, F_UI);
    const char *names[4] = { "CPU", "Memory", "Disk", "Network" };
    char sub[4][48];
    snprintf(sub[0], 48, "%.0f%%  %.2f GHz", u->cpu_pct, u->sys.mhz / 1000);
    char b1[24], b2[24]; fmt_bytes(b1, 24, (double)u->sys.mem_used); fmt_bytes(b2, 24, (double)u->sys.mem_total);
    snprintf(sub[1], 48, "%s / %s (%.0f%%)", b1, b2, u->sys.mem_total ? 100.0 * u->sys.mem_used / u->sys.mem_total : 0);
    fmt_rate(b1, 24, u->drd); fmt_rate(b2, 24, u->dwr); snprintf(sub[2], 48, "R: %s  W: %s", b1, b2);
    fmt_rate(b1, 24, u->nrx); fmt_rate(b2, 24, u->ntx); snprintf(sub[3], 48, "R: %s  S: %s", b1, b2);
    uint32_t lc[4] = { C_CPU, C_MEM, C_DISK, C_NET }, fc[4] = { C_CPUF, C_MEMF, C_DISKF, C_NETF }, gc[4] = { C_CPUG, C_MEMG, C_DISKG, C_NETG };
    float dmax = 1; for (int i = 0; i < u->hcount; i++) { float v = hist_at(u->hdrd, u->hpos, u->hcount, i) + hist_at(u->hdwr, u->hpos, u->hcount, i); if (v > dmax) dmax = v; }
    float nmax = 1; for (int i = 0; i < u->hcount; i++) { float v = hist_at(u->hnrx, u->hpos, u->hcount, i); float w = hist_at(u->hntx, u->hpos, u->hcount, i); if (v > nmax) nmax = v; if (w > nmax) nmax = w; }
    float dm = dmax; if (dm < 1024 * 1024) dm = 1024 * 1024;
    float nm = nmax; if (nm < 100 * 1024) nm = 100 * 1024;
    for (int i = 0; i < 4; i++) {
        Rect it = R(a.x + P(8), a.y + P(8) + i * P(74), lw - P(12), P(70));
        u->r_perf[i] = it;
        if (u->perf_page == i) gfx_rrect_b(g, it.x, it.y, it.w, it.h, P(6), C_PANEL, C_BORDER);
        else if (inr(it, u->mx, u->my)) gfx_rrect(g, it.x, it.y, it.w, it.h, P(6), 0xffeaeaea);
        if (u->perf_page == i) gfx_rrect(g, it.x, it.y + P(20), P(3), it.h - P(40), P(2), C_SELB);
        Rect gr = R(it.x + P(12), it.y + P(10), P(70), P(50));
        const float *ha = i == 0 ? u->hcpu : i == 1 ? u->hmem : i == 2 ? u->hdrd : u->hnrx;
        const float *hb = i == 2 ? u->hdwr : i == 3 ? u->hntx : NULL;
        graph(u, gr, ha, hb, i < 2 ? 100 : i == 2 ? dm : nm, lc[i], fc[i], gc[i], 1);
        gfx_font(g, F_BOLD); gfx_text(g, gr.x + gr.w + P(12), it.y + P(16), names[i], C_TEXT);
        gfx_font(g, F_UI); gfx_text_clip(g, gr.x + gr.w + P(12), it.y + P(36), it.w - gr.w - P(30), sub[i], C_DIM);
    }

    /* main pane card */
    Rect card = R(a.x + lw, a.y + P(8), a.w - lw - P(8), a.h - P(16));
    gfx_rrect_b(g, card.x, card.y, card.w, card.h, P(8), C_PANEL, C_BORDER);
    int mx = card.x + P(20), my = card.y + P(14), mw = card.w - P(40);
    char t2[96];
    gfx_font(g, F_BIG); gfx_text(g, mx, my, names[u->perf_page], C_TEXT);
    if (u->perf_page == 0) snprintf(t2, sizeof t2, "%s", u->sys.cpu_model[0] ? u->sys.cpu_model : "Unknown CPU");
    else if (u->perf_page == 1) { fmt_bytes(b1, 24, (double)u->sys.mem_total); snprintf(t2, sizeof t2, "%s", b1); }
    else if (u->perf_page == 2) snprintf(t2, sizeof t2, "All physical disks");
    else snprintf(t2, sizeof t2, "All interfaces (excluding loopback)");
    gfx_font(g, F_UI);
    gfx_text_rv(g, mx + mw, my, P(28), t2, C_DIM);

    Rect r_expand = R(mx + mw - P(150), my + P(34), P(150), P(24));
    u->r_cores = u->perf_page == 0 ? r_expand : R(0, 0, 0, 0);
    if (u->perf_page == 0) button(u, r_expand, u->perf_cores ? "Overall utilization" : "Logical processors", 0, inr(r_expand, u->mx, u->my));

    int gy = my + P(78), gh = card.h * 42 / 100; if (gh < P(120)) gh = P(120);
    int seconds = HIST * u->interval_ms / 1000;
    char secs[32]; snprintf(secs, sizeof secs, "%d seconds", seconds);
    int page = u->perf_page;

    if (page == 0 && u->perf_cores) {
        int n = u->sys.ncpu, cols = 1; while (cols * cols < n) cols++;
        int rowsn = (n + cols - 1) / cols;
        int cw = (mw - P(6) * (cols - 1)) / cols, ch = (gh - P(6) * (rowsn - 1)) / rowsn;
        gfx_text(g, mx, gy - P(18), "% Utilization per logical processor", C_DIM);
        for (int i = 0; i < n; i++) {
            Rect r = R(mx + (i % cols) * (cw + P(6)), gy + (i / cols) * (ch + P(6)), cw, ch);
            graph(u, r, u->hcore[i], NULL, 100, C_CPU, C_CPUF, C_CPUG, 1);
            char l[16]; snprintf(l, sizeof l, "CPU %d", i);
            if (ch > P(24) && cw > P(50)) gfx_text(g, r.x + P(5), r.y + P(3), l, C_DIM);
        }
    } else {
        Rect gr = R(mx, gy, mw, gh);
        const char *lbl = page < 2 ? (page == 0 ? "% Utilization" : "Memory usage") : "Throughput";
        char top[32];
        if (page < 2) snprintf(top, sizeof top, "100%%"); else fmt_rate(top, sizeof top, page == 2 ? dm : nm);
        gfx_text(g, mx, gy - P(18), lbl, C_DIM);
        gfx_text_r(g, mx + mw, gy - P(18), top, C_DIM);
        const float *ha = page == 0 ? u->hcpu : page == 1 ? u->hmem : page == 2 ? u->hdrd : u->hnrx;
        const float *hb = page == 2 ? u->hdwr : page == 3 ? u->hntx : NULL;
        graph(u, gr, ha, hb, page < 2 ? 100 : page == 2 ? dm : nm, lc[page], fc[page], gc[page], 0);
        gfx_text(g, mx, gy + gh + P(5), secs, C_DIM);
        gfx_text_r(g, mx + mw, gy + gh + P(5), "0", C_DIM);
        if (hb) {
            int lx = mx + mw - P(190), ly = gy + gh + P(5);
            gfx_fill(g, lx, ly + P(7), P(16), P(2), lc[page]);
            gfx_text(g, lx + P(22), ly, page == 2 ? "Read" : "Receive", C_DIM);
            for (int i = 0; i < P(16); i += P(5)) gfx_fill(g, lx + P(92) + i, ly + P(7), P(3), P(2), lc[page]);
            gfx_text(g, lx + P(114), ly, page == 2 ? "Write" : "Send", C_DIM);
        }
    }

    /* stats */
    int sy = gy + gh + P(30), colw = P(130);
    char v[64];
    if (page == 1) {
        Rect bar = R(mx, sy, mw, P(20));
        double tot = (double)(u->sys.mem_total ? u->sys.mem_total : 1);
        int wu = (int)(bar.w * u->sys.mem_used / tot), wc = (int)(bar.w * u->sys.mem_cached / tot);
        if (wu + wc > bar.w) wc = bar.w - wu;
        gfx_rrect_b(g, bar.x, bar.y, bar.w, bar.h, P(3), C_PANEL, C_MEM);
        gfx_clip(g, bar.x + P(1), bar.y + P(1), bar.w - P(2), bar.h - P(2));
        gfx_fill(g, bar.x, bar.y, wu, bar.h, C_MEM);
        for (int i = 0; i < wc; i += P(4)) gfx_fill(g, bar.x + wu + i, bar.y, P(2), bar.h, C_MEMG);
        gfx_noclip(g);
        gfx_text(g, mx, sy + P(24), "In use", C_DIM); gfx_text(g, mx + P(70), sy + P(24), "Standby / cached", C_DIM); gfx_text_r(g, mx + mw, sy + P(24), "Free", C_DIM);
        sy += P(46);
    }
    int y = sy, x = mx;
    switch (page) {
    case 0:
        snprintf(v, sizeof v, "%.0f%%", u->cpu_pct); kv(u, x, y, "Utilization", v, 1);
        snprintf(v, sizeof v, "%.2f GHz", u->sys.mhz / 1000); kv(u, x + colw, y, "Speed", v, 1);
        y += P(48);
        snprintf(v, sizeof v, "%d", u->sys.nproc); kv(u, x, y, "Processes", v, 1);
        snprintf(v, sizeof v, "%d", u->sys.nthreads); kv(u, x + colw, y, "Threads", v, 1);
        snprintf(v, sizeof v, "%d", u->sys.nhandles); kv(u, x + 2 * colw, y, "Handles", v, 1);
        y += P(48);
        fmt_time(v, sizeof v, u->sys.uptime); kv(u, x, y, "Up time", v, 1);
        x = mx + mw - P(190); y = sy;
        snprintf(v, sizeof v, "%.2f GHz", (u->sys.base_mhz > 0 ? u->sys.base_mhz : u->sys.mhz) / 1000); kv(u, x, y, "Base speed:", v, 0); y += P(34);
        snprintf(v, sizeof v, "%d", u->sys.sockets); kv(u, x, y, "Sockets:", v, 0); y += P(34);
        snprintf(v, sizeof v, "%d", u->sys.cores); kv(u, x, y, "Cores:", v, 0); y += P(34);
        snprintf(v, sizeof v, "%d", u->sys.ncpu); kv(u, x, y, "Logical processors:", v, 0); y += P(34);
        if (u->sys.load[0] || u->sys.load[1]) { snprintf(v, sizeof v, "%.2f  %.2f  %.2f", u->sys.load[0], u->sys.load[1], u->sys.load[2]); kv(u, x, y, "Load average:", v, 0); y += P(34); }
        break;
    case 1:
        fmt_bytes(v, sizeof v, (double)u->sys.mem_used); kv(u, x, y, "In use", v, 1);
        fmt_bytes(v, sizeof v, (double)u->sys.mem_avail); kv(u, x + colw, y, "Available", v, 1);
        y += P(48);
        { char c1[24], c2[24]; fmt_bytes(c1, 24, (double)u->sys.mem_committed); fmt_bytes(c2, 24, (double)u->sys.mem_commit_limit);
          snprintf(v, sizeof v, "%s / %s", c1, c2); kv(u, x, y, "Committed", v, 1); }
        fmt_bytes(v, sizeof v, (double)u->sys.mem_cached); kv(u, x + colw * 2, y, "Cached", v, 1);
        y += P(48);
        { char c1[24], c2[24]; fmt_bytes(c1, 24, (double)u->sys.swap_used); fmt_bytes(c2, 24, (double)u->sys.swap_total);
          snprintf(v, sizeof v, "%s / %s", c1, c2); kv(u, x, y, "Swap", v, 1); }
        x = mx + mw - P(190); y = sy;
        fmt_bytes(v, sizeof v, (double)u->sys.mem_total); kv(u, x, y, "Total:", v, 0); y += P(34);
        snprintf(v, sizeof v, "%.1f%%", u->sys.mem_total ? 100.0 * u->sys.mem_used / u->sys.mem_total : 0); kv(u, x, y, "Used:", v, 0); y += P(34);
        break;
    case 2:
        fmt_rate(v, sizeof v, u->drd); kv(u, x, y, "Read speed", v, 1);
        fmt_rate(v, sizeof v, u->dwr); kv(u, x + colw * 3 / 2, y, "Write speed", v, 1);
        y += P(48);
        fmt_bytes(v, sizeof v, (double)u->sys.disk_rd); kv(u, x, y, "Total read", v, 1);
        fmt_bytes(v, sizeof v, (double)u->sys.disk_wr); kv(u, x + colw * 3 / 2, y, "Total written", v, 1);
        break;
    case 3:
        fmt_rate(v, sizeof v, u->ntx); kv(u, x, y, "Send", v, 1);
        fmt_rate(v, sizeof v, u->nrx); kv(u, x + colw * 3 / 2, y, "Receive", v, 1);
        y += P(48);
        fmt_bytes(v, sizeof v, (double)u->sys.net_tx); kv(u, x, y, "Total sent", v, 1);
        fmt_bytes(v, sizeof v, (double)u->sys.net_rx); kv(u, x + colw * 3 / 2, y, "Total received", v, 1);
        x = mx + mw - P(190); y = sy;
        kv(u, x, y, "Host name:", u->sys.host, 0); y += P(34);
        break;
    }
}

/* ---- menus ---------------------------------------------------------------------- */
static const char *menu_names[4] = { "File", "Options", "View", "Help" };
#define SPEED_CHECK(ms) (u->paused ? 0 : u->interval_ms == (ms))

static int menu_items(UI *u, int m, MenuItem *out)
{
    int n = 0;
    switch (m) {
    case 0:
        out[n++] = (MenuItem){ "Refresh now", "F5", A_REFRESH, 0, 0 };
        out[n++] = (MenuItem){ "Exit", "Ctrl+Q", A_EXIT, 0, 1 };
        break;
    case 1:
        out[n++] = (MenuItem){ "Show kernel threads", "Ctrl+K", A_KERNEL, u->show_kernel, 0 };
        out[n++] = (MenuItem){ "Only my processes", "Ctrl+U", A_ONLYME, u->only_me, 0 };
        break;
    case 2:
        out[n++] = (MenuItem){ "Processes", "Ctrl+1", A_TAB_PROC, u->tab == 0, 0 };
        out[n++] = (MenuItem){ "Performance", "Ctrl+2", A_TAB_PERF, u->tab == 1, 0 };
        out[n++] = (MenuItem){ "Details", "Ctrl+3", A_TAB_DET, u->tab == 2, 0 };
        out[n++] = (MenuItem){ "Tree view", "Ctrl+T", A_TREE, u->tree, 1 };
        out[n++] = (MenuItem){ "Expand all", "", A_EXPAND_ALL, 0, 0 };
        out[n++] = (MenuItem){ "Collapse all", "", A_COLLAPSE_ALL, 0, 0 };
        out[n++] = (MenuItem){ "Logical processors", "Ctrl+L", A_CORES, u->perf_cores, 1 };
        out[n++] = (MenuItem){ "Speed: High (0.5 s)", "", A_SPEED_HIGH, SPEED_CHECK(500), 1 };
        out[n++] = (MenuItem){ "Speed: Normal (1 s)", "", A_SPEED_NORMAL, SPEED_CHECK(1000), 0 };
        out[n++] = (MenuItem){ "Speed: Low (4 s)", "", A_SPEED_LOW, SPEED_CHECK(4000), 0 };
        out[n++] = (MenuItem){ "Paused", "Space", A_SPEED_PAUSE, u->paused, 0 };
        break;
    case 3:
        out[n++] = (MenuItem){ "About Task Manager", "F1", A_ABOUT, 0, 0 };
        break;
    }
    return n;
}

static int ctx_items(UI *u, MenuItem *out)
{
    int n = 0;
    out[n++] = (MenuItem){ "End task", "Del", A_END, 0, 0 };
    out[n++] = (MenuItem){ "End process tree", "Shift+Del", A_END_TREE, 0, 0 };
    if (u->tab == 0) out[n++] = (MenuItem){ "Go to details", "", A_GOTO_DETAILS, 0, 1 };
    else out[n++] = (MenuItem){ "Go to process", "", A_GOTO_PROC, 0, 1 };
    out[n++] = (MenuItem){ "Properties", "Enter", A_PROPS, 0, 0 };
    return n;
}

static void draw_dropdown(UI *u, Rect *out, int x, int y, MenuItem *items, int n, int hover)
{
    Gfx *g = &u->g;
    gfx_font(g, F_UI);
    int ih = P(28), w = P(230), h = P(8);
    for (int i = 0; i < n; i++) h += ih + (items[i].sep ? P(9) : 0);
    if (x + w > u->w) x = u->w - w; if (y + h > u->h) y = u->h - h;
    *out = R(x, y, w, h);
    for (int i = 4; i >= 1; i--) gfx_rrect_a(g, x - i + 2, y - i + 4, w + 2 * i - 4, h + 2 * i - 4, P(8) + i, C_SHADOW, 10);
    gfx_rrect_b(g, x, y, w, h, P(8), C_MENU, C_BORDER);
    int iy = y + P(4);
    for (int i = 0; i < n; i++) {
        if (items[i].sep) { gfx_hline(g, x + P(8), iy + P(4), w - P(16), C_BORDER); iy += P(9); }
        if (i == hover) gfx_rrect(g, x + P(4), iy, w - P(8), ih, P(4), 0xffe9e9e9);
        if (items[i].check) checkbox_mark(u, x + P(12), iy + (ih - P(10)) / 2, C_TEXT);
        gfx_text_v(g, x + P(30), iy, ih, items[i].label, C_TEXT);
        gfx_text_rv(g, x + w - P(12), iy, ih, items[i].key, C_DIM);
        iy += ih;
    }
}
static int dropdown_hit(UI *u, Rect dd, MenuItem *items, int n, int x, int y)
{
    if (!inr(dd, x, y)) return -1;
    int ih = P(28), iy = dd.y + P(4);
    for (int i = 0; i < n; i++) {
        if (items[i].sep) iy += P(9);
        if (y >= iy && y < iy + ih) return i;
        iy += ih;
    }
    return -1;
}

/* ---- dialogs -------------------------------------------------------------------- */
static void draw_dialog(UI *u)
{
    Gfx *g = &u->g;
    if (u->dlg == DLG_NONE) return;
    gfx_blend(g, 0, 0, u->w, u->h, 0xff000000, 90);
    int w = P(440), h = P(190);
    if (u->dlg == DLG_PROPS) { w = P(560); h = P(330); }
    if (u->dlg == DLG_ABOUT) { w = P(460); h = P(250); }
    int x = (u->w - w) / 2, y = (u->h - h) / 2;
    for (int i = 8; i >= 1; i--) gfx_rrect_a(g, x - i + 4, y - i + 8, w + 2 * i - 8, h + 2 * i - 8, P(10) + i, C_SHADOW, 9);
    gfx_rrect_b(g, x, y, w, h, P(10), C_PANEL, C_BORDER);
    /* footer strip (rounded at the bottom to follow the dialog corners) */
    gfx_clip(g, x + P(1), y + h - P(60), w - P(2), P(59));
    gfx_rrect(g, x + P(1), y + h - P(120), w - P(2), P(119), P(9), C_WINBG);
    gfx_noclip(g);
    gfx_hline(g, x + P(1), y + h - P(60), w - P(2), C_BORDER);

    const char *title = u->dlg == DLG_END ? "End task" : u->dlg == DLG_END_TREE ? "End process tree" :
                        u->dlg == DLG_PROPS ? "Properties" : u->dlg == DLG_ABOUT ? "About Task Manager" : "Something went wrong";
    gfx_font(g, F_BIG); gfx_text(g, x + P(24), y + P(18), title, C_TEXT); gfx_font(g, F_UI);
    int ty = y + P(58);
    Proc *p = u->dlg_pid > 0 ? find_proc(u, u->dlg_pid) : NULL;
    char line[256];
    int icon_sz = P(32);
    if (u->dlg == DLG_END || u->dlg == DLG_END_TREE) {
        if (p) icon_draw(g, x + P(24), ty, icon_for(p, icon_sz));
        int tx = x + P(24) + icon_sz + P(14);
        gfx_font(g, F_BOLD);
        snprintf(line, sizeof line, "%s  (PID %d)", u->dlg_name, u->dlg_pid);
        gfx_text_clip(g, tx, ty, x + w - tx - P(24), line, C_TEXT); ty += P(20);
        gfx_font(g, F_UI);
        gfx_text(g, tx, ty, u->dlg == DLG_END_TREE ? "This process and all of its child processes will be ended." : "Any unsaved data in this process will be lost.", C_DIM); ty += P(18);
        if (p) { snprintf(line, sizeof line, "%s   %s", p->user, p->cmd); gfx_text_clip(g, tx, ty, x + w - tx - P(24), line, C_DIM); }
        u->r_dlg_btn[0] = R(x + w - P(236), y + h - P(46), P(110), P(30));
        u->r_dlg_btn[1] = R(x + w - P(118), y + h - P(46), P(96), P(30));
        button(u, u->r_dlg_btn[0], "End process", 1, inr(u->r_dlg_btn[0], u->mx, u->my));
        button(u, u->r_dlg_btn[1], "Cancel", 0, inr(u->r_dlg_btn[1], u->mx, u->my));
    } else if (u->dlg == DLG_PROPS) {
        if (!p) { gfx_text(g, x + P(24), ty, "The process has exited.", C_TEXT); }
        else {
            icon_draw(g, x + P(24), ty - P(2), icon_for(p, icon_sz));
            gfx_font(g, F_BOLD); gfx_text(g, x + P(24) + icon_sz + P(12), ty, p->name, C_TEXT);
            gfx_font(g, F_UI); gfx_text_clip(g, x + P(24) + icon_sz + P(12), ty + P(17), w - P(48) - icon_sz - P(12), p->cmd, C_DIM);
            ty += P(46);
            gfx_hline(g, x + P(24), ty - P(8), w - P(48), C_LINE);
            const char *keys[] = { "PID", "Parent PID", "User", "Status", "CPU", "CPU time", "Threads", "Memory", "Virtual", "Handles", "Prio / nice", "Disk R / W" };
            char vals[12][128]; char a[32], b[32];
            snprintf(vals[0], 128, "%d", p->pid); snprintf(vals[1], 128, "%d", p->ppid);
            snprintf(vals[2], 128, "%s", p->user); snprintf(vals[3], 128, "%s (%c)", state_name(p->state)[0] ? state_name(p->state) : "Sleeping", p->state);
            snprintf(vals[4], 128, "%.1f%%", p->cpu); fmt_time(vals[5], 128, p->cpu_time / 1000000000ull);
            snprintf(vals[6], 128, "%d", p->threads);
            fmt_bytes(vals[7], 128, (double)p->rss); fmt_bytes(vals[8], 128, (double)p->vsz);
            snprintf(vals[9], 128, "%d", p->handles); snprintf(vals[10], 128, "%d / %d", p->prio, p->nice);
            fmt_bytes(a, 32, (double)p->rd); fmt_bytes(b, 32, (double)p->wr); snprintf(vals[11], 128, "%s / %s", a, b);
            int colw = (w - P(48)) / 2;
            for (int i = 0; i < 12; i++) {
                int col = i / 6, yy = ty + (i % 6) * P(22), xx = x + P(24) + col * colw;
                gfx_text(g, xx, yy, keys[i], C_DIM);
                gfx_text_clip(g, xx + P(92), yy, colw - P(100), vals[i], C_TEXT);
            }
        }
        u->r_dlg_btn[0] = R(x + w - P(236), y + h - P(46), P(110), P(30));
        u->r_dlg_btn[1] = R(x + w - P(118), y + h - P(46), P(96), P(30));
        button(u, u->r_dlg_btn[0], "End task", 0, inr(u->r_dlg_btn[0], u->mx, u->my));
        button(u, u->r_dlg_btn[1], "Close", 1, inr(u->r_dlg_btn[1], u->mx, u->my));
    } else if (u->dlg == DLG_ABOUT) {
        Proc self; memset(&self, 0, sizeof self); snprintf(self.name, sizeof self.name, "Task Manager");
        Icon logo = { icon_sz, NULL, IC_SYSTEM }; icon_draw(g, x + P(24), ty, &logo);
        int tx = x + P(24) + icon_sz + P(14);
        gfx_font(g, F_BOLD); gfx_text(g, tx, ty, "Task Manager " TM_VERSION, C_TEXT); gfx_font(g, F_UI);
        gfx_text(g, tx, ty + P(18), "A compact, dependency-free task manager.", C_DIM); ty += P(46);
        gfx_text(g, x + P(24), ty, "Software-rendered UI, native OS APIs, one tiny executable.", C_TEXT); ty += P(20);
        snprintf(line, sizeof line, "%s  -  %s", u->sys.os, u->sys.host); gfx_text_clip(g, x + P(24), ty, w - P(48), line, C_DIM); ty += P(24);
        gfx_text(g, x + P(24), ty, "Tab: switch tab   Type: search   Del: end task   Enter: properties   Space: pause", C_DIM);
        u->r_dlg_btn[0] = R(0, 0, 0, 0);
        u->r_dlg_btn[1] = R(x + w - P(118), y + h - P(46), P(96), P(30));
        button(u, u->r_dlg_btn[1], "OK", 1, inr(u->r_dlg_btn[1], u->mx, u->my));
    } else {
        gfx_text_clip(g, x + P(24), ty, w - P(48), u->dlg_msg, C_TEXT);
        gfx_text(g, x + P(24), ty + P(20), "You may need elevated privileges (root / Administrator).", C_DIM);
        u->r_dlg_btn[0] = R(0, 0, 0, 0);
        u->r_dlg_btn[1] = R(x + w - P(118), y + h - P(46), P(96), P(30));
        button(u, u->r_dlg_btn[1], "OK", 1, inr(u->r_dlg_btn[1], u->mx, u->my));
    }
}

/* ---- main draw ------------------------------------------------------------------ */
void ui_draw(UI *u)
{
    Gfx *g = &u->g;
    if (!u->px) return;
    gfx_fill(g, 0, 0, u->w, u->h, C_WINBG);
    gfx_font(g, F_UI);

    /* menu bar */
    int x = P(6);
    for (int i = 0; i < 4; i++) {
        int w = gfx_textw(g, menu_names[i]) + P(20);
        u->r_menu[i] = R(x, P(2), w, P(MENU_H) - P(4));
        if (u->menu_open == i) gfx_rrect(g, x, P(2), w, P(MENU_H) - P(4), P(4), 0xffdedede);
        else if (inr(u->r_menu[i], u->mx, u->my) && u->dlg == DLG_NONE) gfx_rrect(g, x, P(2), w, P(MENU_H) - P(4), P(4), 0xffe8e8e8);
        gfx_text_mid(g, x, P(2), w, P(MENU_H) - P(4), menu_names[i], C_TEXT);
        x += w;
    }
    if (u->paused) { gfx_rrect(g, u->w - P(70), P(4), P(62), P(MENU_H) - P(8), P(4), 0xfffde7e9); gfx_text_mid(g, u->w - P(70), P(4), P(62), P(MENU_H) - P(8), "Paused", 0xffc42b1c); }
    else { char sp[32]; snprintf(sp, sizeof sp, "Every %.1f s", u->interval_ms / 1000.0); gfx_text_rv(g, u->w - P(10), 0, P(MENU_H), sp, C_DIM); }

    /* tabs (Win11 pivot style) */
    const char *tabs[3] = { "Processes", "Performance", "Details" };
    int ty = P(MENU_H);
    x = P(12);
    for (int i = 0; i < 3; i++) {
        int w = gfx_textw(g, tabs[i]) + P(28);
        u->r_tabs[i] = R(x, ty, w, P(TAB_H));
        if (u->tab != i && inr(u->r_tabs[i], u->mx, u->my) && u->dlg == DLG_NONE) gfx_rrect(g, x, ty + P(4), w, P(TAB_H) - P(8), P(4), 0xffe8e8e8);
        gfx_font(g, u->tab == i ? F_BOLD : F_UI);
        gfx_text_mid(g, x, ty, w, P(TAB_H), tabs[i], u->tab == i ? C_TEXT : C_DIM);
        if (u->tab == i) gfx_rrect(g, x + P(10), ty + P(TAB_H) - P(4), w - P(20), P(3), P(2), C_SELB);
        x += w;
    }
    gfx_font(g, F_UI);
    int cy = ty + P(TAB_H);
    Rect content = R(0, cy, u->w, u->h - cy - P(STATUS_H));

    if (u->tab == 1) {
        u->r_end = u->r_search = u->r_tree = R(0, 0, 0, 0);
        draw_perf(u, content);
    } else {
        /* toolbar */
        int tby = content.y;
        gfx_fill(g, 0, tby, u->w, P(TOOL_H), C_WINBG);
        u->r_search = R(P(12), tby + P(6), P(260), P(28));
        int sfocus = u->nsearch > 0;
        gfx_rrect_b(g, u->r_search.x, u->r_search.y, u->r_search.w, u->r_search.h, P(5), 0xffffffff, sfocus ? C_SELB : C_BTNB);
        if (sfocus) gfx_rrect(g, u->r_search.x + P(6), u->r_search.y + u->r_search.h - P(2), u->r_search.w - P(12), P(2), P(1), C_SELB);
        else gfx_hline(g, u->r_search.x + P(4), u->r_search.y + u->r_search.h - P(1), u->r_search.w - P(8), 0xffbdbdbd);
        /* magnifier glyph */
        { int gx = u->r_search.x + P(10), gy = u->r_search.y + P(8);
          for (int k = 0; k < 3; k++) gfx_rrect_a(g, gx + k, gy + k, P(9) - 2 * k, P(9) - 2 * k, P(4), C_DIM, k == 1 ? 255 : 0);
          gfx_rrect(g, gx + 2, gy + 2, P(9) - 4, P(9) - 4, P(3), sfocus ? 0xffffffff : 0xffffffff);
          gfx_line_aa(g, gx + P(8), gy + P(8), gx + P(12), gy + P(12), C_DIM); }
        if (u->nsearch) {
            char sb[80]; snprintf(sb, sizeof sb, "%s", u->search);
            gfx_text_clip(g, u->r_search.x + P(28), u->r_search.y + (u->r_search.h - gfx_fonth(g)) / 2, u->r_search.w - P(52), sb, C_TEXT);
            int cx = u->r_search.x + P(28) + gfx_textw(g, sb) + P(1); if (cx < u->r_search.x + u->r_search.w - P(24)) gfx_fill(g, cx, u->r_search.y + P(7), P(1), u->r_search.h - P(14), C_TEXT);
            int xx = u->r_search.x + u->r_search.w - P(18), xy = u->r_search.y + u->r_search.h / 2;
            gfx_line_aa(g, xx - P(3), xy - P(3), xx + P(3), xy + P(3), C_DIM); gfx_line_aa(g, xx - P(3), xy + P(3), xx + P(3), xy - P(3), C_DIM);
        } else gfx_text_v(g, u->r_search.x + P(28), u->r_search.y, u->r_search.h, "Search by name, user or PID", 0xff8a8a8a);
        char cnt[64]; snprintf(cnt, sizeof cnt, "%d of %d processes", u->nview, u->nproc);
        gfx_text_v(g, u->r_search.x + u->r_search.w + P(14), u->r_search.y, u->r_search.h, cnt, C_DIM);

        u->r_end = R(u->w - P(112), tby + P(6), P(100), P(28));
        Proc *sp = find_proc(u, u->sel_pid);
        if (sp) button(u, u->r_end, "End task", 1, inr(u->r_end, u->mx, u->my));
        else { gfx_rrect_b(g, u->r_end.x, u->r_end.y, u->r_end.w, u->r_end.h, P(4), 0xffededed, 0xffe2e2e2); gfx_text_mid(g, u->r_end.x, u->r_end.y, u->r_end.w, u->r_end.h, "End task", 0xffa6a6a6); }
        if (u->tab == 0) {
            u->r_tree = R(u->r_end.x - P(96), tby + P(6), P(86), P(28));
            button(u, u->r_tree, u->tree ? "Tree: on" : "Tree: off", 0, inr(u->r_tree, u->mx, u->my));
        } else u->r_tree = R(0, 0, 0, 0);

        Table *t = cur_table(u);
        Rect ta = R(P(8), tby + P(TOOL_H), u->w - P(16), content.h - P(TOOL_H) - P(6));
        gfx_rrect_b(g, ta.x - P(1), ta.y - P(1), ta.w + P(2), ta.h + P(2), P(6), C_PANEL, C_BORDER);
        gfx_clip(g, ta.x, ta.y, ta.w, ta.h);
        draw_table(u, t, ta);
        gfx_noclip(g);
    }

    /* status bar */
    int sy = u->h - P(STATUS_H);
    gfx_fill(g, 0, sy, u->w, P(STATUS_H), C_WINBG);
    char st[256], up[32]; fmt_time(up, sizeof up, u->sys.uptime);
    if (u->flash_until) snprintf(st, sizeof st, "%s", u->flash);
    else snprintf(st, sizeof st, "Processes: %d     Threads: %d     CPU: %.0f%%     Memory: %.0f%%     Up time: %s",
             u->sys.nproc, u->sys.nthreads, u->cpu_pct, u->sys.mem_total ? 100.0 * u->sys.mem_used / u->sys.mem_total : 0, up);
    gfx_text_v(g, P(12), sy, P(STATUS_H), st, u->flash_until ? C_ACCENT : C_DIM);
    gfx_text_rv(g, u->w - P(12), sy, P(STATUS_H), u->sys.os, C_DIM);

    /* overlays */
    if (u->menu_open >= 0) {
        static MenuItem items[16]; int n = menu_items(u, u->menu_open, items);
        draw_dropdown(u, &u->r_dd, u->r_menu[u->menu_open].x, P(MENU_H), items, n, u->menu_hover);
    }
    if (u->ctx_open) {
        static MenuItem items[8]; int n = ctx_items(u, items);
        u->ctx_n = n;
        draw_dropdown(u, &u->r_ctx, u->ctx_x, u->ctx_y, items, n, u->ctx_hover);
    }
    draw_dialog(u);
}

/* ---- actions ----------------------------------------------------------------------- */
static void collect_tree(UI *u, int pid, int *out, int *n, int max)
{
    if (*n >= max) return;
    for (int i = 0; i < u->nproc && *n < max; i++)
        if (u->procs[i].ppid == pid && u->procs[i].pid != pid) { collect_tree(u, u->procs[i].pid, out, n, max); }
    if (*n < max) out[(*n)++] = pid;
}

static void do_kill(UI *u, int pid, int tree)
{
    int list[1024], n = 0, fail = 0, ok = 0;
    if (tree) collect_tree(u, pid, list, &n, 1024); else list[n++] = pid;
    for (int i = 0; i < n; i++) { if (sys_kill(list[i]) == 0) ok++; else fail++; }
    char msg[128];
    if (fail) { snprintf(u->dlg_msg, sizeof u->dlg_msg, "Could not end %d of %d process(es) - access denied.", fail, n); u->dlg = DLG_ERROR; }
    else { snprintf(msg, sizeof msg, "Ended %d process(es).", ok); flash(u, msg); u->dlg = DLG_NONE; }
    u->last_sample = sys_now_ns() - (uint64_t)u->interval_ms * 1000000ull + 400000000ull;   /* resample soon */
}

static void open_dialog(UI *u, int kind, int pid)
{
    Proc *p = find_proc(u, pid);
    if (!p) return;
    u->dlg = kind; u->dlg_pid = pid; snprintf(u->dlg_name, sizeof u->dlg_name, "%s", p->name);
}

static int do_action(UI *u, int a)
{
    switch (a) {
    case A_EXIT: return -1;
    case A_REFRESH: u->last_sample = 0; break;
    case A_SPEED_HIGH: u->interval_ms = 500; u->paused = 0; break;
    case A_SPEED_NORMAL: u->interval_ms = 1000; u->paused = 0; break;
    case A_SPEED_LOW: u->interval_ms = 4000; u->paused = 0; break;
    case A_SPEED_PAUSE: u->paused = !u->paused; break;
    case A_TREE: u->tree = !u->tree; rebuild_view(u); break;
    case A_KERNEL: u->show_kernel = !u->show_kernel; rebuild_view(u); break;
    case A_ONLYME: u->only_me = !u->only_me; rebuild_view(u); break;
    case A_CORES: u->perf_cores = !u->perf_cores; break;
    case A_ABOUT: u->dlg = DLG_ABOUT; break;
    case A_END: open_dialog(u, DLG_END, u->ctx_pid > 0 ? u->ctx_pid : u->sel_pid); break;
    case A_END_TREE: open_dialog(u, DLG_END_TREE, u->ctx_pid > 0 ? u->ctx_pid : u->sel_pid); break;
    case A_PROPS: open_dialog(u, DLG_PROPS, u->ctx_pid > 0 ? u->ctx_pid : u->sel_pid); break;
    case A_GOTO_DETAILS: u->sel_pid = u->ctx_pid; u->tab = 2; rebuild_view(u); { int i = sel_index(u); if (i >= 0) ensure_visible(u, &u->td, i); } break;
    case A_GOTO_PROC: u->sel_pid = u->ctx_pid; u->tab = 0; rebuild_view(u); { int i = sel_index(u); if (i >= 0) ensure_visible(u, &u->tp, i); } break;
    case A_EXPAND_ALL: u->ncollapsed = 0; rebuild_view(u); break;
    case A_COLLAPSE_ALL:
        u->ncollapsed = 0;
        for (int i = 0; i < u->nproc && u->ncollapsed < 256; i++) if (u->procs[i].nchild) u->collapsed[u->ncollapsed++] = u->procs[i].pid;
        rebuild_view(u); break;
    case A_TAB_PROC: u->tab = 0; rebuild_view(u); break;
    case A_TAB_PERF: u->tab = 1; break;
    case A_TAB_DET: u->tab = 2; rebuild_view(u); break;
    }
    return 1;
}

/* ---- events ----------------------------------------------------------------------- */
static int move_sel(UI *u, int delta, int absolute)
{
    Table *t = cur_table(u); if (!t || !u->nview) return 0;
    int i = sel_index(u);
    int rows = t->body.h / P(t->row_h); if (rows < 1) rows = 1;
    if (absolute == 1) i = 0; else if (absolute == 2) i = u->nview - 1;
    else if (i < 0) i = delta > 0 ? 0 : u->nview - 1;
    else i += delta * (delta == 100 || delta == -100 ? 0 : 1) + (delta == 100 ? rows : delta == -100 ? -rows : 0);
    if (i < 0) i = 0; if (i >= u->nview) i = u->nview - 1;
    u->sel_pid = u->procs[u->view[i]].pid;
    ensure_visible(u, t, i);
    return 1;
}

static int event_dialog(UI *u, const Event *e)
{
    if (e->type == EV_MOUSE_MOVE) return 1;
    if (e->type == EV_KEY) {
        if (e->key == K_ESC) { u->dlg = DLG_NONE; return 1; }
        if (e->key == K_ENTER) {
            if (u->dlg == DLG_END) do_kill(u, u->dlg_pid, 0);
            else if (u->dlg == DLG_END_TREE) do_kill(u, u->dlg_pid, 1);
            else u->dlg = DLG_NONE;
            return 1;
        }
        return 0;
    }
    if (e->type == EV_MOUSE_DOWN && e->button == 1) {
        if (inr(u->r_dlg_btn[1], e->x, e->y)) { u->dlg = DLG_NONE; return 1; }
        if (inr(u->r_dlg_btn[0], e->x, e->y)) {
            if (u->dlg == DLG_END) do_kill(u, u->dlg_pid, 0);
            else if (u->dlg == DLG_END_TREE) do_kill(u, u->dlg_pid, 1);
            else if (u->dlg == DLG_PROPS) { u->dlg = DLG_END; }
            return 1;
        }
    }
    return 0;
}

int ui_event(UI *u, const Event *e)
{
    if (e->type == EV_QUIT) return -1;
    if (e->type == EV_RESIZE) { ui_resize(u, e->w, e->h); return 1; }
    if (e->type == EV_PAINT) return 1;
    if (e->type == EV_MOUSE_MOVE) {
        u->mx = e->x; u->my = e->y;
        if (u->drag_tab) {
            int nw = u->drag_w0 + (e->x - u->drag_x0) / u->s;
            if (nw < 30) nw = 30; if (nw > 800) nw = 800;
            u->drag_tab->cols[u->drag_col].w = nw; return 1;
        }
        if (u->sb_tab) {
            Table *t = u->sb_tab; int rows = t->body.h / P(t->row_h); if (rows < 1) rows = 1;
            int maxs = u->nview - rows; if (maxs < 1) return 1;
            int track = t->body.h + t->hdr.h; int th = track * rows / u->nview; if (th < P(20)) th = P(20);
            t->scroll = u->sb_scroll0 + (int)((long)(e->y - u->sb_y0) * maxs / (track - th > 0 ? track - th : 1));
            if (t->scroll < 0) t->scroll = 0; if (t->scroll > maxs) t->scroll = maxs;
            return 1;
        }
        if (u->menu_open >= 0) {
            static MenuItem items[16]; int n = menu_items(u, u->menu_open, items);
            u->menu_hover = dropdown_hit(u, u->r_dd, items, n, e->x, e->y);
            for (int i = 0; i < 4; i++) if (inr(u->r_menu[i], e->x, e->y) && i != u->menu_open) { u->menu_open = i; u->menu_hover = -1; }
            return 1;
        }
        if (u->ctx_open) { static MenuItem items[8]; int n = ctx_items(u, items); u->ctx_hover = dropdown_hit(u, u->r_ctx, items, n, e->x, e->y); return 1; }
        Table *t = cur_table(u);
        if (t) { int r = table_row_at(u, t, e->x, e->y); if (r != t->hover_row) { t->hover_row = r; return 1; } }
        return 1;   /* hover highlights on buttons/tabs */
    }
    if (e->type == EV_MOUSE_UP) { u->drag_tab = NULL; u->sb_tab = NULL; return 0; }

    if (u->dlg != DLG_NONE) return event_dialog(u, e);

    /* menus */
    if (u->menu_open >= 0) {
        if (e->type == EV_MOUSE_DOWN) {
            static MenuItem items[16]; int n = menu_items(u, u->menu_open, items);
            int hit = dropdown_hit(u, u->r_dd, items, n, e->x, e->y);
            u->menu_open = -1;
            if (hit >= 0) return do_action(u, items[hit].id);
            for (int i = 0; i < 4; i++) if (inr(u->r_menu[i], e->x, e->y)) return 1;
            return 1;   /* click outside closes */
        }
        if (e->type == EV_KEY) {
            static MenuItem items[16]; int n = menu_items(u, u->menu_open, items);
            if (e->key == K_ESC) u->menu_open = -1;
            else if (e->key == K_DOWN) u->menu_hover = (u->menu_hover + 1) % n;
            else if (e->key == K_UP) u->menu_hover = (u->menu_hover - 1 + n) % n;
            else if (e->key == K_LEFT) { u->menu_open = (u->menu_open + 3) % 4; u->menu_hover = -1; }
            else if (e->key == K_RIGHT) { u->menu_open = (u->menu_open + 1) % 4; u->menu_hover = -1; }
            else if (e->key == K_ENTER && u->menu_hover >= 0) { int id = items[u->menu_hover].id; u->menu_open = -1; return do_action(u, id); }
            return 1;
        }
        return 0;
    }
    if (u->ctx_open) {
        if (e->type == EV_MOUSE_DOWN) {
            static MenuItem items[8]; int n = ctx_items(u, items);
            int hit = dropdown_hit(u, u->r_ctx, items, n, e->x, e->y);
            u->ctx_open = 0;
            int r = hit >= 0 ? do_action(u, items[hit].id) : 1;
            u->ctx_pid = -1;
            return r;
        }
        if (e->type == EV_KEY) {
            static MenuItem items[8]; int n = ctx_items(u, items);
            if (e->key == K_ESC) { u->ctx_open = 0; u->ctx_pid = -1; }
            else if (e->key == K_DOWN) u->ctx_hover = (u->ctx_hover + 1) % n;
            else if (e->key == K_UP) u->ctx_hover = (u->ctx_hover - 1 + n) % n;
            else if (e->key == K_ENTER && u->ctx_hover >= 0) { u->ctx_open = 0; int r = do_action(u, items[u->ctx_hover].id); u->ctx_pid = -1; return r; }
            return 1;
        }
        return 0;
    }

    Table *t = cur_table(u);

    if (e->type == EV_WHEEL) {
        if (t && (inr(t->body, e->x, e->y) || inr(t->hdr, e->x, e->y) || e->x >= t->body.x + t->body.w || e->y >= t->body.y + t->body.h)) {
            if ((e->mods & KM_SHIFT) || e->y >= t->body.y + t->body.h) t->hscroll -= e->delta * P(40); else t->scroll -= e->delta * 3;
            return 1;
        }
        if (u->tab == 1 && e->x < u->r_perf[0].x + u->r_perf[0].w) { u->perf_page = (u->perf_page - e->delta + 4) % 4; return 1; }
        return 0;
    }

    if (e->type == EV_MOUSE_DOWN) {
        u->mx = e->x; u->my = e->y;
        for (int i = 0; i < 4; i++) if (inr(u->r_menu[i], e->x, e->y)) { u->menu_open = i; u->menu_hover = -1; return 1; }
        for (int i = 0; i < 3; i++) if (inr(u->r_tabs[i], e->x, e->y)) { u->tab = i; rebuild_view(u); return 1; }
        if (u->tab == 1) {
            for (int i = 0; i < 4; i++) if (inr(u->r_perf[i], e->x, e->y)) { u->perf_page = i; return 1; }
            if (inr(u->r_cores, e->x, e->y)) { u->perf_cores = !u->perf_cores; return 1; }
            return 0;
        }
        if (inr(u->r_end, e->x, e->y) && e->button == 1) { if (find_proc(u, u->sel_pid)) open_dialog(u, DLG_END, u->sel_pid); return 1; }
        if (inr(u->r_tree, e->x, e->y) && e->button == 1) return do_action(u, A_TREE);
        if (inr(u->r_search, e->x, e->y) && e->button == 1) {
            if (u->nsearch && e->x > u->r_search.x + u->r_search.w - P(20)) { u->nsearch = 0; u->search[0] = 0; rebuild_view(u); }
            return 1;
        }
        if (!t) return 0;
        if (inr(t->hdr, e->x, e->y) && e->button == 1) {
            int border, ci = table_col_at(u, t, e->x, &border);
            if (border >= 0) { u->drag_tab = t; u->drag_col = border; u->drag_x0 = e->x; u->drag_w0 = t->cols[border].w; return 1; }
            if (ci >= 0) table_sort_click(u, t, ci);
            return 1;
        }
        /* scrollbar */
        Rect sb = R(t->body.x + t->body.w, t->hdr.y, P(SB_W), t->hdr.h + t->body.h);
        if (inr(sb, e->x, e->y) && e->button == 1) {
            int rows = t->body.h / P(t->row_h); if (rows < 1) rows = 1;
            int maxs = u->nview - rows; if (maxs <= 0) return 1;
            int th = sb.h * rows / u->nview; if (th < P(20)) th = P(20);
            int ty = sb.y + (int)((long)(sb.h - th) * t->scroll / maxs);
            if (e->y >= ty && e->y < ty + th) { u->sb_tab = t; u->sb_y0 = e->y; u->sb_scroll0 = t->scroll; }
            else t->scroll += e->y < ty ? -rows : rows;
            return 1;
        }
        int r = table_row_at(u, t, e->x, e->y);
        if (r >= 0) {
            Proc *p = &u->procs[u->view[r]];
            int was = u->sel_pid == p->pid;
            u->sel_pid = p->pid;
            if (e->button == 3) { u->ctx_open = 1; u->ctx_x = e->x; u->ctx_y = e->y; u->ctx_pid = p->pid; u->ctx_hover = -1; return 1; }
            if (e->button == 1 && u->tree && t == &u->tp && !u->nsearch && p->nchild) {
                int bx = t->body.x - t->hscroll + P(6) + p->depth * P(16);
                if (e->x >= bx - P(2) && e->x <= bx + P(14)) { toggle_collapsed(u, p->pid); rebuild_view(u); return 1; }
            }
            if (e->button == 1 && was) {
                /* double click emulation: second click on same row within 400ms opens properties */
                static uint64_t last; static int lastpid;
                uint64_t now = sys_now_ns();
                if (lastpid == p->pid && now - last < 400000000ull) { open_dialog(u, DLG_PROPS, p->pid); last = 0; return 1; }
                last = now; lastpid = p->pid;
            } else { static uint64_t last2; (void)last2; }
            return 1;
        }
        if (inr(t->body, e->x, e->y)) { u->sel_pid = -1; return 1; }
        return 0;
    }

    if (e->type == EV_CHAR) {
        int c = e->ch;
        if (e->mods & KM_CTRL) {
            switch (c) {
            case 'q': return -1;
            case 't': return do_action(u, A_TREE);
            case 'k': return do_action(u, A_KERNEL);
            case 'u': return do_action(u, A_ONLYME);
            case 'l': return do_action(u, A_CORES);
            case 'f': return 1;
            case '1': return do_action(u, A_TAB_PROC);
            case '2': return do_action(u, A_TAB_PERF);
            case '3': return do_action(u, A_TAB_DET);
            case 'e': return do_action(u, A_EXPAND_ALL);
            case 'w': return do_action(u, A_COLLAPSE_ALL);
            }
            return 0;
        }
        if (c == ' ' && !u->nsearch) return do_action(u, A_SPEED_PAUSE);
        if (u->tab == 1) return 0;
        if (c >= 32 && c < 127 && u->nsearch < (int)sizeof u->search - 1) {
            u->search[u->nsearch++] = (char)c; u->search[u->nsearch] = 0;
            if (t) t->scroll = 0;
            rebuild_view(u); return 1;
        }
        return 0;
    }

    if (e->type == EV_KEY) {
        switch (e->key) {
        case K_TAB:
            u->tab = (u->tab + ((e->mods & KM_SHIFT) ? 2 : 1)) % 3; rebuild_view(u); return 1;
        case K_F5: u->last_sample = 0; return 1;
        case K_F1: u->dlg = DLG_ABOUT; return 1;
        case K_F10: u->menu_open = 0; u->menu_hover = -1; return 1;
        case K_ESC:
            if (u->nsearch) { u->nsearch = 0; u->search[0] = 0; rebuild_view(u); return 1; }
            u->sel_pid = -1; return 1;
        case K_BS:
            if (u->nsearch) { u->search[--u->nsearch] = 0; rebuild_view(u); return 1; }
            return 0;
        }
        if (u->tab == 1) {
            if (e->key == K_DOWN) { u->perf_page = (u->perf_page + 1) % 4; return 1; }
            if (e->key == K_UP) { u->perf_page = (u->perf_page + 3) % 4; return 1; }
            return 0;
        }
        switch (e->key) {
        case K_DOWN: return move_sel(u, 1, 0);
        case K_UP: return move_sel(u, -1, 0);
        case K_PGDN: return move_sel(u, 100, 0);
        case K_PGUP: return move_sel(u, -100, 0);
        case K_HOME: return move_sel(u, 0, 1);
        case K_END: return move_sel(u, 0, 2);
        case K_DEL: if (find_proc(u, u->sel_pid)) open_dialog(u, (e->mods & KM_SHIFT) ? DLG_END_TREE : DLG_END, u->sel_pid); return 1;
        case K_ENTER: if (find_proc(u, u->sel_pid)) open_dialog(u, DLG_PROPS, u->sel_pid); return 1;
        case K_LEFT: case K_RIGHT: {
            Proc *p = find_proc(u, u->sel_pid);
            if (p && u->tree && u->tab == 0 && p->nchild) {
                int col = is_collapsed(u, p->pid);
                if ((e->key == K_LEFT && !col) || (e->key == K_RIGHT && col)) { toggle_collapsed(u, p->pid); rebuild_view(u); }
                return 1;
            }
            if (p && e->key == K_LEFT && u->tree && u->tab == 0 && p->depth > 0) { u->sel_pid = p->ppid; int i = sel_index(u); if (i >= 0) ensure_visible(u, t, i); return 1; }
            if (t) { t->hscroll += e->key == K_RIGHT ? P(60) : -P(60); return 1; }
            return 0;
        }
        }
    }
    return 0;
}

/* ---- text dump (for --dump and tests) --------------------------------------------- */
void ui_dump(UI *u)
{
    char a[32], b[32], up[32];
    fmt_bytes(a, 32, (double)u->sys.mem_used); fmt_bytes(b, 32, (double)u->sys.mem_total); fmt_time(up, 32, u->sys.uptime);
    printf("Task Manager %s - %s on %s\n", TM_VERSION, u->sys.os, u->sys.host);
    printf("CPU: %s  %d logical / %d cores  %.0f%%  %.2f GHz\n", u->sys.cpu_model, u->sys.ncpu, u->sys.cores, u->cpu_pct, u->sys.mhz / 1000);
    printf("Memory: %s / %s (%.0f%%)  cached ", a, b, u->sys.mem_total ? 100.0 * u->sys.mem_used / u->sys.mem_total : 0);
    fmt_bytes(a, 32, (double)u->sys.mem_cached); printf("%s\n", a);
    fmt_rate(a, 32, u->drd); fmt_rate(b, 32, u->dwr); printf("Disk: read %s  write %s\n", a, b);
    fmt_rate(a, 32, u->nrx); fmt_rate(b, 32, u->ntx); printf("Network: recv %s  send %s\n", a, b);
    printf("Processes: %d  Threads: %d  Handles: %d  Up: %s\n\n", u->sys.nproc, u->sys.nthreads, u->sys.nhandles, up);
    Table *t = cur_table(u); if (!t) t = &u->tp;
    for (int i = 0; i < t->ncols; i++) printf("%-*s", t->cols[i].w / 5, coldef[t->cols[i].id].title);
    printf("\n");
    for (int r = 0; r < u->nview && r < 40; r++) {
        Proc *p = &u->procs[u->view[r]];
        for (int i = 0; i < t->ncols; i++) {
            char cell[256]; cell_text(u, p, t->cols[i].id, cell, sizeof cell);
            int w = t->cols[i].w / 5;
            if (t->cols[i].id == CL_NAME) { for (int d = 0; d < p->depth; d++) printf("  "); w -= 2 * p->depth; }
            if ((int)strlen(cell) > w - 1 && w > 1) cell[w - 1] = 0;
            printf("%-*s", w, cell);
        }
        printf("\n");
    }
    if (u->nview > 40) printf("... %d more\n", u->nview - 40);
}

/* ---- introspection ----------------------------------------------------------------- */
void ui_state(UI *u, UIState *st)
{
    Table *t = cur_table(u); if (!t) t = &u->tp;
    memset(st, 0, sizeof *st);
    st->tab = u->tab; st->perf_page = u->perf_page; st->nview = u->nview; st->nproc = u->nproc; st->sel_pid = u->sel_pid;
    st->dlg = u->dlg; st->menu_open = u->menu_open; st->ctx_open = u->ctx_open; st->tree = u->tree; st->paused = u->paused;
    st->sort_col = t->sort_col; st->sort_dir = t->sort_dir; st->scroll = t->scroll;
    snprintf(st->search, sizeof st->search, "%s", u->search);
}
int ui_view_pid(UI *u, int row) { return row >= 0 && row < u->nview ? u->procs[u->view[row]].pid : -1; }
int ui_view_depth(UI *u, int row) { return row >= 0 && row < u->nview ? u->procs[u->view[row]].depth : -1; }
void ui_hit_rects(UI *u, Rect *tabs3, Rect *end_btn, Rect *tree_btn, Rect *hdr, Rect *body, int *row_h)
{
    Table *t = cur_table(u); if (!t) t = &u->tp;
    if (tabs3) memcpy(tabs3, u->r_tabs, sizeof u->r_tabs);
    if (end_btn) *end_btn = u->r_end; if (tree_btn) *tree_btn = u->r_tree;
    if (hdr) *hdr = t->hdr; if (body) *body = t->body; if (row_h) *row_h = P(t->row_h);
}
