/* win_w32.c - minimal Win32 window (GDI StretchDIBits blit) */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tm.h"

static HWND hwnd; static int cur_w, cur_h;
static const uint32_t *fb; static int fb_w, fb_h;
static Event queue[64]; static int qh, qt;
static int headless; static const char *shot_path; static int shot_w, shot_h, shot_frames;

void headless_config(const char *bmp_path, int w, int h) { headless = 1; shot_path = bmp_path; shot_w = w; shot_h = h; }

static void push(Event *e) { int n = (qt + 1) % 64; if (n == qh) return; queue[qt] = *e; qt = n; }

static int map_vk(WPARAM vk)
{
    switch (vk) {
    case VK_UP: return K_UP; case VK_DOWN: return K_DOWN; case VK_LEFT: return K_LEFT; case VK_RIGHT: return K_RIGHT;
    case VK_PRIOR: return K_PGUP; case VK_NEXT: return K_PGDN; case VK_HOME: return K_HOME; case VK_END: return K_END;
    case VK_DELETE: return K_DEL; case VK_BACK: return K_BS; case VK_RETURN: return K_ENTER; case VK_ESCAPE: return K_ESC;
    case VK_TAB: return K_TAB;
    }
    if (vk >= VK_F1 && vk <= VK_F12) return K_F1 + (int)(vk - VK_F1);
    return 0;
}

static int mods_now(void)
{
    return ((GetKeyState(VK_SHIFT) & 0x8000) ? KM_SHIFT : 0) | ((GetKeyState(VK_CONTROL) & 0x8000) ? KM_CTRL : 0) |
           ((GetKeyState(VK_MENU) & 0x8000) ? KM_ALT : 0);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    Event e; memset(&e, 0, sizeof e);
    switch (msg) {
    case WM_CLOSE: e.type = EV_QUIT; push(&e); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SIZE:
        cur_w = LOWORD(lp); cur_h = HIWORD(lp);
        if (cur_w && cur_h) { e.type = EV_RESIZE; e.w = cur_w; e.h = cur_h; push(&e); }
        return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *mm = (MINMAXINFO *)lp; mm->ptMinTrackSize.x = 480; mm->ptMinTrackSize.y = 320; return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        if (fb) {
            BITMAPINFO bi; memset(&bi, 0, sizeof bi);
            bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = fb_w; bi.bmiHeader.biHeight = -fb_h;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            SetDIBitsToDevice(dc, 0, 0, fb_w, fb_h, 0, 0, 0, fb_h, fb, &bi, DIB_RGB_COLORS);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        int k = map_vk(wp);
        if (k) { e.type = EV_KEY; e.key = k; e.mods = mods_now(); push(&e); return 0; }
        if (mods_now() & KM_CTRL) { int c = MapVirtualKeyA((UINT)wp, MAPVK_VK_TO_CHAR); if (c >= 'A' && c <= 'Z') { e.type = EV_CHAR; e.ch = c + 32; e.mods = KM_CTRL; push(&e); return 0; } }
        break;
    }
    case WM_CHAR:
        if (wp >= 32 && wp < 127) { e.type = EV_CHAR; e.ch = (int)wp; e.mods = mods_now(); push(&e); }
        return 0;
    case WM_MOUSEMOVE: e.type = EV_MOUSE_MOVE; e.x = GET_X_LPARAM(lp); e.y = GET_Y_LPARAM(lp); push(&e); return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        SetCapture(h);
        e.type = EV_MOUSE_DOWN; e.x = GET_X_LPARAM(lp); e.y = GET_Y_LPARAM(lp);
        e.button = msg == WM_LBUTTONDOWN ? 1 : msg == WM_MBUTTONDOWN ? 2 : 3; e.mods = mods_now(); push(&e); return 0;
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
        ReleaseCapture();
        e.type = EV_MOUSE_UP; e.x = GET_X_LPARAM(lp); e.y = GET_Y_LPARAM(lp);
        e.button = msg == WM_LBUTTONUP ? 1 : msg == WM_MBUTTONUP ? 2 : 3; push(&e); return 0;
    case WM_LBUTTONDBLCLK:
        e.type = EV_MOUSE_DOWN; e.x = GET_X_LPARAM(lp); e.y = GET_Y_LPARAM(lp); e.button = 1; push(&e); return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &pt);
        e.type = EV_WHEEL; e.x = pt.x; e.y = pt.y; e.delta = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA; push(&e); return 0;
    }
    }
    return DefWindowProcA(h, msg, wp, lp);
}

int win_open(const char *title, int w, int h, int *scale)
{
    if (headless) { cur_w = shot_w; cur_h = shot_h; return 0; }
    typedef BOOL (WINAPI *PFN_SPDA)(void);
    HMODULE u32 = GetModuleHandleA("user32.dll");
    PFN_SPDA spda = (PFN_SPDA)(void *)GetProcAddress(u32, "SetProcessDPIAware");
    if (spda) spda();
    if (scale && *scale <= 0) {
        HDC dc = GetDC(NULL); int dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(NULL, dc);
        *scale = dpi >= 168 ? 2 : 1;
    }
    if (scale && *scale > 1) { w *= *scale; h *= *scale; }

    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "TaskManager2";
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW); wc.style = CS_DBLCLKS;
    wc.hIcon = LoadIconA(GetModuleHandleA(NULL), (LPCSTR)1);
    RegisterClassA(&wc);
    RECT r = { 0, 0, w, h };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowExA(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return -1;
    RECT c; GetClientRect(hwnd, &c); cur_w = c.right; cur_h = c.bottom;
    Event e; memset(&e, 0, sizeof e); e.type = EV_RESIZE; e.w = cur_w; e.h = cur_h; push(&e);
    return 0;
}

int win_poll(Event *ev, int timeout_ms)
{
    memset(ev, 0, sizeof *ev);
    if (headless) {
        if (shot_frames == 0) { ev->type = EV_RESIZE; ev->w = cur_w; ev->h = cur_h; shot_frames++; return 1; }
        if (timeout_ms > 0) Sleep(timeout_ms);
        return 0;
    }
    MSG m;
    if (qh == qt && timeout_ms > 0) MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)timeout_ms, QS_ALLINPUT);
    while (qh == qt && PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) { ev->type = EV_QUIT; return 1; }
        TranslateMessage(&m); DispatchMessageA(&m);
    }
    if (qh != qt) { *ev = queue[qh]; qh = (qh + 1) % 64; return 1; }
    return 0;
}

void win_present(const uint32_t *px, int w, int h)
{
    if (headless) { if (shot_path) bmp_write(shot_path, px, w, h); return; }
    fb = px; fb_w = w; fb_h = h;
    HDC dc = GetDC(hwnd);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0, 0, w, h, 0, 0, 0, h, px, &bi, DIB_RGB_COLORS);
    ReleaseDC(hwnd, dc);
}

void win_close(void) { if (hwnd) { DestroyWindow(hwnd); hwnd = NULL; } }

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
