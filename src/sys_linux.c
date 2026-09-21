/* sys_linux.c - collect process/system data from /proc and /sys */
#include "tm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>

static long clk_tck = 100;
static long page = 4096;
static uint64_t boot_time;

uint64_t sys_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static int read_file(const char *path, char *buf, int n)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int r = (int)fread(buf, 1, n - 1, f);
    fclose(f);
    if (r < 0) r = 0;
    buf[r] = 0;
    return r;
}

/* tiny uid -> name cache (parses /etc/passwd once) */
typedef struct { uint32_t uid; char name[32]; } UserEnt;
static UserEnt *users; static int nusers;

static void load_users(void)
{
    FILE *f = fopen("/etc/passwd", "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *name = strtok(line, ":"); strtok(NULL, ":");
        char *uid = strtok(NULL, ":");
        if (!name || !uid) continue;
        users = realloc(users, (nusers + 1) * sizeof *users);
        users[nusers].uid = (uint32_t)atoi(uid);
        snprintf(users[nusers].name, 32, "%s", name);
        nusers++;
    }
    fclose(f);
}

int sys_username(uint32_t uid, char *buf, int n)
{
    for (int i = 0; i < nusers; i++)
        if (users[i].uid == uid) { snprintf(buf, n, "%s", users[i].name); return 0; }
    snprintf(buf, n, "%u", uid);
    return -1;
}

int sys_init(Sys *s)
{
    memset(s, 0, sizeof *s);
    clk_tck = sysconf(_SC_CLK_TCK);
    page = sysconf(_SC_PAGESIZE);
    load_users();

    struct utsname u;
    if (uname(&u) == 0) {
        snprintf(s->os, sizeof s->os, "%s %s", u.sysname, u.release);
        snprintf(s->host, sizeof s->host, "%s", u.nodename);
    }
    /* pretty OS name */
    char buf[4096];
    if (read_file("/etc/os-release", buf, sizeof buf) > 0) {
        char *p = strstr(buf, "PRETTY_NAME=\"");
        if (p) {
            p += 13; char *e = strchr(p, '"');
            if (e) { *e = 0; snprintf(s->os, sizeof s->os, "%s", p); }
        }
    }
    /* cpu model / topology */
    if (read_file("/proc/cpuinfo", buf, sizeof buf) > 0) {
        char *p = strstr(buf, "model name");
        if (!p) p = strstr(buf, "Hardware");
        if (!p) p = strstr(buf, "Processor");
        if (p) {
            p = strchr(p, ':');
            if (p) {
                p++; while (*p == ' ') p++;
                char *e = strchr(p, '\n'); if (e) *e = 0;
                snprintf(s->cpu_model, sizeof s->cpu_model, "%s", p);
            }
        }
        p = strstr(buf, "cpu cores");
        if (p && (p = strchr(p, ':'))) s->cores = atoi(p + 1);
        p = strstr(buf, "cpu MHz");
        if (p && (p = strchr(p, ':'))) s->base_mhz = atof(p + 1);
    }
    s->ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (s->ncpu > MAX_CPUS) s->ncpu = MAX_CPUS;
    if (s->cores == 0) s->cores = s->ncpu;
    s->sockets = 1;
    if (read_file("/proc/stat", buf, sizeof buf) > 0) {
        char *p = strstr(buf, "btime ");
        if (p) boot_time = strtoull(p + 6, NULL, 10);
    }
    sys_sample(s);
    return 0;
}

static void sample_cpu(Sys *s)
{
    static char buf[65536];
    if (read_file("/proc/stat", buf, sizeof buf) <= 0) return;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (!strncmp(line, "cpu", 3)) {
            int idx = 0;
            const char *p = line + 3;
            if (*p == ' ') idx = 0; else idx = atoi(p) + 1;
            if (idx <= MAX_CPUS) {
                unsigned long long v[10] = {0};
                while (*p && *p != ' ') p++;
                sscanf(p, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
                uint64_t idle = v[3] + v[4];
                uint64_t total = 0; for (int i = 0; i < 8; i++) total += v[i];
                s->cpu_total[idx] = total; s->cpu_busy[idx] = total - idle;
            }
        } else if (!strncmp(line, "processes ", 10)) {
            /* forks since boot - unused */
        }
        line = nl ? nl + 1 : NULL;
    }
    /* current frequency */
    if (read_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", buf, 64) > 0)
        s->mhz = atof(buf) / 1000.0;
    else if (read_file("/proc/cpuinfo", buf, sizeof buf) > 0) {
        char *p = strstr(buf, "cpu MHz");
        if (p && (p = strchr(p, ':'))) s->mhz = atof(p + 1);
    }
    if (read_file("/sys/devices/system/cpu/cpu0/cpufreq/base_frequency", buf, 64) > 0)
        s->base_mhz = atof(buf) / 1000.0;
    else if (read_file("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", buf, 64) > 0)
        s->base_mhz = atof(buf) / 1000.0;
}

static uint64_t meminfo_kb(const char *buf, const char *key)
{
    const char *p = strstr(buf, key);
    if (!p) return 0;
    p += strlen(key);
    return strtoull(p, NULL, 10) * 1024;
}

static void sample_mem(Sys *s)
{
    static char buf[8192];
    if (read_file("/proc/meminfo", buf, sizeof buf) <= 0) return;
    s->mem_total = meminfo_kb(buf, "MemTotal:");
    s->mem_avail = meminfo_kb(buf, "MemAvailable:");
    if (!s->mem_avail) s->mem_avail = meminfo_kb(buf, "MemFree:");
    s->mem_cached = meminfo_kb(buf, "Cached:") + meminfo_kb(buf, "Buffers:");
    s->mem_used = s->mem_total - s->mem_avail;
    s->swap_total = meminfo_kb(buf, "SwapTotal:");
    s->swap_used = s->swap_total - meminfo_kb(buf, "SwapFree:");
    s->mem_committed = meminfo_kb(buf, "Committed_AS:");
    s->mem_commit_limit = meminfo_kb(buf, "CommitLimit:");
}

static void sample_disk(Sys *s)
{
    static char buf[32768];
    if (read_file("/proc/diskstats", buf, sizeof buf) <= 0) return;
    uint64_t rd = 0, wr = 0;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        int maj, min; char name[64]; unsigned long long f[11] = {0};
        if (sscanf(line, "%d %d %63s %llu %llu %llu %llu %llu %llu %llu %llu",
                   &maj, &min, name, &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7]) >= 10) {
            /* only whole devices: skip partitions (sdaN, nvme0n1pN, mmcblk0pN) and virtual */
            size_t n = strlen(name);
            int is_part = (n && isdigit((unsigned char)name[n - 1]) &&
                           (strncmp(name, "sd", 2) == 0 || strncmp(name, "hd", 2) == 0 ||
                            strncmp(name, "vd", 2) == 0 || strstr(name, "p") != NULL));
            int is_virt = !strncmp(name, "loop", 4) || !strncmp(name, "ram", 3) ||
                          !strncmp(name, "dm-", 3) || !strncmp(name, "zram", 4) || !strncmp(name, "md", 2);
            if (!strncmp(name, "nvme", 4) || !strncmp(name, "mmcblk", 6)) is_part = strchr(name, 'p') && isdigit((unsigned char)name[n - 1]);
            if (!is_part && !is_virt) { rd += f[2] * 512; wr += f[6] * 512; }
        }
        line = nl ? nl + 1 : NULL;
    }
    s->disk_rd = rd; s->disk_wr = wr;
}

static void sample_net(Sys *s)
{
    static char buf[16384];
    if (read_file("/proc/net/dev", buf, sizeof buf) <= 0) return;
    uint64_t rx = 0, tx = 0;
    char *line = strchr(buf, '\n'); if (line) line = strchr(line + 1, '\n');
    if (line) line++;
    while (line && *line) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        char *colon = strchr(line, ':');
        if (colon) {
            char *name = line; while (*name == ' ') name++;
            *colon = 0;
            if (strcmp(name, "lo")) {
                unsigned long long f[16] = {0};
                sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7], &f[8]);
                rx += f[0]; tx += f[8];
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    s->net_rx = rx; s->net_tx = tx;
}

void sys_sample(Sys *s)
{
    s->t_ns = sys_now_ns();
    sample_cpu(s);
    sample_mem(s);
    sample_disk(s);
    sample_net(s);
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        s->uptime = si.uptime;
        s->load[0] = si.loads[0] / 65536.0;
        s->load[1] = si.loads[1] / 65536.0;
        s->load[2] = si.loads[2] / 65536.0;
    }
    char buf[64];
    if (read_file("/proc/sys/fs/file-nr", buf, sizeof buf) > 0) s->nhandles = atoi(buf);
}

static int count_fds(int pid)
{
    char path[64]; snprintf(path, sizeof path, "/proc/%d/fd", pid);
    DIR *d = opendir(path);
    if (!d) return 0;
    int n = 0; struct dirent *e;
    while ((e = readdir(d))) if (e->d_name[0] != '.') n++;
    closedir(d);
    return n;
}

int sys_procs(Proc **arr, int *n, int *cap)
{
    DIR *d = opendir("/proc");
    if (!d) return -1;
    *n = 0;
    struct dirent *e;
    static char buf[4096];
    while ((e = readdir(d))) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        int pid = atoi(e->d_name);
        char path[80];
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        if (read_file(path, buf, sizeof buf) <= 0) continue;

        if (*n >= *cap) { *cap = *cap ? *cap * 2 : 256; *arr = realloc(*arr, *cap * sizeof(Proc)); }
        Proc *p = &(*arr)[*n];
        memset(p, 0, sizeof *p);
        p->pid = pid;

        /* comm may contain spaces/parens: find the last ')' */
        char *lp = strchr(buf, '('), *rp = strrchr(buf, ')');
        if (!lp || !rp) continue;
        int nl = (int)(rp - lp - 1); if (nl > 63) nl = 63;
        memcpy(p->name, lp + 1, nl); p->name[nl] = 0;
        char *f = rp + 2;
        /* fields from index 3 (state) */
        char state; int ppid; long long utime, stime, prio, nice, thr, starttime, vsz, rss;
        unsigned long long pgrp, sess, tty, tpgid, flags, minflt, cminflt, majflt, cmajflt, cutime, cstime, itreal;
        if (sscanf(f, "%c %d %llu %llu %llu %llu %llu %llu %llu %llu %llu %lld %lld %llu %llu %lld %lld %lld %llu %lld %lld %lld",
                   &state, &ppid, &pgrp, &sess, &tty, &tpgid, &flags, &minflt, &cminflt, &majflt, &cmajflt,
                   &utime, &stime, &cutime, &cstime, &prio, &nice, &thr, &itreal, &starttime, &vsz, &rss) < 22)
            continue;
        p->state = state; p->ppid = ppid; p->prio = (int)prio; p->nice = (int)nice;
        p->threads = (int)thr;
        p->cpu_time = (uint64_t)(utime + stime) * (1000000000ull / clk_tck);
        p->vsz = (uint64_t)vsz; p->rss = (uint64_t)rss * page;
        p->start = (uint64_t)starttime;

        /* owner: from /proc/pid/status (Uid:) */
        snprintf(path, sizeof path, "/proc/%d/status", pid);
        if (read_file(path, buf, sizeof buf) > 0) {
            char *u = strstr(buf, "Uid:");
            if (u) sys_username((uint32_t)atoi(u + 4), p->user, sizeof p->user);
        }
        /* I/O counters (may need permission) */
        snprintf(path, sizeof path, "/proc/%d/io", pid);
        if (read_file(path, buf, sizeof buf) > 0) {
            char *r = strstr(buf, "read_bytes:"), *w = strstr(buf, "write_bytes:");
            if (r) p->rd = strtoull(r + 11, NULL, 10);
            if (w) p->wr = strtoull(w + 12, NULL, 10);
        }
        p->handles = count_fds(pid);
        /* command line */
        snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
        int len = read_file(path, buf, sizeof buf);
        if (len > 0) {
            for (int i = 0; i < len - 1; i++) if (!buf[i]) buf[i] = ' ';
            snprintf(p->cmd, sizeof p->cmd, "%s", buf);
        } else {
            snprintf(p->cmd, sizeof p->cmd, "[%s]", p->name);   /* kernel thread */
        }
        (*n)++;
    }
    closedir(d);
    return 0;
}

int sys_kill(int pid)
{
    if (kill(pid, SIGTERM) == 0) return 0;
    return kill(pid, SIGKILL);
}
