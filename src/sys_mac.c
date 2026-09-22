/* sys_mac.c - macOS data collection (libproc, mach host, sysctl, IOKit-free) */
#include "tm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pwd.h>
#include <time.h>
#include <unistd.h>
#include <libproc.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_var.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <mach/vm_statistics.h>

static mach_port_t host;
static vm_size_t pagesz;

uint64_t sys_now_ns(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

int sys_username(uint32_t uid, char *buf, int n)
{
    static uint32_t cache_uid[64]; static char cache_name[64][32]; static int nc;
    for (int i = 0; i < nc; i++) if (cache_uid[i] == uid) { snprintf(buf, n, "%s", cache_name[i]); return 0; }
    struct passwd *pw = getpwuid(uid);
    snprintf(buf, n, "%s", pw ? pw->pw_name : "?");
    if (!pw) { snprintf(buf, n, "%u", uid); return -1; }
    if (nc < 64) { cache_uid[nc] = uid; snprintf(cache_name[nc], 32, "%s", pw->pw_name); nc++; }
    return 0;
}

static int sysctl_str(const char *name, char *out, size_t n) { return sysctlbyname(name, out, &n, NULL, 0); }
static uint64_t sysctl_u64(const char *name) { uint64_t v = 0; size_t n = sizeof v; sysctlbyname(name, &v, &n, NULL, 0); return v; }

int sys_init(Sys *s)
{
    memset(s, 0, sizeof *s);
    host = mach_host_self();
    host_page_size(host, &pagesz);
    s->ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (s->ncpu > MAX_CPUS) s->ncpu = MAX_CPUS;
    s->cores = (int)sysctl_u64("hw.physicalcpu"); if (!s->cores) s->cores = s->ncpu;
    s->sockets = (int)sysctl_u64("hw.packages"); if (!s->sockets) s->sockets = 1;
    sysctl_str("machdep.cpu.brand_string", s->cpu_model, sizeof s->cpu_model);
    s->base_mhz = sysctl_u64("hw.cpufrequency") / 1e6;
    s->mem_total = sysctl_u64("hw.memsize");
    char ver[32] = ""; sysctl_str("kern.osproductversion", ver, sizeof ver);
    struct utsname u; uname(&u);
    snprintf(s->os, sizeof s->os, "macOS %s (%s)", ver[0] ? ver : "?", u.release);
    snprintf(s->host, sizeof s->host, "%s", u.nodename);
    sys_sample(s);
    return 0;
}

static void sample_cpu(Sys *s)
{
    natural_t ncpu; processor_info_array_t info; mach_msg_type_number_t cnt;
    if (host_processor_info(host, PROCESSOR_CPU_LOAD_INFO, &ncpu, &info, &cnt) != KERN_SUCCESS) return;
    processor_cpu_load_info_t li = (processor_cpu_load_info_t)info;
    uint64_t tb = 0, tt = 0;
    for (natural_t i = 0; i < ncpu && i < (natural_t)s->ncpu; i++) {
        uint64_t u = li[i].cpu_ticks[CPU_STATE_USER], sy = li[i].cpu_ticks[CPU_STATE_SYSTEM];
        uint64_t n = li[i].cpu_ticks[CPU_STATE_NICE], id = li[i].cpu_ticks[CPU_STATE_IDLE];
        s->cpu_busy[i + 1] = u + sy + n; s->cpu_total[i + 1] = u + sy + n + id;
        tb += u + sy + n; tt += u + sy + n + id;
    }
    s->cpu_busy[0] = tb; s->cpu_total[0] = tt;
    vm_deallocate(mach_task_self(), (vm_address_t)info, cnt * sizeof(int));
    s->mhz = s->base_mhz;
}

static void sample_mem(Sys *s)
{
    vm_statistics64_data_t vm; mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm, &cnt) == KERN_SUCCESS) {
        uint64_t free = (uint64_t)(vm.free_count - vm.speculative_count) * pagesz;
        uint64_t cached = (uint64_t)(vm.external_page_count + vm.purgeable_count) * pagesz;
        uint64_t used = (uint64_t)(vm.internal_page_count + vm.wire_count + vm.compressor_page_count) * pagesz;
        s->mem_used = used; s->mem_cached = cached;
        s->mem_avail = s->mem_total > used ? s->mem_total - used : free;
    }
    struct xsw_usage sw; size_t n = sizeof sw;
    if (sysctlbyname("vm.swapusage", &sw, &n, NULL, 0) == 0) { s->swap_total = sw.xsu_total; s->swap_used = sw.xsu_used; }
    s->mem_committed = s->mem_used + s->swap_used;
    s->mem_commit_limit = s->mem_total + s->swap_total;
}

static void sample_net(Sys *s)
{
    struct ifaddrs *ifa0;
    if (getifaddrs(&ifa0)) return;
    uint64_t rx = 0, tx = 0;
    for (struct ifaddrs *ifa = ifa0; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK || !ifa->ifa_data) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        struct if_data *d = ifa->ifa_data;
        rx += d->ifi_ibytes; tx += d->ifi_obytes;
    }
    freeifaddrs(ifa0);
    s->net_rx = rx; s->net_tx = tx;
}

void sys_sample(Sys *s)
{
    s->t_ns = sys_now_ns();
    sample_cpu(s); sample_mem(s); sample_net(s);
    struct timeval bt; size_t n = sizeof bt; int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    if (sysctl(mib, 2, &bt, &n, NULL, 0) == 0) s->uptime = (uint64_t)(time(NULL) - bt.tv_sec);
    double ld[3]; if (getloadavg(ld, 3) == 3) { s->load[0] = ld[0]; s->load[1] = ld[1]; s->load[2] = ld[2]; }
    /* disk: no cheap API without IOKit; approximate via per-process rusage totals */
    uint64_t rd = 0, wr = 0; static int *pids; static int npids;
    int need = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    if (need > npids * (int)sizeof(int)) { npids = need / sizeof(int) + 64; pids = realloc(pids, npids * sizeof(int)); }
    int got = proc_listpids(PROC_ALL_PIDS, 0, pids, npids * sizeof(int)) / sizeof(int);
    for (int i = 0; i < got; i++) {
        struct rusage_info_v2 ru;
        if (pids[i] && proc_pid_rusage(pids[i], RUSAGE_INFO_V2, (rusage_info_t *)&ru) == 0) { rd += ru.ri_diskio_bytesread; wr += ru.ri_diskio_byteswritten; }
    }
    /* monotonic only while processes live; good enough for a rate graph */
    if (rd >= s->disk_rd) s->disk_rd = rd;
    if (wr >= s->disk_wr) s->disk_wr = wr;
}

int sys_procs(Proc **arr, int *n, int *cap)
{
    static int *pids; static int npids;
    int need = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    if (need <= 0) return -1;
    if (need > npids * (int)sizeof(int)) { npids = need / sizeof(int) + 64; pids = realloc(pids, npids * sizeof(int)); }
    int got = proc_listpids(PROC_ALL_PIDS, 0, pids, npids * sizeof(int)) / sizeof(int);
    *n = 0;
    for (int i = 0; i < got; i++) {
        int pid = pids[i]; if (pid <= 0) continue;
        struct proc_taskallinfo ti;
        if (proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0, &ti, sizeof ti) != (int)sizeof ti) continue;
        if (*n >= *cap) { *cap = *cap ? *cap * 2 : 256; *arr = realloc(*arr, *cap * sizeof(Proc)); }
        Proc *p = &(*arr)[*n]; memset(p, 0, sizeof *p);
        p->pid = pid; p->ppid = (int)ti.pbsd.pbi_ppid;
        snprintf(p->name, sizeof p->name, "%s", ti.pbsd.pbi_name[0] ? ti.pbsd.pbi_name : ti.pbsd.pbi_comm);
        sys_username(ti.pbsd.pbi_uid, p->user, sizeof p->user);
        static const char st[] = "?IRSTZ";  /* SIDL SRUN SSLEEP SSTOP SZOMB */
        p->state = ti.pbsd.pbi_status < 6 ? st[ti.pbsd.pbi_status] : '?';
        p->nice = ti.pbsd.pbi_nice; p->prio = ti.ptinfo.pti_priority;
        p->threads = ti.ptinfo.pti_threadnum;
        p->cpu_time = ti.ptinfo.pti_total_user + ti.ptinfo.pti_total_system;  /* already ns (mach abs ~ns) */
        p->rss = ti.ptinfo.pti_resident_size; p->vsz = ti.ptinfo.pti_virtual_size;
        p->start = ti.pbsd.pbi_start_tvsec;
        struct rusage_info_v2 ru;
        if (proc_pid_rusage(pid, RUSAGE_INFO_V2, (rusage_info_t *)&ru) == 0) { p->rd = ru.ri_diskio_bytesread; p->wr = ru.ri_diskio_byteswritten; }
        char path[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(pid, path, sizeof path) > 0) snprintf(p->cmd, sizeof p->cmd, "%s", path);
        else snprintf(p->cmd, sizeof p->cmd, "%s", p->name);
        /* open file count */
        int fdsz = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, NULL, 0);
        p->handles = fdsz > 0 ? fdsz / (int)sizeof(struct proc_fdinfo) : 0;
        (*n)++;
    }
    return 0;
}

int sys_theme_dark(void)
{
    const char *force = getenv("TM_THEME");
    if (force && (!strcasecmp(force, "dark") || !strcasecmp(force, "1"))) return 1;
    if (force && (!strcasecmp(force, "light") || !strcasecmp(force, "0"))) return 0;
    FILE *f = popen("defaults read -g AppleInterfaceStyle 2>/dev/null", "r");
    if (!f) return 0;
    char buf[64] = ""; fgets(buf, sizeof buf, f); pclose(f);
    return strcasestr(buf, "dark") != NULL;
}

int sys_self_pid(void) { return (int)getpid(); }

int sys_spawn(const char *cmdline)
{
    pid_t c = fork();
    if (c < 0) return -1;
    if (c == 0) {
        setsid(); if (fork() != 0) _exit(0);
        int fd = open("/dev/null", O_RDWR); if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
        /* "open -a Name" for app bundles, else a shell command line */
        if (!strchr(cmdline, '/') && !strchr(cmdline, ' ') && !strchr(cmdline, '.')) execl("/usr/bin/open", "open", "-a", cmdline, (char *)NULL);
        execl("/bin/sh", "sh", "-c", cmdline, (char *)NULL); _exit(127);
    }
    int st; waitpid(c, &st, 0);
    return 0;
}
int sys_kill(int pid, uint64_t start)
{
    if (pid <= 1 || pid == getpid()) return -1;
    if (start) {
        struct proc_taskallinfo ti;
        if (proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0, &ti, sizeof ti) != (int)sizeof ti) return -2;
        if ((uint64_t)ti.pbsd.pbi_start_tvsec != start) return -2;   /* pid was reused */
    }
    if (kill(pid, SIGTERM) == 0) return 0;
    return kill(pid, SIGKILL);
}

/* ---- application icons: NSWorkspace iconForFile: via the objc runtime ------- */
#include <dlfcn.h>
typedef void *id, *SEL;
typedef struct { double x, y; } CGPoint_; typedef struct { double w, h; } CGSize_; typedef struct { CGPoint_ o; CGSize_ s; } CGRect_;

int os_icon_load(const Proc *p, int size, uint32_t *out)
{
    static int tried; static id (*getClass)(const char *); static SEL (*sel)(const char *); static void *send;
    static void *(*CGBitmapContextCreate_)(void *, size_t, size_t, size_t, size_t, void *, uint32_t);
    static void *(*CGColorSpaceCreateDeviceRGB_)(void); static void (*CGContextRelease_)(void *);
    static void (*CGContextDrawImage_)(void *, CGRect_, void *);
    if (!tried) {
        tried = 1;
        void *objc = dlopen("/usr/lib/libobjc.A.dylib", RTLD_NOW);
        void *ak = dlopen("/System/Library/Frameworks/AppKit.framework/AppKit", RTLD_NOW);
        void *cg = dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_NOW);
        if (!objc || !ak || !cg) return 0;
        *(void **)&getClass = dlsym(objc, "objc_getClass"); *(void **)&sel = dlsym(objc, "sel_registerName"); send = dlsym(objc, "objc_msgSend");
        *(void **)&CGBitmapContextCreate_ = dlsym(cg, "CGBitmapContextCreate"); *(void **)&CGColorSpaceCreateDeviceRGB_ = dlsym(cg, "CGColorSpaceCreateDeviceRGB");
        *(void **)&CGContextRelease_ = dlsym(cg, "CGContextRelease"); *(void **)&CGContextDrawImage_ = dlsym(cg, "CGContextDrawImage");
    }
    if (!getClass || !CGBitmapContextCreate_ || p->cmd[0] != '/') return 0;
    /* find the enclosing .app bundle if any, else use the executable path */
    char path[512]; snprintf(path, sizeof path, "%s", p->cmd);
    char *app = strstr(path, ".app/"); if (app) app[4] = 0;
    else return 0;   /* plain unix binaries have no icon; use generic */
    id ws = ((id (*)(id, SEL))send)(getClass("NSWorkspace"), sel("sharedWorkspace"));
    id nspath = ((id (*)(id, SEL, const char *))send)(getClass("NSString"), sel("stringWithUTF8String:"), path);
    id img = ((id (*)(id, SEL, id))send)(ws, sel("iconForFile:"), nspath);
    if (!img) return 0;
    CGRect_ r = { { 0, 0 }, { (double)size, (double)size } };
    void *cs = CGColorSpaceCreateDeviceRGB_();
    /* kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little = 0x2002 -> BGRA in memory = ARGB uint32 LE */
    void *ctx = CGBitmapContextCreate_(out, size, size, 8, size * 4, cs, 0x2002);
    if (!ctx) return 0;
    memset(out, 0, (size_t)size * size * 4);
    void *cgimg = ((void *(*)(id, SEL, CGRect_ *, id, id))send)(img, sel("CGImageForProposedRect:context:hints:"), &r, NULL, NULL);
    if (cgimg) CGContextDrawImage_(ctx, r, cgimg);
    CGContextRelease_(ctx);
    /* un-premultiply */
    for (int i = 0; i < size * size; i++) {
        uint32_t px = out[i]; unsigned a = px >> 24; if (!a || a == 255) continue;
        unsigned rr = ((px >> 16) & 255) * 255 / a, gg = ((px >> 8) & 255) * 255 / a, bb = (px & 255) * 255 / a;
        out[i] = (a << 24) | (rr > 255 ? 255 : rr) << 16 | (gg > 255 ? 255 : gg) << 8 | (bb > 255 ? 255 : bb);
    }
    return cgimg != NULL;
}
