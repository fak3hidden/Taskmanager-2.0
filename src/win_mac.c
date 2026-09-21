/* win_mac.c - minimal Cocoa window written in plain C.
 * AppKit / CoreGraphics / libobjc are loaded at runtime with dlopen, so the
 * binary can be cross-compiled without any Apple SDK.                       */
#include "tm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>

typedef void *id, *SEL, *Class, *IMP;
typedef struct { double x, y; } NSPoint;
typedef struct { double w, h; } NSSize;
typedef struct { NSPoint origin; NSSize size; } NSRect;

static id (*objc_getClass_)(const char *);
static SEL (*sel_)(const char *);
static void *msgSend;            /* objc_msgSend, cast per call */
static void *msgSend_stret;      /* x86_64 only */
static Class (*allocPair)(Class, const char *, size_t);
static void (*regPair)(Class);
static int (*addMethod)(Class, SEL, IMP, const char *);
static int (*addIvar)(Class, const char *, size_t, unsigned char, const char *);

#define SEND(ret, obj, sel, ...) ((ret (*)(id, SEL, ##__VA_ARGS__))msgSend)
#define CLS(n) objc_getClass_(n)

/* CoreGraphics */
static void *(*CGColorSpaceCreateDeviceRGB_)(void);
static void *(*CGDataProviderCreateWithData_)(void *, const void *, size_t, void *);
static void *(*CGImageCreate_)(size_t, size_t, size_t, size_t, size_t, void *, uint32_t, void *, const double *, int, int);
static void (*CGContextDrawImage_)(void *, NSRect, void *);
static void (*CGImageRelease_)(void *);
static void (*CGDataProviderRelease_)(void *);

static id app, window, view, pool;
static const uint32_t *fb; static int fb_w, fb_h, cur_w, cur_h, want_quit;
static Event queue[64]; static int qh, qt;
static int headless; static const char *shot_path; static int shot_w, shot_h, shot_frames;
static void *cs;

void headless_config(const char *bmp_path, int w, int h) { headless = 1; shot_path = bmp_path; shot_w = w; shot_h = h; }
static void push(Event *e) { int n = (qt + 1) % 64; if (n == qh) return; queue[qt] = *e; qt = n; }

/* ---- custom view methods -------------------------------------------- */
static void view_drawRect(id self, SEL _cmd, NSRect r)
{
    (void)_cmd; (void)r;
    if (!fb) return;
    id nsctx = SEND(id, 0, 0)(CLS("NSGraphicsContext"), sel_("currentContext"));
    void *cg = SEND(void *, 0, 0)(nsctx, sel_("CGContext"));
    void *prov = CGDataProviderCreateWithData_(NULL, fb, (size_t)fb_w * fb_h * 4, NULL);
    /* kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst = 0x2000 | 6 */
    void *img = CGImageCreate_(fb_w, fb_h, 8, 32, fb_w * 4, cs, 0x2006, prov, NULL, 0, 0);
    NSRect dst = { { 0, 0 }, { (double)cur_w, (double)cur_h } };
    CGContextDrawImage_(cg, dst, img);
    CGImageRelease_(img); CGDataProviderRelease_(prov);
    (void)self;
}
static signed char view_isFlipped(id self, SEL _cmd) { (void)self; (void)_cmd; return 1; }
static signed char view_acceptsFirstResponder(id self, SEL _cmd) { (void)self; (void)_cmd; return 1; }
static signed char win_shouldClose(id self, SEL _cmd, id sender) { (void)self; (void)_cmd; (void)sender; want_quit = 1; return 0; }
static void win_didResize(id self, SEL _cmd, id note)
{
    (void)self; (void)_cmd; (void)note;
    NSRect b;
#if defined(__x86_64__)
    ((void (*)(NSRect *, id, SEL))msgSend_stret)(&b, view, sel_("bounds"));
#else
    b = SEND(NSRect, 0, 0)(view, sel_("bounds"));
#endif
    int w = (int)b.size.w, h = (int)b.size.h;
    if (w && h && (w != cur_w || h != cur_h)) { cur_w = w; cur_h = h; Event e; memset(&e, 0, sizeof e); e.type = EV_RESIZE; e.w = w; e.h = h; push(&e); }
}

static int load(void)
{
    void *objc = dlopen("/usr/lib/libobjc.A.dylib", RTLD_NOW);
    void *appkit = dlopen("/System/Library/Frameworks/AppKit.framework/AppKit", RTLD_NOW);
    void *cg = dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_NOW);
    if (!objc || !appkit || !cg) { fprintf(stderr, "cannot load Cocoa: %s\n", dlerror()); return -1; }
    *(void **)&objc_getClass_ = dlsym(objc, "objc_getClass");
    *(void **)&sel_ = dlsym(objc, "sel_registerName");
    msgSend = dlsym(objc, "objc_msgSend");
    msgSend_stret = dlsym(objc, "objc_msgSend_stret");
    *(void **)&allocPair = dlsym(objc, "objc_allocateClassPair");
    *(void **)&regPair = dlsym(objc, "objc_registerClassPair");
    *(void **)&addMethod = dlsym(objc, "class_addMethod");
    *(void **)&addIvar = dlsym(objc, "class_addIvar");
    *(void **)&CGColorSpaceCreateDeviceRGB_ = dlsym(cg, "CGColorSpaceCreateDeviceRGB");
    *(void **)&CGDataProviderCreateWithData_ = dlsym(cg, "CGDataProviderCreateWithData");
    *(void **)&CGImageCreate_ = dlsym(cg, "CGImageCreate");
    *(void **)&CGContextDrawImage_ = dlsym(cg, "CGContextDrawImage");
    *(void **)&CGImageRelease_ = dlsym(cg, "CGImageRelease");
    *(void **)&CGDataProviderRelease_ = dlsym(cg, "CGDataProviderRelease");
    if (!objc_getClass_ || !sel_ || !msgSend || !CGImageCreate_) return -1;
    return 0;
}

int win_open(const char *title, int w, int h, int *scale)
{
    if (headless) { cur_w = shot_w; cur_h = shot_h; return 0; }
    if (load()) return -1;
    cs = CGColorSpaceCreateDeviceRGB_();
    pool = SEND(id, 0, 0)(SEND(id, 0, 0)(CLS("NSAutoreleasePool"), sel_("alloc")), sel_("init"));
    app = SEND(id, 0, 0)(CLS("NSApplication"), sel_("sharedApplication"));
    SEND(void, 0, 0, long)(app, sel_("setActivationPolicy:"), 0);

    /* Retina: backing scale factor */
    if (scale && *scale <= 0) {
        id screen = SEND(id, 0, 0)(CLS("NSScreen"), sel_("mainScreen"));
        double bsf = screen ? SEND(double, 0, 0)(screen, sel_("backingScaleFactor")) : 1.0;
        *scale = bsf >= 2.0 ? 2 : 1;
    }
    /* we render at 1 buffer px = 1 point (Cocoa scales up); large scale = bigger UI */
    if (scale && *scale > 1) { w *= *scale; h *= *scale; }
    cur_w = w; cur_h = h;

    Class ViewCls = allocPair(CLS("NSView"), "TMView", 0);
    addMethod(ViewCls, sel_("drawRect:"), (IMP)view_drawRect, "v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
    addMethod(ViewCls, sel_("isFlipped"), (IMP)view_isFlipped, "c@:");
    addMethod(ViewCls, sel_("acceptsFirstResponder"), (IMP)view_acceptsFirstResponder, "c@:");
    regPair(ViewCls);
    Class DelCls = allocPair(CLS("NSObject"), "TMDelegate", 0);
    addMethod(DelCls, sel_("windowShouldClose:"), (IMP)win_shouldClose, "c@:@");
    addMethod(DelCls, sel_("windowDidResize:"), (IMP)win_didResize, "v@:@");
    regPair(DelCls);

    NSRect frame = { { 0, 0 }, { (double)w, (double)h } };
    /* NSWindowStyleMaskTitled|Closable|Miniaturizable|Resizable = 15, NSBackingStoreBuffered = 2 */
    window = SEND(id, 0, 0)(CLS("NSWindow"), sel_("alloc"));
    window = SEND(id, 0, 0, NSRect, unsigned long, unsigned long, signed char)(window,
                 sel_("initWithContentRect:styleMask:backing:defer:"), frame, 15, 2, 0);
    id nstitle = SEND(id, 0, 0, const char *)(CLS("NSString"), sel_("stringWithUTF8String:"), title);
    SEND(void, 0, 0, id)(window, sel_("setTitle:"), nstitle);
    NSSize minsz = { 480, 320 };
    SEND(void, 0, 0, NSSize)(window, sel_("setContentMinSize:"), minsz);
    view = SEND(id, 0, 0)(SEND(id, 0, 0)((id)ViewCls, sel_("alloc")), sel_("init"));
    SEND(void, 0, 0, id)(window, sel_("setContentView:"), view);
    id del = SEND(id, 0, 0)(SEND(id, 0, 0)((id)DelCls, sel_("alloc")), sel_("init"));
    SEND(void, 0, 0, id)(window, sel_("setDelegate:"), del);
    SEND(void, 0, 0)(window, sel_("center"));
    SEND(void, 0, 0, id)(window, sel_("makeKeyAndOrderFront:"), NULL);
    SEND(void, 0, 0, id)(window, sel_("makeFirstResponder:"), view);
    SEND(void, 0, 0, signed char)(app, sel_("activateIgnoringOtherApps:"), 1);
    SEND(void, 0, 0)(app, sel_("finishLaunching"));
    Event e; memset(&e, 0, sizeof e); e.type = EV_RESIZE; e.w = w; e.h = h; push(&e);
    return 0;
}

static int mods_of(unsigned long f)
{
    return ((f & (1 << 17)) ? KM_SHIFT : 0) | ((f & (1 << 18)) ? KM_CTRL : 0) | ((f & (1 << 19)) ? KM_ALT : 0) |
           ((f & (1 << 20)) ? KM_CTRL : 0); /* treat Cmd as Ctrl */
}

static int map_key(unsigned short kc, const char *chars)
{
    switch (kc) {
    case 126: return K_UP; case 125: return K_DOWN; case 123: return K_LEFT; case 124: return K_RIGHT;
    case 116: return K_PGUP; case 121: return K_PGDN; case 115: return K_HOME; case 119: return K_END;
    case 117: return K_DEL; case 51: return K_BS; case 36: return K_ENTER; case 76: return K_ENTER;
    case 53: return K_ESC; case 48: return K_TAB;
    case 122: return K_F1; case 120: return K_F2; case 99: return K_F3; case 118: return K_F4; case 96: return K_F5;
    case 97: return K_F6; case 98: return K_F7; case 100: return K_F8; case 101: return K_F9; case 109: return K_F10;
    }
    (void)chars;
    return 0;
}

int win_poll(Event *ev, int timeout_ms)
{
    memset(ev, 0, sizeof *ev);
    if (headless) {
        if (shot_frames == 0) { ev->type = EV_RESIZE; ev->w = cur_w; ev->h = cur_h; shot_frames++; return 1; }
        if (timeout_ms > 0) usleep(timeout_ms * 1000);
        return 0;
    }
    if (qh != qt) { *ev = queue[qh]; qh = (qh + 1) % 64; return 1; }
    if (want_quit) { ev->type = EV_QUIT; return 1; }

    id until = SEND(id, 0, 0, double)(CLS("NSDate"), sel_("dateWithTimeIntervalSinceNow:"), timeout_ms / 1000.0);
    id mode = SEND(id, 0, 0, const char *)(CLS("NSString"), sel_("stringWithUTF8String:"), "kCFRunLoopDefaultMode");
    id e = SEND(id, 0, 0, unsigned long long, id, id, signed char)(app,
              sel_("nextEventMatchingMask:untilDate:inMode:dequeue:"), ~0ull, until, mode, 1);
    if (!e) { if (want_quit) { ev->type = EV_QUIT; return 1; } return 0; }
    long type = SEND(long, 0, 0)(e, sel_("type"));
    NSPoint loc = SEND(NSPoint, 0, 0)(e, sel_("locationInWindow"));
    int x = (int)loc.x, y = cur_h - (int)loc.y;
    unsigned long flags = SEND(unsigned long, 0, 0)(e, sel_("modifierFlags"));
    int handled = 0;
    switch (type) {
    case 1: case 3: case 25: /* L/R/other mouse down */
        ev->type = EV_MOUSE_DOWN; ev->x = x; ev->y = y; ev->button = type == 1 ? 1 : type == 3 ? 3 : 2; ev->mods = mods_of(flags); handled = 1; break;
    case 2: case 4: case 26:
        ev->type = EV_MOUSE_UP; ev->x = x; ev->y = y; ev->button = type == 2 ? 1 : type == 4 ? 3 : 2; handled = 1; break;
    case 5: case 6: case 7: case 27:
        ev->type = EV_MOUSE_MOVE; ev->x = x; ev->y = y; handled = 1; break;
    case 22: { /* scroll wheel */
        double dy = SEND(double, 0, 0)(e, sel_("scrollingDeltaY"));
        ev->type = EV_WHEEL; ev->x = x; ev->y = y; ev->delta = dy > 0 ? 1 : dy < 0 ? -1 : 0; handled = ev->delta != 0; break;
    }
    case 10: { /* key down */
        unsigned short kc = SEND(unsigned short, 0, 0)(e, sel_("keyCode"));
        id chars = SEND(id, 0, 0)(e, sel_("charactersIgnoringModifiers"));
        const char *utf = chars ? SEND(const char *, 0, 0)(chars, sel_("UTF8String")) : "";
        ev->mods = mods_of(flags);
        ev->key = map_key(kc, utf);
        if (ev->key) { ev->type = EV_KEY; handled = 1; }
        else if (utf && utf[0] >= 32 && utf[0] < 127 && !utf[1]) { ev->type = EV_CHAR; ev->ch = utf[0]; handled = 1; }
        /* Cmd+Q quits */
        if ((flags & (1 << 20)) && utf && utf[0] == 'q') { ev->type = EV_QUIT; return 1; }
        if (handled) return 1;
        break;
    }
    }
    SEND(void, 0, 0, id)(app, sel_("sendEvent:"), e);
    if (want_quit) { ev->type = EV_QUIT; return 1; }
    if (handled) return 1;
    if (qh != qt) { *ev = queue[qh]; qh = (qh + 1) % 64; return 1; }
    return 0;
}

void win_present(const uint32_t *px, int w, int h)
{
    if (headless) { if (shot_path) bmp_write(shot_path, px, w, h); return; }
    fb = px; fb_w = w; fb_h = h;
    SEND(void, 0, 0, signed char)(view, sel_("setNeedsDisplay:"), 1);
    SEND(void, 0, 0)(view, sel_("displayIfNeeded"));
}

void win_close(void) { if (window) { SEND(void, 0, 0)(window, sel_("close")); window = NULL; } }

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
        for (int x = 0; x < w; x++) { uint32_t c = px[y * w + x]; row[x * 3] = c & 255; row[x * 3 + 1] = (c >> 8) & 255; row[x * 3 + 2] = (c >> 16) & 255; }
        fwrite(row, 1, stride, f);
    }
    free(row); fclose(f);
    return 0;
}
