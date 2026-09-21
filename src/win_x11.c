/* win_x11.c - minimal X11 window.  libX11 is loaded at runtime with dlopen so
 * the binary has no link-time dependency and no headers are required.       */
#include "tm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/select.h>
#include <sys/time.h>

/* --- just enough of Xlib's ABI ------------------------------------------ */
typedef unsigned long XID, Atom, Time, KeySym;
typedef XID Window, Drawable, Colormap;
typedef struct _XDisplay Display;
typedef struct _XGC *GC;
typedef struct { void *ext; XID visualid; int class; unsigned long r, g, b; int bits, entries; } Visual;
typedef char *XPointer;

typedef struct _XImage {
    int width, height, xoffset, format;
    char *data;
    int byte_order, bitmap_unit, bitmap_bit_order, bitmap_pad, depth, bytes_per_line, bits_per_pixel;
    unsigned long red_mask, green_mask, blue_mask;
    XPointer obdata;
    struct funcs {
        struct _XImage *(*create_image)(void);
        int (*destroy_image)(struct _XImage *);
        unsigned long (*get_pixel)(struct _XImage *, int, int);
        int (*put_pixel)(struct _XImage *, int, int, unsigned long);
        struct _XImage *(*sub_image)(struct _XImage *, int, int, unsigned int, unsigned int);
        int (*add_pixel)(struct _XImage *, long);
    } f;
} XImage;

typedef struct { int type; unsigned long serial; int send_event; Display *display; Window window;
    Window root, subwindow; Time time; int x, y, x_root, y_root; unsigned int state, keycode; int same_screen; } XKeyEvent;
typedef struct { int type; unsigned long serial; int send_event; Display *display; Window window;
    Window root, subwindow; Time time; int x, y, x_root, y_root; unsigned int state, button; int same_screen; } XButtonEvent;
typedef struct { int type; unsigned long serial; int send_event; Display *display; Window window;
    Window root, subwindow; Time time; int x, y, x_root, y_root; unsigned int state; char is_hint; int same_screen; } XMotionEvent;
typedef struct { int type; unsigned long serial; int send_event; Display *display; Window window;
    int x, y, width, height, count; } XExposeEvent;
typedef struct { int type; unsigned long serial; int send_event; Display *display; Window event, window;
    int x, y, width, height, border_width; Window above; int override_redirect; } XConfigureEvent;
typedef struct { int type; unsigned long serial; int send_event; Display *display; Window window;
    Atom message_type; int format; union { char b[20]; short s[10]; long l[5]; } data; } XClientMessageEvent;
typedef union { int type; XKeyEvent xkey; XButtonEvent xbutton; XMotionEvent xmotion; XExposeEvent xexpose;
    XConfigureEvent xconfigure; XClientMessageEvent xclient; long pad[24]; } XEvent;

typedef struct { long flags; int x, y, width, height, min_width, min_height, max_width, max_height,
    width_inc, height_inc; struct { int x, y; } min_aspect, max_aspect; int base_width, base_height, win_gravity; } XSizeHints;

enum { KeyPress = 2, KeyRelease = 3, ButtonPress = 4, ButtonRelease = 5, MotionNotify = 6,
       Expose = 12, ConfigureNotify = 22, ClientMessage = 33 };
#define KeyPressMask (1L<<0)
#define KeyReleaseMask (1L<<1)
#define ButtonPressMask (1L<<2)
#define ButtonReleaseMask (1L<<3)
#define PointerMotionMask (1L<<6)
#define ExposureMask (1L<<15)
#define StructureNotifyMask (1L<<17)
#define ZPixmap 2
#define ShiftMask 1
#define ControlMask 4
#define Mod1Mask 8
#define PMinSize (1L<<4)

static struct {
    void *lib;
    Display *(*OpenDisplay)(const char *);
    int (*CloseDisplay)(Display *);
    int (*DefaultScreen)(Display *);
    Window (*DefaultRootWindow)(Display *);
    Visual *(*DefaultVisual)(Display *, int);
    int (*DefaultDepth)(Display *, int);
    GC (*DefaultGC)(Display *, int);
    unsigned long (*BlackPixel)(Display *, int);
    unsigned long (*WhitePixel)(Display *, int);
    Window (*CreateSimpleWindow)(Display *, Window, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long);
    int (*SelectInput)(Display *, Window, long);
    int (*MapWindow)(Display *, Window);
    int (*StoreName)(Display *, Window, const char *);
    Atom (*InternAtom)(Display *, const char *, int);
    int (*SetWMProtocols)(Display *, Window, Atom *, int);
    int (*Pending)(Display *);
    int (*NextEvent)(Display *, XEvent *);
    int (*ConnectionNumber)(Display *);
    XImage *(*CreateImage)(Display *, Visual *, unsigned, int, int, char *, unsigned, unsigned, int, int);
    int (*PutImage)(Display *, Drawable, GC, XImage *, int, int, int, int, unsigned, unsigned);
    int (*Flush)(Display *);
    int (*Sync)(Display *, int);
    int (*LookupString)(XKeyEvent *, char *, int, KeySym *, void *);
    KeySym (*LookupKeysym)(XKeyEvent *, int);
    int (*DestroyWindow)(Display *, Window);
    int (*ChangeProperty)(Display *, Window, Atom, Atom, int, int, const unsigned char *, int);
    void (*SetWMNormalHints)(Display *, Window, XSizeHints *);
    int (*DisplayWidth)(Display *, int);
    int (*DisplayHeight)(Display *, int);
    int (*DisplayWidthMM)(Display *, int);
} X;

static Display *dpy; static Window win; static int scr; static XImage *img;
static Atom wm_delete; static int cur_w, cur_h;
static int headless; static const char *shot_path; static int shot_w, shot_h, shot_frames;

void headless_config(const char *bmp_path, int w, int h)
{
    headless = 1; shot_path = bmp_path; shot_w = w; shot_h = h; shot_frames = 0;
}

#define LOAD(n) do { *(void **)&X.n = dlsym(X.lib, "X" #n); if (!X.n) { fprintf(stderr, "X11: missing X%s\n", #n); return -1; } } while (0)

int win_open(const char *title, int w, int h, int *scale)
{
    cur_w = w; cur_h = h;
    if (headless) { cur_w = shot_w; cur_h = shot_h; return 0; }
    X.lib = dlopen("libX11.so.6", RTLD_NOW);
    if (!X.lib) X.lib = dlopen("libX11.so", RTLD_NOW);
    if (!X.lib) {
        fprintf(stderr, "Could not load libX11 (%s).\nNo display available - use --dump for a text snapshot or --screenshot FILE.bmp\n", dlerror());
        return -1;
    }
    LOAD(OpenDisplay); LOAD(CloseDisplay); LOAD(DefaultScreen); LOAD(DefaultRootWindow); LOAD(DefaultVisual);
    LOAD(DefaultDepth); LOAD(DefaultGC); LOAD(BlackPixel); LOAD(WhitePixel); LOAD(CreateSimpleWindow);
    LOAD(SelectInput); LOAD(MapWindow); LOAD(StoreName); LOAD(InternAtom); LOAD(SetWMProtocols);
    LOAD(Pending); LOAD(NextEvent); LOAD(ConnectionNumber); LOAD(CreateImage); LOAD(PutImage); LOAD(Flush);
    LOAD(Sync); LOAD(LookupString); LOAD(LookupKeysym); LOAD(DestroyWindow); LOAD(ChangeProperty);
    LOAD(SetWMNormalHints); LOAD(DisplayWidth); LOAD(DisplayHeight); LOAD(DisplayWidthMM);

    dpy = X.OpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "Cannot open X display (is DISPLAY set?)\n"); return -1; }
    scr = X.DefaultScreen(dpy);
    if (X.DefaultDepth(dpy, scr) < 24) { fprintf(stderr, "Need a 24/32-bit display\n"); return -1; }

    /* auto UI scale from physical DPI when caller passes 0 */
    if (scale && *scale <= 0) {
        int mm = X.DisplayWidthMM(dpy, scr), pxw = X.DisplayWidth(dpy, scr);
        double dpi = mm > 0 ? pxw * 25.4 / mm : 96;
        *scale = dpi >= 168 ? 2 : 1;
        if (X.DisplayHeight(dpy, scr) >= 2000 && *scale == 1) *scale = 2;
    }
    if (scale && *scale > 1) { w *= *scale; h *= *scale; cur_w = w; cur_h = h; }

    win = X.CreateSimpleWindow(dpy, X.DefaultRootWindow(dpy), 0, 0, (unsigned)w, (unsigned)h, 0,
                               X.BlackPixel(dpy, scr), 0xf3f3f3);
    X.SelectInput(dpy, win, KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                            ExposureMask | StructureNotifyMask);
    X.StoreName(dpy, win, title);
    Atom cls = X.InternAtom(dpy, "WM_CLASS", 0), str = X.InternAtom(dpy, "STRING", 0);
    X.ChangeProperty(dpy, win, cls, str, 8, 0, (const unsigned char *)"taskmgr\0TaskManager", 20);
    XSizeHints hints; memset(&hints, 0, sizeof hints);
    hints.flags = PMinSize; hints.min_width = 480; hints.min_height = 320;
    X.SetWMNormalHints(dpy, win, &hints);
    wm_delete = X.InternAtom(dpy, "WM_DELETE_WINDOW", 0);
    X.SetWMProtocols(dpy, win, &wm_delete, 1);
    X.MapWindow(dpy, win);
    X.Flush(dpy);
    return 0;
}

static int map_key(KeySym ks)
{
    switch (ks) {
    case 0xff52: return K_UP;    case 0xff54: return K_DOWN;  case 0xff51: return K_LEFT;  case 0xff53: return K_RIGHT;
    case 0xff55: return K_PGUP;  case 0xff56: return K_PGDN;  case 0xff50: return K_HOME;  case 0xff57: return K_END;
    case 0xffff: return K_DEL;   case 0xff08: return K_BS;    case 0xff0d: return K_ENTER; case 0xff8d: return K_ENTER;
    case 0xff1b: return K_ESC;   case 0xff09: return K_TAB;   case 0xfe20: return K_TAB;
    }
    if (ks >= 0xffbe && ks <= 0xffc9) return K_F1 + (int)(ks - 0xffbe);
    return 0;
}

static int mods_of(unsigned st)
{
    return (st & ShiftMask ? KM_SHIFT : 0) | (st & ControlMask ? KM_CTRL : 0) | (st & Mod1Mask ? KM_ALT : 0);
}

int win_poll(Event *ev, int timeout_ms)
{
    memset(ev, 0, sizeof *ev);
    if (headless) {
        if (shot_frames == 0) { ev->type = EV_RESIZE; ev->w = cur_w; ev->h = cur_h; shot_frames++; return 1; }
        if (timeout_ms > 0) { struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 }; select(0, 0, 0, 0, &tv); }
        return 0;
    }
    if (!X.Pending(dpy)) {
        if (timeout_ms <= 0) return 0;
        fd_set fds; int fd = X.ConnectionNumber(dpy);
        FD_ZERO(&fds); FD_SET(fd, &fds);
        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0) return 0;
        if (!X.Pending(dpy)) return 0;
    }
    XEvent xe; X.NextEvent(dpy, &xe);
    switch (xe.type) {
    case KeyPress: {
        char buf[8]; KeySym ks = 0;
        int n = X.LookupString(&xe.xkey, buf, sizeof buf, &ks, NULL);
        ev->mods = mods_of(xe.xkey.state);
        ev->key = map_key(ks);
        if (ev->key) { ev->type = EV_KEY; return 1; }
        if (n == 1 && (unsigned char)buf[0] >= 32 && (unsigned char)buf[0] < 127) {
            ev->type = EV_CHAR; ev->ch = buf[0]; return 1;
        }
        if (ks >= 0x20 && ks < 0x7f) { ev->type = EV_CHAR; ev->ch = (int)ks; return 1; }
        return 0;
    }
    case ButtonPress:
        ev->x = xe.xbutton.x; ev->y = xe.xbutton.y; ev->mods = mods_of(xe.xbutton.state);
        if (xe.xbutton.button == 4 || xe.xbutton.button == 5) {
            ev->type = EV_WHEEL; ev->delta = xe.xbutton.button == 4 ? 1 : -1; return 1;
        }
        ev->type = EV_MOUSE_DOWN; ev->button = (int)xe.xbutton.button; return 1;
    case ButtonRelease:
        if (xe.xbutton.button >= 4) return 0;
        ev->type = EV_MOUSE_UP; ev->x = xe.xbutton.x; ev->y = xe.xbutton.y; ev->button = (int)xe.xbutton.button; return 1;
    case MotionNotify:
        ev->type = EV_MOUSE_MOVE; ev->x = xe.xmotion.x; ev->y = xe.xmotion.y; return 1;
    case Expose:
        if (xe.xexpose.count) return 0;
        ev->type = EV_PAINT; return 1;
    case ConfigureNotify:
        if (xe.xconfigure.width == cur_w && xe.xconfigure.height == cur_h) return 0;
        cur_w = xe.xconfigure.width; cur_h = xe.xconfigure.height;
        ev->type = EV_RESIZE; ev->w = cur_w; ev->h = cur_h; return 1;
    case ClientMessage:
        if ((Atom)xe.xclient.data.l[0] == wm_delete) { ev->type = EV_QUIT; return 1; }
        return 0;
    }
    return 0;
}

void win_present(const uint32_t *px, int w, int h)
{
    if (headless) {
        if (shot_path) bmp_write(shot_path, px, w, h);
        return;
    }
    if (img && (img->width != w || img->height != h)) { img->data = NULL; img->f.destroy_image(img); img = NULL; }
    if (!img) {
        img = X.CreateImage(dpy, X.DefaultVisual(dpy, scr), (unsigned)X.DefaultDepth(dpy, scr), ZPixmap, 0,
                            (char *)px, (unsigned)w, (unsigned)h, 32, w * 4);
        if (!img) return;
    }
    img->data = (char *)px;
    X.PutImage(dpy, win, X.DefaultGC(dpy, scr), img, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
    X.Flush(dpy);
}

void win_close(void)
{
    if (headless) return;
    if (img) { img->data = NULL; img->f.destroy_image(img); img = NULL; }
    if (dpy) { X.DestroyWindow(dpy, win); X.CloseDisplay(dpy); dpy = NULL; }
}

/* ---- BMP writer (used for --screenshot and tests) ----------------------- */
int bmp_write(const char *path, const uint32_t *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int stride = (w * 3 + 3) & ~3, size = 54 + stride * h;
    unsigned char hdr[54] = { 'B', 'M' };
    hdr[2] = size; hdr[3] = size >> 8; hdr[4] = size >> 16; hdr[5] = size >> 24;
    hdr[10] = 54; hdr[14] = 40;
    hdr[18] = w; hdr[19] = w >> 8; hdr[20] = w >> 16; hdr[21] = w >> 24;
    hdr[22] = h; hdr[23] = h >> 8; hdr[24] = h >> 16; hdr[25] = h >> 24;
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    unsigned char *row = calloc(1, stride);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t c = px[y * w + x];
            row[x * 3] = c & 255; row[x * 3 + 1] = (c >> 8) & 255; row[x * 3 + 2] = (c >> 16) & 255;
        }
        fwrite(row, 1, stride, f);
    }
    free(row);
    fclose(f);
    return 0;
}
