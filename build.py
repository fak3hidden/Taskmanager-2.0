#!/usr/bin/env python3
"""
build.py - compile Task Manager 2.0 for the operating system of your choice.

    python3 build.py              interactive menu
    python3 build.py windows      build one target directly
    python3 build.py all          build every target
    python3 build.py --list       show targets

Building for your *own* OS uses whatever C compiler is installed (cc/gcc/clang,
or MinGW gcc on Windows).  Building for *another* OS (or if no compiler is
found) uses `zig cc` as a cross-compiler; the script offers to install it via
`pip install ziglang` if it is missing.
"""
import os
import platform
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "src")
OUT = os.path.join(ROOT, "build")

COMMON = ["main.c", "ui.c", "gfx.c", "icons.c", "png.c"]
CFLAGS = ["-Os", "-std=c11", "-Wall", "-Wno-unused-parameter", "-Wno-misleading-indentation",
          "-Wno-format-truncation", "-Wno-missing-field-initializers",
          "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
          "-ffunction-sections", "-fdata-sections"]

# key: (label, zig target, platform sources, link flags, output name)
TARGETS = {
    "linux":         ("Linux  x86-64",        "x86_64-linux-gnu.2.17",  ["sys_linux.c", "win_x11.c"],
                      ["-ldl", "-lm", "-s", "-Wl,--gc-sections"], "taskmgr"),
    "linux-arm64":   ("Linux  ARM64",         "aarch64-linux-gnu.2.17", ["sys_linux.c", "win_x11.c"],
                      ["-ldl", "-lm", "-s", "-Wl,--gc-sections"], "taskmgr"),
    "windows":       ("Windows x64",          "x86_64-windows-gnu",     ["sys_win.c", "win_w32.c"],
                      ["-Wl,--subsystem,windows", "-lgdi32", "-luser32", "-ladvapi32", "-liphlpapi",
                       "-lpowrprof", "-lshell32", "-s", "-Wl,--gc-sections"], "taskmgr.exe"),
    "macos":         ("macOS  Intel",         "x86_64-macos",           ["sys_mac.c", "win_mac.c"],
                      ["-Wl,-dead_strip"], "taskmgr"),
    "macos-arm64":   ("macOS  Apple Silicon", "aarch64-macos",          ["sys_mac.c", "win_mac.c"],
                      ["-Wl,-dead_strip"], "taskmgr"),
}
ORDER = ["linux", "linux-arm64", "windows", "macos", "macos-arm64"]


def host_target():
    sysname = platform.system().lower()
    arm = platform.machine().lower() in ("arm64", "aarch64")
    if sysname.startswith("linux"):
        return "linux-arm64" if arm else "linux"
    if sysname.startswith("win") or sysname.startswith("msys") or sysname.startswith("mingw"):
        return "windows"
    if sysname == "darwin":
        return "macos-arm64" if arm else "macos"
    return None


def find_native_cc():
    for cc in (os.environ.get("CC"), "cc", "gcc", "clang", "x86_64-w64-mingw32-gcc"):
        if cc and shutil.which(cc):
            return [cc]
    return None


def find_zig():
    if os.environ.get("ZIG"):
        return os.environ["ZIG"].split()
    if shutil.which("zig"):
        return ["zig"]
    try:
        import ziglang  # noqa: F401
        return [sys.executable, "-m", "ziglang"]
    except ImportError:
        return None


def install_zig():
    ans = input("zig (cross-compiler) not found. Install it with 'pip install ziglang' (~50 MB)? [Y/n] ").strip().lower()
    if ans not in ("", "y", "yes"):
        return None
    cmd = [sys.executable, "-m", "pip", "install", "ziglang"]
    if subprocess.call(cmd) != 0:
        print("  plain pip install failed, retrying with --user / --break-system-packages ...")
        subprocess.call(cmd + ["--user"])
        if find_zig() is None:
            subprocess.call(cmd + ["--break-system-packages"])
    return find_zig()


def build(key, force_zig=False):
    label, zig_target, plat_src, ldflags, outname = TARGETS[key]
    outdir = os.path.join(OUT, key)
    os.makedirs(outdir, exist_ok=True)
    outfile = os.path.join(outdir, outname)
    sources = [os.path.join(SRC, f) for f in COMMON + plat_src]

    native = (key == host_target()) and not force_zig
    cc = find_native_cc() if native else None
    if cc:
        cmd = cc + CFLAGS + ["-o", outfile] + sources + ldflags
        how = "native compiler " + cc[0]
    else:
        zig = find_zig() or install_zig()
        if not zig:
            print(f"!! cannot build {label}: no C compiler and zig not available")
            return None
        cmd = zig + ["cc", "-target", zig_target] + CFLAGS + ["-o", outfile] + sources + ldflags
        how = "zig cc -target " + zig_target

    print(f"\n==> {label}  ({how})")
    rc = subprocess.call(cmd, cwd=ROOT)
    if rc != 0:
        print(f"!! build failed for {label} (exit {rc})")
        return None
    size = os.path.getsize(outfile)
    print(f"    OK  {os.path.relpath(outfile, ROOT)}  ({size:,} bytes = {size / 1024:.0f} KB)")
    return outfile


def menu():
    host = host_target()
    print("Task Manager 2.0 - build\n")
    print("Which operating system do you want to compile for?\n")
    for i, key in enumerate(ORDER, 1):
        mark = "  (this machine)" if key == host else ""
        print(f"  {i}) {TARGETS[key][0]}{mark}")
    print(f"  {len(ORDER) + 1}) All of the above")
    print("  q) Quit\n")
    default = str(ORDER.index(host) + 1) if host in ORDER else "1"
    while True:
        choice = input(f"Choice [{default}]: ").strip().lower() or default
        if choice in ("q", "quit", "exit"):
            return []
        if choice.isdigit():
            n = int(choice)
            if 1 <= n <= len(ORDER):
                return [ORDER[n - 1]]
            if n == len(ORDER) + 1:
                return ORDER[:]
        if choice in TARGETS:
            return [choice]
        print("  please enter a number from the list")


def main(argv):
    if "-h" in argv or "--help" in argv:
        print(__doc__)
        return 0
    if "--list" in argv:
        for k in ORDER:
            print(f"{k:14} {TARGETS[k][0]}")
        return 0
    force_zig = "--zig" in argv
    args = [a for a in argv if not a.startswith("-")]

    if args:
        keys = ORDER[:] if "all" in args else args
        bad = [k for k in keys if k not in TARGETS]
        if bad:
            print("unknown target(s):", ", ".join(bad), "\nvalid:", ", ".join(ORDER), "or 'all'")
            return 2
    else:
        keys = menu()
        if not keys:
            return 0

    results = {k: build(k, force_zig) for k in keys}
    ok = [k for k, v in results.items() if v]
    print("\nSummary:")
    for k in keys:
        print(f"  {'ok  ' if results[k] else 'FAIL'}  {TARGETS[k][0]:22} {os.path.relpath(results[k], ROOT) if results[k] else ''}")
    if len(ok) == 1 and ok[0] == host_target():
        exe = os.path.relpath(results[ok[0]], ROOT)
        print(f"\nRun it with:  {exe if os.sep in exe else './' + exe}")
    return 0 if len(ok) == len(keys) else 1


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        print()
        sys.exit(130)
