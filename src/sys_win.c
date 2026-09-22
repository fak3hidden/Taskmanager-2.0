/* sys_win.c - Windows data collection (NtQuerySystemInformation, psapi, iphlpapi) */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <powerbase.h>
#include <sddl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tm.h"

typedef NTSTATUS (NTAPI *PFN_NtQSI)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
static PFN_NtQSI pNtQSI;
static LARGE_INTEGER qpf;

#define SystemProcessorPerformanceInformation_ 8
#define SystemProcessInformation_ 5

typedef struct {
    LARGE_INTEGER IdleTime, KernelTime, UserTime, DpcTime, InterruptTime;
    ULONG InterruptCount;
} SPPI;

typedef struct {
    ULONG Number; ULONG MaxMhz; ULONG CurrentMhz; ULONG MhzLimit; ULONG MaxIdleState; ULONG CurrentIdleState;
} PPI;

uint64_t sys_now_ns(void)
{
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e9 / (double)qpf.QuadPart);
}

int sys_username(uint32_t uid, char *buf, int n) { (void)uid; snprintf(buf, n, "?"); return -1; }

static void proc_owner(HANDLE h, char *out, int n)
{
    HANDLE tok; out[0] = 0;
    if (!OpenProcessToken(h, TOKEN_QUERY, &tok)) return;
    BYTE tb[512]; DWORD len = 0;
    if (GetTokenInformation(tok, TokenUser, tb, sizeof tb, &len)) {
        char name[64], dom[64]; DWORD nn = 64, nd = 64; SID_NAME_USE use;
        if (LookupAccountSidA(NULL, ((TOKEN_USER *)tb)->User.Sid, name, &nn, dom, &nd, &use))
            snprintf(out, n, "%s", name);
    }
    CloseHandle(tok);
}

int sys_init(Sys *s)
{
    memset(s, 0, sizeof *s);
    QueryPerformanceFrequency(&qpf);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    pNtQSI = (PFN_NtQSI)(void *)GetProcAddress(nt, "NtQuerySystemInformation");

    SYSTEM_INFO si; GetSystemInfo(&si);
    s->ncpu = (int)si.dwNumberOfProcessors;
    if (s->ncpu > MAX_CPUS) s->ncpu = MAX_CPUS;

    /* physical cores / sockets */
    DWORD len = 0; GetLogicalProcessorInformation(NULL, &len);
    if (len) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION *info = malloc(len);
        if (GetLogicalProcessorInformation(info, &len)) {
            int cnt = (int)(len / sizeof *info);
            for (int i = 0; i < cnt; i++) {
                if (info[i].Relationship == RelationProcessorCore) s->cores++;
                if (info[i].Relationship == RelationProcessorPackage) s->sockets++;
            }
        }
        free(info);
    }
    if (!s->cores) s->cores = s->ncpu;
    if (!s->sockets) s->sockets = 1;

    HKEY k;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &k) == 0) {
        DWORD sz = sizeof s->cpu_model, mhz = 0, msz = sizeof mhz;
        RegQueryValueExA(k, "ProcessorNameString", NULL, NULL, (BYTE *)s->cpu_model, &sz);
        if (RegQueryValueExA(k, "~MHz", NULL, NULL, (BYTE *)&mhz, &msz) == 0) s->base_mhz = mhz;
        RegCloseKey(k);
        /* trim leading spaces */
        char *p = s->cpu_model; while (*p == ' ') p++;
        if (p != s->cpu_model) memmove(s->cpu_model, p, strlen(p) + 1);
    }
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &k) == 0) {
        char prod[64] = "", build[16] = ""; DWORD a = sizeof prod, b = sizeof build;
        RegQueryValueExA(k, "ProductName", NULL, NULL, (BYTE *)prod, &a);
        RegQueryValueExA(k, "CurrentBuildNumber", NULL, NULL, (BYTE *)build, &b);
        snprintf(s->os, sizeof s->os, "%s (build %s)", prod, build);
        RegCloseKey(k);
    }
    DWORD hn = sizeof s->host; GetComputerNameA(s->host, &hn);
    sys_sample(s);
    return 0;
}

static void sample_cpu(Sys *s)
{
    if (!pNtQSI) return;
    static SPPI sp[MAX_CPUS]; ULONG got = 0;
    if (pNtQSI((SYSTEM_INFORMATION_CLASS)SystemProcessorPerformanceInformation_, sp, sizeof sp, &got) != 0) return;
    int n = (int)(got / sizeof(SPPI)); if (n > s->ncpu) n = s->ncpu;
    uint64_t tb = 0, tt = 0;
    for (int i = 0; i < n; i++) {
        uint64_t idle = sp[i].IdleTime.QuadPart;
        uint64_t total = sp[i].KernelTime.QuadPart + sp[i].UserTime.QuadPart; /* kernel includes idle */
        s->cpu_total[i + 1] = total; s->cpu_busy[i + 1] = total - idle;
        tb += total - idle; tt += total;
    }
    s->cpu_total[0] = tt; s->cpu_busy[0] = tb;

    static PPI ppi[MAX_CPUS];
    if (CallNtPowerInformation(ProcessorInformation, NULL, 0, ppi, sizeof(PPI) * s->ncpu) == 0) {
        s->mhz = ppi[0].CurrentMhz;
        if (!s->base_mhz) s->base_mhz = ppi[0].MaxMhz;
    }
}

static void sample_mem(Sys *s)
{
    MEMORYSTATUSEX m; memset(&m, 0, sizeof m); m.dwLength = sizeof m;
    if (GlobalMemoryStatusEx(&m)) {
        s->mem_total = m.ullTotalPhys; s->mem_avail = m.ullAvailPhys;
        s->mem_used = m.ullTotalPhys - m.ullAvailPhys;
        s->mem_commit_limit = m.ullTotalPageFile;
        s->mem_committed = m.ullTotalPageFile - m.ullAvailPageFile;
        s->swap_total = m.ullTotalPageFile > m.ullTotalPhys ? m.ullTotalPageFile - m.ullTotalPhys : 0;
        s->swap_used = s->mem_committed > s->mem_used ? s->mem_committed - s->mem_used : 0;
    }
    PERFORMANCE_INFORMATION pi; memset(&pi, 0, sizeof pi); pi.cb = sizeof pi;
    if (GetPerformanceInfo(&pi, sizeof pi)) {
        s->mem_cached = (uint64_t)pi.SystemCache * pi.PageSize;
        s->nhandles = (int)pi.HandleCount; s->nthreads = (int)pi.ThreadCount; s->nproc = (int)pi.ProcessCount;
    }
}

static void sample_disk(Sys *s)
{
    uint64_t rd = 0, wr = 0;
    for (int i = 0; i < 16; i++) {
        char path[32]; snprintf(path, sizeof path, "\\\\.\\PhysicalDrive%d", i);
        HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) { if (i > 3) break; continue; }
        DISK_PERFORMANCE dp; DWORD got;
        if (DeviceIoControl(h, IOCTL_DISK_PERFORMANCE, NULL, 0, &dp, sizeof dp, &got, NULL)) {
            rd += dp.BytesRead.QuadPart; wr += dp.BytesWritten.QuadPart;
        }
        CloseHandle(h);
    }
    s->disk_rd = rd; s->disk_wr = wr;
}

static void sample_net(Sys *s)
{
    MIB_IF_TABLE2 *t = NULL;
    if (GetIfTable2(&t) != NO_ERROR || !t) return;
    uint64_t rx = 0, tx = 0;
    for (ULONG i = 0; i < t->NumEntries; i++) {
        MIB_IF_ROW2 *r = &t->Table[i];
        if (r->Type == IF_TYPE_SOFTWARE_LOOPBACK || r->OperStatus != IfOperStatusUp) continue;
        if (r->Type != IF_TYPE_ETHERNET_CSMACD && r->Type != IF_TYPE_IEEE80211) continue;
        rx += r->InOctets; tx += r->OutOctets;
    }
    FreeMibTable(t);
    s->net_rx = rx; s->net_tx = tx;
}

void sys_sample(Sys *s)
{
    s->t_ns = sys_now_ns();
    sample_cpu(s); sample_mem(s); sample_disk(s); sample_net(s);
    s->uptime = GetTickCount64() / 1000;
}

int sys_procs(Proc **arr, int *n, int *cap)
{
    if (!pNtQSI) return -1;
    static BYTE *buf; static ULONG bufsz;
    ULONG need = 0; NTSTATUS st;
    if (!buf) { bufsz = 1 << 20; buf = malloc(bufsz); }
    for (;;) {
        st = pNtQSI((SYSTEM_INFORMATION_CLASS)SystemProcessInformation_, buf, bufsz, &need);
        if (st == (NTSTATUS)0xC0000004L) { bufsz = need + 65536; buf = realloc(buf, bufsz); continue; }
        if (st != 0) return -1;
        break;
    }
    *n = 0;
    BYTE *p = buf;
    for (;;) {
        SYSTEM_PROCESS_INFORMATION *spi = (SYSTEM_PROCESS_INFORMATION *)p;
        if (*n >= *cap) { *cap = *cap ? *cap * 2 : 256; *arr = realloc(*arr, *cap * sizeof(Proc)); }
        Proc *pr = &(*arr)[*n]; memset(pr, 0, sizeof *pr);
        pr->pid = (int)(uintptr_t)spi->UniqueProcessId;
        pr->ppid = (int)(uintptr_t)spi->InheritedFromUniqueProcessId;
        pr->threads = (int)spi->NumberOfThreads;
        pr->handles = (int)spi->HandleCount;
        pr->prio = (int)spi->BasePriority;
        pr->cpu_time = (uint64_t)(spi->UserTime.QuadPart + spi->KernelTime.QuadPart) * 100; /* 100ns -> ns */
        pr->rss = spi->VirtualMemoryCounters.WorkingSetSize;
        pr->vsz = spi->PrivatePageCount;   /* "commit size" as in taskmgr */
        pr->rd = spi->IoCounters.ReadTransferCount;
        pr->wr = spi->IoCounters.WriteTransferCount;
        pr->start = (uint64_t)spi->CreateTime.QuadPart;
        pr->state = 'R';
        if (spi->ImageName.Buffer && spi->ImageName.Length)
            WideCharToMultiByte(CP_UTF8, 0, spi->ImageName.Buffer, spi->ImageName.Length / 2, pr->name, sizeof pr->name - 1, NULL, NULL);
        else
            snprintf(pr->name, sizeof pr->name, pr->pid == 0 ? "System Idle Process" : "System");
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pr->pid);
        if (h) {
            DWORD sz = sizeof pr->cmd - 1;
            if (!QueryFullProcessImageNameA(h, 0, pr->cmd, &sz)) pr->cmd[0] = 0;
            proc_owner(h, pr->user, sizeof pr->user);
            CloseHandle(h);
        }
        if (!pr->user[0]) snprintf(pr->user, sizeof pr->user, "SYSTEM");
        if (!pr->cmd[0]) snprintf(pr->cmd, sizeof pr->cmd, "%s", pr->name);
        (*n)++;
        if (!spi->NextEntryOffset) break;
        p += spi->NextEntryOffset;
    }
    return 0;
}

int sys_theme_dark(void)
{
    const char *force = getenv("TM_THEME");
    if (force && (!strcmp(force, "dark") || !strcmp(force, "1"))) return 1;
    if (force && (!strcmp(force, "light") || !strcmp(force, "0"))) return 0;
    HKEY k; DWORD v = 1, n = sizeof v, type = 0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &k) == 0) {
        if (RegQueryValueExA(k, "AppsUseLightTheme", NULL, &type, (BYTE *)&v, &n) == ERROR_SUCCESS) { RegCloseKey(k); return v == 0; }
        RegCloseKey(k);
    }
    return 0;
}

int sys_self_pid(void) { return (int)GetCurrentProcessId(); }

int sys_spawn(const char *cmdline)
{
    /* CreateProcess handles "prog args"; fall back to ShellExecute for documents / folders / URLs */
    char cmd[1024]; snprintf(cmd, sizeof cmd, "%s", cmdline);
    STARTUPINFOA si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return 0;
    }
    return (intptr_t)ShellExecuteA(NULL, "open", cmdline, NULL, NULL, SW_SHOWNORMAL) > 32 ? 0 : -1;
}

int sys_kill(int pid, uint64_t start)
{
    if (pid <= 4 || pid == (int)GetCurrentProcessId()) return -1;   /* Idle / System / ourselves */
    HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return -1;
    if (start) {
        /* the pid must still belong to the process the user was shown (Windows recycles pids fast) */
        FILETIME c, e, k, u;
        if (!GetProcessTimes(h, &c, &e, &k, &u)) { CloseHandle(h); return -2; }
        uint64_t ct = ((uint64_t)c.dwHighDateTime << 32) | c.dwLowDateTime;
        if (ct != start) { CloseHandle(h); return -2; }
    }
    int ok = TerminateProcess(h, 1) ? 0 : -1;
    CloseHandle(h);
    return ok;
}

/* ---- application icons: extract from the executable ----------------------- */
int os_icon_load(const Proc *p, int size, uint32_t *out)
{
    if (p->pid <= 4 || !strchr(p->cmd, '\\')) return 0;
    HICON hi = NULL;
    int want_big = size > 20;
    UINT n = ExtractIconExA(p->cmd, 0, want_big ? &hi : NULL, want_big ? NULL : &hi, 1);
    if (n == 0 || !hi) {
        SHFILEINFOA sfi; memset(&sfi, 0, sizeof sfi);
        if (!SHGetFileInfoA(p->cmd, 0, &sfi, sizeof sfi, SHGFI_ICON | (want_big ? SHGFI_LARGEICON : SHGFI_SMALLICON))) return 0;
        hi = sfi.hIcon;
    }
    ICONINFO ii; if (!GetIconInfo(hi, &ii)) { DestroyIcon(hi); return 0; }
    BITMAP bm; GetObject(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof bm, &bm);
    int w = bm.bmWidth, h = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
    if (w <= 0 || h <= 0 || w > 256 || h > 256) { DeleteObject(ii.hbmColor); DeleteObject(ii.hbmMask); DestroyIcon(hi); return 0; }
    uint32_t *px = malloc((size_t)w * h * 4), *mask = malloc((size_t)w * h * 4);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(NULL);
    int ok = 0;
    if (ii.hbmColor && GetDIBits(dc, ii.hbmColor, 0, h, px, &bi, DIB_RGB_COLORS) == h) {
        int has_alpha = 0; for (int i = 0; i < w * h; i++) if (px[i] >> 24) { has_alpha = 1; break; }
        if (!has_alpha && ii.hbmMask && GetDIBits(dc, ii.hbmMask, 0, h, mask, &bi, DIB_RGB_COLORS) == h)
            for (int i = 0; i < w * h; i++) px[i] = (mask[i] & 0xffffff) ? (px[i] & 0xffffff) : (px[i] | 0xff000000);
        else if (!has_alpha) for (int i = 0; i < w * h; i++) px[i] |= 0xff000000;
        icon_scale(px, w, h, out, size); ok = 1;
    }
    ReleaseDC(NULL, dc);
    free(px); free(mask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor); if (ii.hbmMask) DeleteObject(ii.hbmMask);
    DestroyIcon(hi);
    return ok;
}
