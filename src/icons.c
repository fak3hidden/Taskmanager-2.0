/* icons.c - per-process icons.
 * 1. ask the platform for the real application icon (os_icon_load)
 * 2. otherwise draw a crisp generic icon for the process category
 * Results are cached by process name + size. */
#include "tm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- classify ---------------------------------------------------------------- */
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int has(const char *s, const char *sub)      /* portable case-insensitive substring */
{
    size_t n = strlen(sub);
    for (; *s; s++) {
        size_t i = 0;
        while (i < n && s[i] && lower((unsigned char)s[i]) == lower((unsigned char)sub[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}

int icon_generic_kind(const Proc *p)
{
    const char *n = p->name;
    if (p->cmd[0] == '[' || !strcmp(n, "System Idle Process") || !strcmp(n, "kthreadd") || !strcmp(n, "kernel_task")) return IC_KERNEL;
    if (!strcmp(n, "System") || !strcmp(n, "systemd") || !strcmp(n, "init") || !strcmp(n, "launchd") || !strcmp(n, "kernel_task") || !strcmp(n, "Registry") || !strcmp(n, "smss.exe") || !strcmp(n, "csrss.exe") || !strcmp(n, "wininit.exe") || !strcmp(n, "winlogon.exe") || !strcmp(n, "lsass.exe") || !strcmp(n, "services.exe")) return IC_SYSTEM;
    if (has(n, "chrome") || has(n, "firefox") || has(n, "msedge") || has(n, "safari") || has(n, "brave") || has(n, "opera") || has(n, "vivaldi") || has(n, "chromium") || has(n, "WebKit")) return IC_BROWSER;
    if (has(n, "python") || has(n, "pypy")) return IC_PYTHON;
    if (has(n, "node") || has(n, "deno") || has(n, "bun")) return IC_NODE;
    if (has(n, "postgres") || has(n, "mysql") || has(n, "mariadb") || has(n, "redis") || has(n, "mongo") || has(n, "sqlite") || has(n, "sqlservr")) return IC_DB;
    if (has(n, "code") || has(n, "vim") || has(n, "emacs") || has(n, "nano") || has(n, "idea") || has(n, "studio") || has(n, "sublime") || has(n, "notepad") || has(n, "devenv") || has(n, "clion") || has(n, "pycharm")) return IC_EDITOR;
    if (has(n, "spotify") || has(n, "vlc") || has(n, "mpv") || has(n, "pulseaudio") || has(n, "pipewire") || has(n, "wireplumber") || has(n, "audiodg") || has(n, "coreaudio") || has(n, "music") || has(n, "obs")) return IC_MEDIA;
    if (has(n, "discord") || has(n, "slack") || has(n, "teams") || has(n, "telegram") || has(n, "signal") || has(n, "zoom") || has(n, "skype") || has(n, "whatsapp") || has(n, "thunderbird") || has(n, "outlook") || has(n, "mail")) return IC_CHAT;
    if (has(n, "defender") || has(n, "MsMpEng") || has(n, "antimalware") || has(n, "SecurityHealth") || has(n, "apparmor") || has(n, "selinux") || has(n, "firewall") || has(n, "ufw") || has(n, "sshd") || has(n, "polkit") || has(n, "gpg") || has(n, "keyring") || has(n, "lsaiso") || has(n, "securityd")) return IC_SHIELD;
    if (has(n, "explorer") || has(n, "nautilus") || has(n, "dolphin") || has(n, "thunar") || has(n, "finder") || has(n, "nemo") || has(n, "pcmanfm")) return IC_FOLDER;
    if (!strcmp(n, "bash") || !strcmp(n, "zsh") || !strcmp(n, "sh") || !strcmp(n, "fish") || !strcmp(n, "dash") || has(n, "cmd.exe") || has(n, "powershell") || has(n, "pwsh") || has(n, "WindowsTerminal") || has(n, "conhost") || has(n, "terminal") || has(n, "konsole") || has(n, "alacritty") || has(n, "kitty") || has(n, "xterm") || has(n, "tmux") || has(n, "screen") || has(n, "iTerm") || !strcmp(n, "login") || !strcmp(n, "agetty") || !strcmp(n, "ssh")) return IC_TERMINAL;
    if (has(n, "Xorg") || has(n, "Xwayland") || has(n, "gnome-shell") || has(n, "kwin") || has(n, "dwm.exe") || has(n, "mutter") || has(n, "sway") || has(n, "WindowServer") || has(n, "Dock") || has(n, "plasmashell") || has(n, "xfwm") || has(n, "compiz") || has(n, "hyprland") || has(n, "ShellExperience") || has(n, "StartMenu") || has(n, "SearchHost")) return IC_WINDOW;
    size_t len = strlen(n);
    if ((len > 1 && n[len - 1] == 'd' && p->user[0] && strcmp(p->user, "root") != 0 && strcmp(p->user, "SYSTEM") != 0) ||
        has(n, "daemon") || has(n, "svc") || has(n, "service") || has(n, "systemd-") || has(n, "dbus") || has(n, "udev") || has(n, "cron") || has(n, "journal") || has(n, "avahi") || has(n, "cups") || has(n, "bluetooth") || has(n, "Network") || has(n, "wpa_") || has(n, "dhcp") || has(n, "rpc") || has(n, "chrony") || has(n, "ntp") || has(n, "snap") || has(n, "docker") || has(n, "containerd") || has(n, "spool") || has(n, "host.exe") || has(n, "RuntimeBroker") || has(n, "dllhost") || has(n, "wmi") || has(n, "spoolsv") || has(n, "Broker") || has(n, "Agent") || has(n, "helper") || has(n, "Helper") || has(n, "launchd") || has(n, "xpc") || has(n, "coreservices") || has(n, "mds") || has(n, "cfprefsd") || has(n, "distnoted") || has(n, "trustd") || has(n, "logd") || has(n, "syslog") || has(n, "getty") || has(n, "upower") || has(n, "power") || has(n, "acpid") || has(n, "irqbalance") || has(n, "thermald") || has(n, "rsyslog") || has(n, "cupsd") || has(n, "at-spi") || has(n, "gvfs") || has(n, "xdg-") || has(n, "envd") || has(n, "ModemManager") || has(n, "accounts") || has(n, "colord") || has(n, "geoclue") || has(n, "packagekit") || has(n, "fwupd") || has(n, "unattended") || (p->user[0] && (!strcmp(p->user, "root") || !strcmp(p->user, "SYSTEM") || !strcmp(p->user, "LOCAL SERVICE") || !strcmp(p->user, "NETWORK SERVICE") || p->user[0] == '_') && p->cmd[0] != '['))
        return IC_SERVICE;
    return IC_APP;
}

/* ---- procedural drawing ------------------------------------------------------ */
typedef struct { uint32_t *px; int n; } Canvas;

static void cpx(Canvas *c, int x, int y, uint32_t col, float a)
{
    if (x < 0 || y < 0 || x >= c->n || y >= c->n || a <= 0) return;
    if (a > 1) a = 1;
    uint32_t *p = &c->px[y * c->n + x];
    float da = (*p >> 24) / 255.f, oa = a + da * (1 - a);
    if (oa <= 0) { *p = 0; return; }
    int r = (int)((((col >> 16) & 255) * a + ((*p >> 16) & 255) * da * (1 - a)) / oa);
    int g = (int)((((col >> 8) & 255) * a + ((*p >> 8) & 255) * da * (1 - a)) / oa);
    int b = (int)(((col & 255) * a + (*p & 255) * da * (1 - a)) / oa);
    *p = ((uint32_t)(oa * 255 + 0.5f) << 24) | (r << 16) | (g << 8) | b;
}

/* anti-aliased rounded rect on unit coords (0..1) */
static void crrect(Canvas *c, float x0, float y0, float x1, float y1, float r, uint32_t col)
{
    int n = c->n; x0 *= n; y0 *= n; x1 *= n; y1 *= n; r *= n;
    for (int y = (int)y0 - 1; y <= (int)y1 + 1; y++)
        for (int x = (int)x0 - 1; x <= (int)x1 + 1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float qx = fmaxf(fmaxf(x0 + r - px, px - (x1 - r)), 0), qy = fmaxf(fmaxf(y0 + r - py, py - (y1 - r)), 0);
            float d = sqrtf(qx * qx + qy * qy) - r;
            float inside = fminf(px - x0, x1 - px), insidey = fminf(py - y0, y1 - py);
            if (r <= 0) d = -fminf(inside, insidey);
            float a = 0.5f - d; if (a > 1) a = 1;
            cpx(c, x, y, col, a);
        }
}
static void ccircle(Canvas *c, float cx, float cy, float r, uint32_t col)
{
    int n = c->n; cx *= n; cy *= n; r *= n;
    for (int y = (int)(cy - r) - 1; y <= (int)(cy + r) + 1; y++)
        for (int x = (int)(cx - r) - 1; x <= (int)(cx + r) + 1; x++) {
            float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) - r;
            cpx(c, x, y, col, 0.5f - d);
        }
}
static void cline(Canvas *c, float x0, float y0, float x1, float y1, float wdt, uint32_t col)
{
    int n = c->n; x0 *= n; y0 *= n; x1 *= n; y1 *= n; wdt *= n;
    float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    int mx0 = (int)fminf(x0, x1) - 2, mx1 = (int)fmaxf(x0, x1) + 2, my0 = (int)fminf(y0, y1) - 2, my1 = (int)fmaxf(y0, y1) + 2;
    for (int y = my0; y <= my1; y++)
        for (int x = mx0; x <= mx1; x++) {
            float px = x + 0.5f, py = y + 0.5f, t = l2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / l2 : 0;
            if (t < 0) t = 0; if (t > 1) t = 1;
            float ex = x0 + t * dx - px, ey = y0 + t * dy - py, d = sqrtf(ex * ex + ey * ey) - wdt / 2;
            cpx(c, x, y, col, 0.5f - d);
        }
}

static void draw_generic(Canvas *c, int kind)
{
    memset(c->px, 0, (size_t)c->n * c->n * 4);
    const float R = 0.18f;
    switch (kind) {
    case IC_APP:
        crrect(c, .08f, .08f, .92f, .92f, R, 0xff4a8fe7);
        crrect(c, .22f, .22f, .46f, .46f, .05f, 0xffffffff); crrect(c, .54f, .22f, .78f, .46f, .05f, 0xffffffff);
        crrect(c, .22f, .54f, .46f, .78f, .05f, 0xffffffff); crrect(c, .54f, .54f, .78f, .78f, .05f, 0xffd6e8ff);
        break;
    case IC_TERMINAL:
        crrect(c, .06f, .1f, .94f, .9f, .12f, 0xff2b2b33);
        cline(c, .22f, .35f, .38f, .5f, .09f, 0xff7ee787); cline(c, .38f, .5f, .22f, .65f, .09f, 0xff7ee787);
        cline(c, .46f, .68f, .72f, .68f, .09f, 0xffdddddd);
        break;
    case IC_SERVICE:
        ccircle(c, .5f, .5f, .42f, 0xff8e8e96);
        for (int i = 0; i < 8; i++) { float a = i * 3.14159f / 4; crrect(c, .5f + cosf(a) * .38f - .07f, .5f + sinf(a) * .38f - .07f, .5f + cosf(a) * .38f + .07f, .5f + sinf(a) * .38f + .07f, .03f, 0xff8e8e96); }
        ccircle(c, .5f, .5f, .17f, 0xfff0f0f0);
        break;
    case IC_KERNEL:
        crrect(c, .12f, .12f, .88f, .88f, .1f, 0xff5a5f6b);
        crrect(c, .3f, .3f, .7f, .7f, .06f, 0xffb9c2d0);
        for (int i = 0; i < 3; i++) { float t = .3f + i * .2f; cline(c, t, .02f, t, .12f, .07f, 0xff5a5f6b); cline(c, t, .88f, t, .98f, .07f, 0xff5a5f6b); cline(c, .02f, t, .12f, t, .07f, 0xff5a5f6b); cline(c, .88f, t, .98f, t, .07f, 0xff5a5f6b); }
        break;
    case IC_BROWSER:
        ccircle(c, .5f, .5f, .43f, 0xff1e88e5);
        cline(c, .1f, .5f, .9f, .5f, .06f, 0xffbbdefb); cline(c, .5f, .1f, .5f, .9f, .06f, 0xffbbdefb);
        for (int i = 0; i < 64; i++) {   /* meridian ellipse */
            float a0 = i * 6.2832f / 64, a1 = (i + 1) * 6.2832f / 64;
            cline(c, .5f + cosf(a0) * .2f, .5f + sinf(a0) * .42f, .5f + cosf(a1) * .2f, .5f + sinf(a1) * .42f, .05f, 0xffbbdefb);
        }
        break;
    case IC_SHIELD:
        crrect(c, .16f, .08f, .84f, .62f, .08f, 0xff2e7d32);
        ccircle(c, .5f, .6f, .34f, 0xff2e7d32);
        cline(c, .35f, .5f, .46f, .62f, .09f, 0xffffffff); cline(c, .46f, .62f, .66f, .38f, .09f, 0xffffffff);
        break;
    case IC_WINDOW:
        crrect(c, .06f, .12f, .94f, .88f, .1f, 0xff5c6bc0);
        crrect(c, .06f, .12f, .94f, .3f, .1f, 0xff3949ab);
        ccircle(c, .18f, .21f, .045f, 0xffef5350); ccircle(c, .3f, .21f, .045f, 0xffffca28); ccircle(c, .42f, .21f, .045f, 0xff66bb6a);
        crrect(c, .16f, .4f, .84f, .78f, .05f, 0xffe8eaf6);
        break;
    case IC_PYTHON:
        crrect(c, .26f, .06f, .74f, .54f, .16f, 0xff3776ab); crrect(c, .26f, .46f, .74f, .94f, .16f, 0xffffd43b);
        crrect(c, .06f, .3f, .54f, .7f, .14f, 0xff3776ab); crrect(c, .46f, .3f, .94f, .7f, .14f, 0xffffd43b);
        ccircle(c, .38f, .18f, .05f, 0xffffffff); ccircle(c, .62f, .82f, .05f, 0xffffffff);
        break;
    case IC_NODE:
        for (int i = 0; i < 6; i++) { float a = i * 1.0472f - 0.5236f, b = a + 1.0472f; cline(c, .5f + cosf(a) * .42f, .5f + sinf(a) * .42f, .5f + cosf(b) * .42f, .5f + sinf(b) * .42f, .1f, 0xff539e43); }
        ccircle(c, .5f, .5f, .32f, 0xff539e43);
        cline(c, .38f, .64f, .38f, .36f, .09f, 0xffffffff); cline(c, .38f, .36f, .62f, .64f, .09f, 0xffffffff); cline(c, .62f, .64f, .62f, .36f, .09f, 0xffffffff);
        break;
    case IC_DB:
        crrect(c, .14f, .2f, .86f, .86f, .12f, 0xff00897b);
        for (int i = 0; i < 3; i++) { float y = .22f + i * .22f; crrect(c, .14f, y - .05f, .86f, y + .05f, .05f, 0xff4db6ac); }
        break;
    case IC_EDITOR:
        crrect(c, .1f, .06f, .9f, .94f, .1f, 0xff0288d1);
        for (int i = 0; i < 4; i++) { float y = .25f + i * .16f; cline(c, .25f, y, i == 1 ? .55f : .75f, y, .07f, 0xffffffff); }
        break;
    case IC_MEDIA:
        ccircle(c, .5f, .5f, .43f, 0xffe64a19);
        cline(c, .4f, .3f, .4f, .7f, .1f, 0xffffffff); cline(c, .4f, .3f, .7f, .5f, .1f, 0xffffffff); cline(c, .4f, .7f, .7f, .5f, .1f, 0xffffffff);
        break;
    case IC_CHAT:
        crrect(c, .06f, .12f, .94f, .74f, .2f, 0xff7e57c2);
        cline(c, .2f, .74f, .2f, .92f, .12f, 0xff7e57c2); cline(c, .2f, .92f, .4f, .74f, .12f, 0xff7e57c2);
        ccircle(c, .32f, .43f, .06f, 0xffffffff); ccircle(c, .5f, .43f, .06f, 0xffffffff); ccircle(c, .68f, .43f, .06f, 0xffffffff);
        break;
    case IC_FOLDER:
        crrect(c, .06f, .2f, .5f, .4f, .08f, 0xffe6a800);
        crrect(c, .06f, .3f, .94f, .86f, .1f, 0xffffc107);
        crrect(c, .06f, .42f, .94f, .86f, .1f, 0xffffca28);
        break;
    case IC_SYSTEM:
        crrect(c, .1f, .1f, .48f, .48f, .06f, 0xff0078d4); crrect(c, .52f, .1f, .9f, .48f, .06f, 0xff0078d4);
        crrect(c, .1f, .52f, .48f, .9f, .06f, 0xff0078d4); crrect(c, .52f, .52f, .9f, .9f, .06f, 0xff0078d4);
        break;
    }
}

/* ---- cache ---------------------------------------------------------------------- */
typedef struct { char key[72]; int size; Icon ic; } Entry;
static Entry *cache; static int ncache, capcache;
static Icon generic[IC_COUNT][2];   /* [kind][size index: 0 = 16*s, 1 = 32*s] */

static Icon *generic_icon(int kind, int size)
{
    int idx = size > 20 ? 1 : 0;
    /* the same slot may be requested with different scaled sizes; regenerate if size differs */
    Icon *ic = &generic[kind][idx];
    if (ic->px && ic->size == size) return ic;
    free(ic->px);
    ic->size = size; ic->kind = kind; ic->px = calloc((size_t)size * size, 4);
    Canvas c = { ic->px, size };
    /* render at 4x and downsample for smoother edges on small sizes */
    int big = size * 4; uint32_t *tmp = calloc((size_t)big * big, 4); Canvas cb = { tmp, big };
    draw_generic(&cb, kind);
    icon_scale(tmp, big, big, ic->px, size);
    free(tmp); (void)c;
    return ic;
}

const Icon *icon_for(const Proc *p, int size)
{
    char key[72]; snprintf(key, sizeof key, "%s", p->name);
    for (int i = 0; i < ncache; i++) if (cache[i].size == size && !strcmp(cache[i].key, key)) return &cache[i].ic;
    if (ncache >= capcache) { capcache = capcache ? capcache * 2 : 128; cache = realloc(cache, capcache * sizeof *cache); }
    Entry *e = &cache[ncache++]; memset(e, 0, sizeof *e);
    snprintf(e->key, sizeof e->key, "%s", key); e->size = size;
    e->ic.size = size; e->ic.kind = icon_generic_kind(p);
    uint32_t *buf = malloc((size_t)size * size * 4);
    if (os_icon_load(p, size, buf)) e->ic.px = buf;
    else { free(buf); e->ic.px = NULL; }
    return &e->ic;
}

void icon_draw(Gfx *g, int x, int y, const Icon *ic)
{
    if (!ic) return;
    const Icon *src = ic->px ? ic : generic_icon(ic->kind, ic->size);
    gfx_blit(g, x, y, src->px, src->size, src->size);
}
