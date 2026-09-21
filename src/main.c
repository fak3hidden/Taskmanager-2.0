/* main.c - entry point / event loop */
#include "tm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

static void usage(void)
{
    printf("Task Manager %s\n\n"
           "usage: taskmgr [options]\n"
           "  --scale N          UI scale factor (1 or 2, default: auto)\n"
           "  --interval MS      sampling interval in ms (default 1000)\n"
           "  --tab N            start tab: 0 processes, 1 performance, 2 details\n"
           "  --dump             print a text snapshot to stdout and exit (no window)\n"
           "  --screenshot FILE  render one frame to FILE.bmp and exit (no window)\n"
           "  --size WxH         window / screenshot size (default 900x600)\n"
           "  -h, --help         this help\n\n"
           "keys: Tab tabs  Ctrl+F/type search  Up/Down select  Del end task  Shift+Del end tree\n"
           "      Enter properties  Ctrl+T tree  Ctrl+K kernel threads  Space pause  F5 refresh\n"
           "      Ctrl+L per-core graphs  Ctrl+Q quit  right-click for context menu\n", TM_VERSION);
}

int main(int argc, char **argv)
{
    int scale = 0, interval = 1000, tab = 0, w = 900, h = 600, dump = 0;
    const char *shot = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--interval") && i + 1 < argc) interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tab") && i + 1 < argc) tab = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump")) dump = 1;
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else { fprintf(stderr, "unknown option %s\n", argv[i]); usage(); return 2; }
    }
    if (interval < 100) interval = 100;
    if (tab < 0 || tab > 2) tab = 0;

    if (dump) {
        UI *u = ui_create(1);
        ui_set_tab(u, tab, 0);
        ui_resize(u, w, h);
        ui_tick(u);
        sleep_ms(interval > 600 ? 600 : interval);    /* second sample gives real CPU % */
        ui_force_sample(u);
        ui_draw(u);
        ui_dump(u);
        ui_destroy(u);
        return 0;
    }

    if (shot) { headless_config(shot, w, h); if (scale <= 0) scale = 1; }
    if (win_open("Task Manager", w, h, &scale) != 0) return 1;

    UI *u = ui_create(scale);
    ui_set_tab(u, tab, 0);
    ui_set_interval(u, interval);
    ui_resize(u, w * (scale > 1 ? scale : 1), h * (scale > 1 ? scale : 1));

    int dirty = 1;
    for (;;) {
        Event ev;
        int wait = ui_next_due_ms(u);
        if (dirty) wait = 0;
        while (win_poll(&ev, wait)) {
            int r = ui_event(u, &ev);
            if (r < 0) goto done;
            if (r > 0) dirty = 1;
            wait = 0;
        }
        if (ui_tick(u)) dirty = 1;
        if (shot) {
            /* headless: take two samples so CPU% is meaningful, then write and exit */
            for (int i = 0; i < 24; i++) { sleep_ms(125); ui_force_sample(u); }
            ui_draw(u);
            int pw, ph; const uint32_t *px = ui_pixels(u, &pw, &ph);
            win_present(px, pw, ph);
            break;
        }
        if (dirty) {
            ui_draw(u);
            int pw, ph; const uint32_t *px = ui_pixels(u, &pw, &ph);
            win_present(px, pw, ph);
            dirty = 0;
        }
    }
done:
    ui_destroy(u);
    win_close();
    return 0;
}

#ifdef _WIN32
/* GUI subsystem entry: forward to main so the app has no console window */
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    (void)hi; (void)hp; (void)cmd; (void)show;
    return main(__argc, __argv);
}
#endif
