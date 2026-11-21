/*
 * Harvest Moon: Back to Nature (Mod Penyu v1.3.0) - PS4 Native Port
 * Target: Sony PlayStation 4 (AMD Jaguar x86-64 Native)
 * Display: 1920x1080 FullHD 16:9 Widescreen Native
 * Controller: DualShock 4
 * Audio: 44.1kHz S16LE Stereo SPU/XA
 * Save Storage: /data/HarvestMoonBTN/saves/ (Standard 128KB Raw PS1 Memory Card)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <locale.h>
#include <wchar.h>
#include <signal.h>
#include <ucontext.h>
#include <SDL2/SDL.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#ifdef ENABLE_HD_TEXTURES
#include "hd_textures.h"
#endif

#define PS4_SCREEN_WIDTH  1920
#define PS4_SCREEN_HEIGHT 1080

static SDL_Window*   g_ps4_window   = NULL;
static SDL_Renderer* g_ps4_renderer = NULL;
static char g_base_path[] = "/app0/";

static FILE* g_log_fp = NULL;
static int g_log_fd = -1;  /* raw fd for async-signal-safe crash writes */

static void ps4_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    if (!g_log_fp) {
        mkdir("/data", 0777);
        mkdir("/data/HarvestMoonBTN", 0777);
        g_log_fp = fopen("/data/HarvestMoonBTN/loader.log", "a");
        if (g_log_fp) {
            g_log_fd = fileno(g_log_fp);
            dup2(g_log_fd, 1);
            dup2(g_log_fd, 2);
        }
    }
    if (g_log_fp) {
        va_start(args, fmt);
        vfprintf(g_log_fp, fmt, args);
        va_end(args);
        fflush(g_log_fp);
    }
}

/* Explicit stack protection canary & handler to prevent null GOT dereference */
__attribute__((visibility("default"))) uintptr_t __stack_chk_guard = 0x595e9fbd94fda766ULL;
__attribute__((visibility("default"))) void __stack_chk_fail(void) {
    ps4_log("[HMBTN PS4 FATAL] Stack smashing detected!\n");
    abort();
}

/* ---------------------------------------------------------------
 * Async-signal-safe crash writer.
 * ONLY uses: write() syscall + static char array.
 * No malloc, no stdio, no snprintf — all forbidden in signal handlers.
 * --------------------------------------------------------------- */
/* NOTE: g_log_fd declared above in ps4_log block */

/* Write a literal C string safely */
#define CRASH_PUTS(s) do {     const char _s[] = s;     if (g_log_fd >= 0) write(g_log_fd, _s, sizeof(_s)-1);     write(2, _s, sizeof(_s)-1); } while(0)

/* Write a uint64 as 18-char hex "0x<16hex>\n" */
static void crash_write_u64(uint64_t v) {
    static const char HEX[] = "0123456789abcdef";
    char buf[20];
    buf[0]  = '0'; buf[1]  = 'x';
    buf[18] = '\n';
    for (int i = 0; i < 16; i++)
        buf[2+i] = HEX[(v >> (60 - i*4)) & 0xF];
    if (g_log_fd >= 0) write(g_log_fd, buf, 19);
    write(2, buf, 19);
}

/* Write label + uint64 hex on one line: "LABEL=0x<16hex>\n" */
static void crash_write_reg(const char* label, int label_len, uint64_t v) {
    if (g_log_fd >= 0) write(g_log_fd, label, label_len);
    write(2, label, label_len);
    crash_write_u64(v);
}

/* FreeBSD amd64 mcontext and ucontext layout */
typedef struct {
    uint64_t mc_onstack;   /* 0: 0x00 */
    uint64_t mc_rdi;       /* 1: 0x08 */
    uint64_t mc_rsi;       /* 2: 0x10 */
    uint64_t mc_rdx;       /* 3: 0x18 */
    uint64_t mc_rcx;       /* 4: 0x20 */
    uint64_t mc_r8;        /* 5: 0x28 */
    uint64_t mc_r9;        /* 6: 0x30 */
    uint64_t mc_rax;       /* 7: 0x38 */
    uint64_t mc_rbx;       /* 8: 0x40 */
    uint64_t mc_rbp;       /* 9: 0x48 */
    uint64_t mc_r10;       /* 10: 0x50 */
    uint64_t mc_r11;       /* 11: 0x58 */
    uint64_t mc_r12;       /* 12: 0x60 */
    uint64_t mc_r13;       /* 13: 0x68 */
    uint64_t mc_r14;       /* 14: 0x70 */
    uint64_t mc_r15;       /* 15: 0x78 */
    uint32_t mc_trapno;    /* 16: 0x80 */
    uint16_t mc_fs;        /* 0x84 */
    uint16_t mc_gs;        /* 0x86 */
    uint64_t mc_addr;      /* 17: 0x88 */
    uint32_t mc_flags;     /* 0x90 */
    uint16_t mc_es;        /* 0x94 */
    uint16_t mc_ds;        /* 0x96 */
    uint64_t mc_err;       /* 18: 0x98 */
    uint64_t mc_rip;       /* 19: 0xa0 */
    uint64_t mc_cs;        /* 20: 0xa8 */
    uint64_t mc_rflags;    /* 21: 0xb0 */
    uint64_t mc_rsp;       /* 22: 0xb8 */
    uint64_t mc_ss;        /* 23: 0xc0 */
} ps4_mcontext_t;

typedef struct {
    uint64_t uc_sigmask[2];
    ps4_mcontext_t uc_mcontext;
} ps4_ucontext_t;

static void crash_handler(int sig, siginfo_t* info, void* uctx) {
    if (g_log_fd < 0) {
        g_log_fd = open("/data/HarvestMoonBTN/loader.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    }
    /* Signal number format */
    char sigbuf[64] = "[HMBTN PS4 FATAL CRASH] signal=";
    int siglen = 31;
    if (sig >= 10) {
        sigbuf[siglen++] = '0' + (sig / 10);
        sigbuf[siglen++] = '0' + (sig % 10);
    } else {
        sigbuf[siglen++] = '0' + sig;
    }
    sigbuf[siglen++] = '\n';
    if (g_log_fd >= 0) write(g_log_fd, sigbuf, siglen);
    write(2, sigbuf, siglen);

    if (info && (uintptr_t)info > 0x1000) {
        CRASH_PUTS("[CRASH] info->si_addr=");
        crash_write_u64((uint64_t)(uintptr_t)info->si_addr);
        CRASH_PUTS("[CRASH] info->si_code=");
        crash_write_u64((uint64_t)info->si_code);
    }

    if (uctx) {
        /* Raw dump of the first 32 words of uctx */
        uint64_t* raw = (uint64_t*)uctx;
        CRASH_PUTS("[CRASH] --- RAW UCONTEXT WORDS ---\n");
        for (int i = 0; i < 32; i++) {
            char wlbl[16] = "[CRASH] W00=";
            wlbl[9] = '0' + (i / 10);
            wlbl[10] = '0' + (i % 10);
            crash_write_reg(wlbl, 12, raw[i]);
        }

        ps4_ucontext_t* uc = (ps4_ucontext_t*)uctx;
        uint64_t rip = uc->uc_mcontext.mc_rip;
        uint64_t rsp = uc->uc_mcontext.mc_rsp;
        crash_write_reg("[CRASH] RIP=", 12, rip);
        crash_write_reg("[CRASH] RSP=", 12, rsp);
        crash_write_reg("[CRASH] RAX=", 12, uc->uc_mcontext.mc_rax);
        crash_write_reg("[CRASH] RBX=", 12, uc->uc_mcontext.mc_rbx);
        crash_write_reg("[CRASH] RCX=", 12, uc->uc_mcontext.mc_rcx);
        crash_write_reg("[CRASH] RDX=", 12, uc->uc_mcontext.mc_rdx);
        crash_write_reg("[CRASH] RSI=", 12, uc->uc_mcontext.mc_rsi);
        crash_write_reg("[CRASH] RDI=", 12, uc->uc_mcontext.mc_rdi);
        crash_write_reg("[CRASH] RBP=", 12, uc->uc_mcontext.mc_rbp);
        crash_write_reg("[CRASH] R8 =", 12, uc->uc_mcontext.mc_r8);
        crash_write_reg("[CRASH] R9 =", 12, uc->uc_mcontext.mc_r9);
        crash_write_reg("[CRASH] R10=", 12, uc->uc_mcontext.mc_r10);
        crash_write_reg("[CRASH] R11=", 12, uc->uc_mcontext.mc_r11);
        crash_write_reg("[CRASH] R12=", 12, uc->uc_mcontext.mc_r12);
        crash_write_reg("[CRASH] R13=", 12, uc->uc_mcontext.mc_r13);
        crash_write_reg("[CRASH] R14=", 12, uc->uc_mcontext.mc_r14);
        crash_write_reg("[CRASH] R15=", 12, uc->uc_mcontext.mc_r15);
        crash_write_reg("[CRASH] ADDR=", 13, uc->uc_mcontext.mc_addr);
        crash_write_reg("[CRASH] ERR =", 13, uc->uc_mcontext.mc_err);

        if (rip >= 0x140000000ULL && rip < 0x142000000ULL) {
            CRASH_PUTS("[CRASH] IN EXE at RVA=");
            crash_write_u64(rip - 0x140000000ULL);
        } else {
            CRASH_PUTS("[CRASH] RIP outside EXE range\n");
        }

        /* Stack dump */
        if (rsp >= 0x10000 && rsp < 0x800000000000ULL) {
            CRASH_PUTS("[CRASH] --- STACK DUMP (RSP) ---\n");
            uint64_t* stk = (uint64_t*)rsp;
            for (int i = 0; i < 16; i++) {
                char slbl[16] = "[CRASH] S00=";
                slbl[9] = '0' + (i / 10);
                slbl[10] = '0' + (i % 10);
                crash_write_reg(slbl, 12, stk[i]);
            }
        }
    }
    CRASH_PUTS("========================================================\n");

    /* fsync to ensure data hits disk before process dies */
    if (g_log_fd >= 0) { fsync(g_log_fd); }
    _exit(128 + sig);
}

/* MSVCRT x64 _iobuf structure: 48 bytes per entry */
typedef struct {
    char* _ptr;
    int   _cnt;
    char* _base;
    int   _flag;
    int   _file;
    int   _charbuf;
    int   _bufsiz;
    char* _tmpfname;
} msvcrt_FILE;
static msvcrt_FILE g_msvcrt_iob[20];
static FILE* g_iob[3];

/* Undefine sys/stat macros for Windows stat struct */
#undef st_atime
#undef st_mtime
#undef st_ctime

struct _stat64_win {
    uint32_t st_dev;
    uint16_t st_ino;
    uint16_t st_mode;
    int16_t  st_nlink;
    int16_t  st_uid;
    int16_t  st_gid;
    uint32_t st_rdev;
    int64_t  st_size;
    int64_t  st_atime;
    int64_t  st_mtime;
    int64_t  st_ctime;
};

struct _finddata64_t {
    uint32_t attrib;
    int64_t  time_create;
    int64_t  time_access;
    int64_t  time_write;
    int64_t  size;
    char     name[260];
};

typedef struct {
    DIR* dir;
    char pattern[260];
} find_handle_t;

/* --- Helper to Seed Missing Files from /app0/ to /data/ --- */
static void copy_file_if_missing(const char* src_path, const char* dst_path) {
    if (access(dst_path, F_OK) != 0) {
        FILE* src = fopen(src_path, "rb");
        if (src) {
            FILE* dst = fopen(dst_path, "wb");
            if (dst) {
                char buf[8192];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                    fwrite(buf, 1, n, dst);
                }
                fclose(dst);
                printf("[HMBTN PS4] Seeded %s from %s\n", dst_path, src_path);
            }
            fclose(src);
        }
    }
}

/* --- Path Redirection for PS4 Writable Files (/data/) --- */
#define HM_DATA_ROOT "/data/HarvestMoonBTN/"
#define HM_DISC_CUE  "/app0/disc.cue"

/* Create every missing parent directory of a file path (like mkdir -p). */
static void mkdirs_for(const char* filepath) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", filepath);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
}

/*
 * /app0/ is read-only on PS4. Every write under /app0/ is redirected to
 * /data/HarvestMoonBTN/<same relative path>. Reads use the /data copy when it
 * exists and fall back to the read-only /app0 original otherwise.
 * saves/* and settings.ini are seeded from /app0 on first access.
 * A ring of buffers is used so two calls in one expression (rename) do not
 * overwrite each other's result.
 */
static const char* redirect_path(const char* path, bool for_write) {
    static char s_ring[4][512];
    static unsigned s_ring_idx = 0;
    char rel_buf[300];
    const char* rel = NULL;
    if (!path) return path;

    if (strncmp(path, "/app0/", 6) == 0) {
        rel = path + 6;
    } else if (strstr(path, "saves") || strstr(path, "settings.ini")) {
        /* Legacy relative paths */
        const char* fname = strrchr(path, '/');
        if (!fname) fname = strrchr(path, '\\');
        fname = fname ? (fname + 1) : path;
        if (strstr(path, "saves")) snprintf(rel_buf, sizeof(rel_buf), "saves/%s", fname);
        else snprintf(rel_buf, sizeof(rel_buf), "settings.ini");
        rel = rel_buf;
    }
    if (!rel || !*rel) return path;

    char* out = s_ring[s_ring_idx++ & 3];
    snprintf(out, 512, HM_DATA_ROOT "%s", rel);

    bool seed = (strncmp(rel, "saves/", 6) == 0) || (strcmp(rel, "settings.ini") == 0);
    if (for_write || seed) {
        mkdir("/data", 0777);
        mkdir("/data/HarvestMoonBTN", 0777);
        mkdirs_for(out);
        if (seed) {
            char app0_path[512];
            snprintf(app0_path, sizeof(app0_path), "/app0/%s", rel);
            copy_file_if_missing(app0_path, out);
        }
        return out;
    }
    if (access(out, F_OK) == 0) return out;
    return path;
}

/*
 * OpenOrbis currently exposes only SDL's software renderer. Migrate existing
 * per-user settings once so old installs do not render a high-resolution
 * texture in software. Profile v2 uses the native PS1 scale (426x240 in
 * widescreen mode), reducing source pixels by 9x versus scale 3. Unknown
 * settings and controller bindings are preserved verbatim.
 */
static void migrate_ps4_performance_settings(void) {
    const char* settings_path = HM_DATA_ROOT "settings.ini";
    const char* temp_path = HM_DATA_ROOT "settings.ini.perf.tmp";
    const char* marker_path = HM_DATA_ROOT ".performance-profile-v2";
    if (access(marker_path, F_OK) == 0) return;

    mkdir("/data", 0777);
    mkdir("/data/HarvestMoonBTN", 0777);
    copy_file_if_missing("/app0/settings.ini", settings_path);

    FILE* src = fopen(settings_path, "r");
    FILE* dst = fopen(temp_path, "w");
    if (!dst) {
        if (src) fclose(src);
        return;
    }

    bool found_resolution = false;
    bool found_smooth = false;
    char line[512];
    while (src && fgets(line, sizeof(line), src)) {
        if (strncmp(line, "resolution=", 11) == 0) {
            fputs("resolution=1\n", dst);
            found_resolution = true;
        } else if (strncmp(line, "smooth=", 7) == 0) {
            fputs("smooth=0\n", dst);
            found_smooth = true;
        } else {
            fputs(line, dst);
        }
    }
    if (src) fclose(src);
    if (!found_resolution) fputs("resolution=1\n", dst);
    if (!found_smooth) fputs("smooth=0\n", dst);
    fclose(dst);

    if (rename(temp_path, settings_path) != 0) {
        remove(temp_path);
        return;
    }

    FILE* marker = fopen(marker_path, "w");
    if (marker) {
        fputs("software-renderer-v2-native-scale\n", marker);
        fclose(marker);
    }
    ps4_log("[HMBTN PS4] Applied software-renderer profile v2: resolution=1, smooth=0\n");
}

/*
 * The cheat-menu language option writes two bytes ("id" or "en") to
 * lang.txt. Feed that persisted choice back through HMBTN_LANG on the next
 * launch instead of overriding the menu with a hard-coded environment value.
 */
static const char* load_language_preference(void) {
    static char language[3] = "id";
    const char* language_path = HM_DATA_ROOT "data/lang.txt";
    const char* legacy_path = HM_DATA_ROOT "lang.txt";
    mkdir("/data", 0777);
    mkdir("/data/HarvestMoonBTN", 0777);
    mkdir(HM_DATA_ROOT "data", 0777);

    FILE* file = fopen(language_path, "rb");
    bool loaded_legacy = false;
    if (!file) {
        file = fopen(legacy_path, "rb");
        loaded_legacy = (file != NULL);
    }
    if (file) {
        char saved[3] = {0, 0, 0};
        size_t count = fread(saved, 1, 2, file);
        fclose(file);
        if (count == 2 &&
            ((saved[0] == 'i' && saved[1] == 'd') ||
             (saved[0] == 'e' && saved[1] == 'n'))) {
            language[0] = saved[0];
            language[1] = saved[1];
            if (!loaded_legacy) return language;
        }
    }

    file = fopen(language_path, "wb");
    if (file) {
        fwrite(language, 1, 2, file);
        fclose(file);
    }
    return language;
}

/* --- Win32 thread handle emulation (CreateThread) --- */
#define HM_THREAD_MAGIC 0x48544852u
typedef struct {
    uint32_t magic;
    volatile int done;
    pthread_t th;
    void* fn;
    void* arg;
    uint32_t ret;
} hm_thread_t;
typedef __attribute__((ms_abi)) uint32_t (*ms_thread_fn_t)(void*);

static void* hm_thread_tramp(void* p) {
    hm_thread_t* t = (hm_thread_t*)p;
    t->ret = ((ms_thread_fn_t)t->fn)(t->arg);
    t->done = 1;
    return NULL;
}

static bool hm_is_thread_handle(void* h) {
    return (intptr_t)h > 0x10000 && ((hm_thread_t*)h)->magic == HM_THREAD_MAGIC;
}

/* --- MS ABI Wrappers for SDL2 --- */
__attribute__((ms_abi)) int hook_SDL_Init(uint32_t flags) {
    ps4_log("[HMBTN PS4] SDL_Init called with flags: 0x%X\n", flags);
    int res = SDL_Init(flags | SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER);
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_JoystickEventState(SDL_ENABLE);
    return res;
}

__attribute__((ms_abi)) int hook_SDL_InitSubSystem(uint32_t flags) {
    return SDL_InitSubSystem(flags);
}

__attribute__((ms_abi)) void hook_SDL_SetMainReady(void) {
    SDL_SetMainReady();
}

__attribute__((ms_abi)) SDL_Window* hook_SDL_CreateWindow(const char* title, int x, int y, int w, int h, uint32_t flags) {
    ps4_log("[HMBTN PS4] Creating FullHD 1080p Fullscreen Window (requested: %dx%d, flags: 0x%X)...\n", w, h, flags);
    g_ps4_window = SDL_CreateWindow(
        "Harvest Moon: Back to Nature (PS4 Native)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        PS4_SCREEN_WIDTH,
        PS4_SCREEN_HEIGHT,
        SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN
    );
    if (!g_ps4_window) {
        ps4_log("[HMBTN PS4 ERROR] SDL_CreateWindow failed: %s\n", SDL_GetError());
    } else {
        ps4_log("[HMBTN PS4] SDL_CreateWindow succeeded: %p\n", g_ps4_window);
    }
    return g_ps4_window;
}

__attribute__((ms_abi)) SDL_Renderer* hook_SDL_CreateRenderer(SDL_Window* window, int index, uint32_t flags) {
    ps4_log("[HMBTN PS4] Creating Renderer for window %p (requested flags: 0x%X)...\n", window, flags);

    /* The bundled PS4 SDL renderer is software-only; nearest scaling avoids
     * an expensive bilinear pass over the full 1080p framebuffer. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    int numDrivers = SDL_GetNumRenderDrivers();
    ps4_log("[HMBTN PS4] Available SDL render drivers: %d\n", numDrivers);
    int sw_index = -1;
    for (int i = 0; i < numDrivers; i++) {
        SDL_RendererInfo rinfo;
        if (SDL_GetRenderDriverInfo(i, &rinfo) == 0) {
            ps4_log("[HMBTN PS4]   Driver %d: '%s' (flags: 0x%X)\n", i, rinfo.name ? rinfo.name : "null", rinfo.flags);
            if (rinfo.name && strcmp(rinfo.name, "software") == 0) {
                sw_index = i;
            }
        }
    }

    /* 1. Try hardware renderer first (PS4 native via SceVideoOut) */
    int hw_index = -1;
    for (int i = 0; i < numDrivers; i++) {
        SDL_RendererInfo rinfo;
        if (SDL_GetRenderDriverInfo(i, &rinfo) == 0) {
            ps4_log("[HMBTN PS4]   Driver %d: '%s' (flags: 0x%X)\n", i, rinfo.name ? rinfo.name : "null", rinfo.flags);
            if (rinfo.name && strcmp(rinfo.name, "software") == 0) {
                sw_index = i;
            }
            if (rinfo.name && (strstr(rinfo.name, "ps4") || strstr(rinfo.name, "gxm") || strstr(rinfo.name, "gnm") || (rinfo.flags & SDL_RENDERER_ACCELERATED))) {
                hw_index = i;
            }
        }
    }

    /* Try hardware accelerated renderer */
    if (hw_index >= 0) {
        ps4_log("[HMBTN PS4] Trying hardware accelerated renderer index %d...\n", hw_index);
        g_ps4_renderer = SDL_CreateRenderer(window, hw_index, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    }
    if (!g_ps4_renderer && sw_index >= 0) {
        ps4_log("[HMBTN PS4] Trying SDL_CreateRenderer with software driver index %d...\n", sw_index);
        g_ps4_renderer = SDL_CreateRenderer(window, sw_index, SDL_RENDERER_SOFTWARE);
    }
    if (!g_ps4_renderer) {
        ps4_log("[HMBTN PS4] Trying SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE)...\n");
        g_ps4_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g_ps4_renderer) {
        ps4_log("[HMBTN PS4] Trying SDL_CreateSoftwareRenderer on window surface...\n");
        SDL_Surface* win_surface = SDL_GetWindowSurface(window);
        if (win_surface) {
            g_ps4_renderer = SDL_CreateSoftwareRenderer(win_surface);
        } else {
            ps4_log("[HMBTN PS4] SDL_GetWindowSurface failed: %s\n", SDL_GetError());
        }
    }
    if (!g_ps4_renderer) {
        ps4_log("[HMBTN PS4] Trying default SDL_CreateRenderer(window, -1, 0)...\n");
        g_ps4_renderer = SDL_CreateRenderer(window, -1, 0);
    }

    if (!g_ps4_renderer) {
        ps4_log("[HMBTN PS4 FATAL] All renderer creation methods failed: %s\n", SDL_GetError());
        return NULL;
    }

    ps4_log("[HMBTN PS4] Renderer created successfully: %p\n", g_ps4_renderer);
    SDL_RendererInfo current_info;
    if (SDL_GetRendererInfo(g_ps4_renderer, &current_info) == 0) {
        ps4_log("[HMBTN PS4] Active Renderer: '%s' (flags: 0x%X)\n", current_info.name ? current_info.name : "null", current_info.flags);
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    int res = SDL_RenderSetLogicalSize(g_ps4_renderer, PS4_SCREEN_WIDTH, PS4_SCREEN_HEIGHT);
    ps4_log("[HMBTN PS4] SDL_RenderSetLogicalSize(1920, 1080) -> %d\n", res);
    ps4_log("[HMBTN PS4] hook_SDL_CreateRenderer returning to game EXE...\n");
    if (g_log_fp) fflush(g_log_fp);
    return g_ps4_renderer;
}

__attribute__((ms_abi)) int hook_SDL_RenderSetLogicalSize(SDL_Renderer* renderer, int w, int h) {
    if (!renderer) return -1;
    ps4_log("[HMBTN PS4] SDL_RenderSetLogicalSize: %dx%d\n", w, h);
    if (g_log_fp) fflush(g_log_fp);
    if (w <= 0 || h <= 0) {
        w = PS4_SCREEN_WIDTH;
        h = PS4_SCREEN_HEIGHT;
    }
    int result = SDL_RenderSetLogicalSize(renderer, w, h);
    if (result == 0) {
        /* At scale 1 the 426x240 widescreen frame fits 4x into 1080p.
         * Integer scaling avoids the generic fractional software scaler. */
        int integer_result = SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
        ps4_log("[HMBTN PS4] SDL_RenderSetIntegerScale(enabled) -> %d\n", integer_result);
    }
    return result;
}

__attribute__((ms_abi)) SDL_Texture* hook_SDL_CreateTexture(SDL_Renderer* renderer, uint32_t format, int access, int w, int h) {
    if (!renderer) {
        ps4_log("[HMBTN PS4 ERROR] SDL_CreateTexture called with NULL renderer!\n");
        if (g_log_fp) fflush(g_log_fp);
        return NULL;
    }
    ps4_log("[HMBTN PS4] SDL_CreateTexture: %dx%d, format: 0x%X, access: %d\n", w, h, format, access);
    if (g_log_fp) fflush(g_log_fp);
    SDL_Texture* tex = SDL_CreateTexture(renderer, format, access, w, h);
    if (!tex) {
        ps4_log("[HMBTN PS4 WARNING] SDL_CreateTexture failed: %s\n", SDL_GetError());
    } else {
        ps4_log("[HMBTN PS4] SDL_CreateTexture success: %p\n", (void*)tex);
    }
    if (g_log_fp) fflush(g_log_fp);
    return tex;
}

static uint64_t s_perf_update_ticks = 0;
static uint64_t s_perf_copy_ticks = 0;
static uint64_t s_perf_present_ticks = 0;

__attribute__((ms_abi)) int hook_SDL_UpdateTexture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch) {
    if (!texture || !pixels) return -1;
    static uint64_t s_update_count = 0;
    s_update_count++;
    if (s_update_count <= 5 || (s_update_count % 300 == 0)) {
        ps4_log("[HMBTN PS4] SDL_UpdateTexture #%llu: pitch=%d\n",
                (unsigned long long)s_update_count, pitch);
        if (g_log_fp) fflush(g_log_fp);
    }
    uint64_t start = SDL_GetPerformanceCounter();
    int result = SDL_UpdateTexture(texture, rect, pixels, pitch);
    s_perf_update_ticks += SDL_GetPerformanceCounter() - start;
    return result;
}

__attribute__((ms_abi)) int hook_SDL_RenderClear(SDL_Renderer* renderer) {
    if (!renderer) return -1;
    return SDL_RenderClear(renderer);
}

__attribute__((ms_abi)) int hook_SDL_RenderCopy(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect) {
    if (!renderer || !texture) return -1;
    uint64_t start = SDL_GetPerformanceCounter();
    int result = SDL_RenderCopy(renderer, texture, srcrect, dstrect);
    s_perf_copy_ticks += SDL_GetPerformanceCounter() - start;
    return result;
}

__attribute__((ms_abi)) void hook_SDL_RenderPresent(SDL_Renderer* renderer) {
    if (!renderer) return;
    static uint64_t s_present_count = 0;
    static uint64_t s_fps_window_start = 0;
    static uint32_t s_fps_window_frames = 0;
    s_present_count++;
    if (s_present_count <= 5 || (s_present_count % 300 == 0)) {
        ps4_log("[HMBTN PS4] SDL_RenderPresent #%llu\n",
                (unsigned long long)s_present_count);
        if (g_log_fp) fflush(g_log_fp);
    }
    static int s_splash_hidden = 0;
    if (!s_splash_hidden) {
        s_splash_hidden = 1;
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
        int ret_splash = sceSystemServiceHideSplashScreen();
        ps4_log("[HMBTN PS4] sceSystemServiceHideSplashScreen on first present -> 0x%08X\n", ret_splash);
        if (g_log_fp) fflush(g_log_fp);
    }

    /* The PS4 video backend already waits for the flip/vblank. Adding a delay
     * here double-paces every software-rendered frame. */
    uint64_t present_start = SDL_GetPerformanceCounter();
    SDL_RenderPresent(renderer);
    uint64_t present_end = SDL_GetPerformanceCounter();
    s_perf_present_ticks += present_end - present_start;

    uint64_t now = present_end;
    uint64_t freq = SDL_GetPerformanceFrequency();
    if (freq > 0) {
        if (s_fps_window_start == 0) {
            /* Discard startup work so every phase counter covers exactly the
             * same measurement interval as the FPS window. */
            s_fps_window_start = now;
            s_perf_update_ticks = 0;
            s_perf_copy_ticks = 0;
            s_perf_present_ticks = 0;
            return;
        }
        s_fps_window_frames++;
        uint64_t elapsed = now - s_fps_window_start;
        if (elapsed >= freq * 5) {
            uint64_t fps_x100 = ((uint64_t)s_fps_window_frames * 100 * freq) / elapsed;
            uint64_t frame_ms_x100 = (elapsed * 100000) / ((uint64_t)s_fps_window_frames * freq);
            uint64_t update_ms_x100 = (s_perf_update_ticks * 100000) / ((uint64_t)s_fps_window_frames * freq);
            uint64_t copy_ms_x100 = (s_perf_copy_ticks * 100000) / ((uint64_t)s_fps_window_frames * freq);
            uint64_t present_ms_x100 = (s_perf_present_ticks * 100000) / ((uint64_t)s_fps_window_frames * freq);
            uint64_t measured_ms_x100 = update_ms_x100 + copy_ms_x100 + present_ms_x100;
            uint64_t other_ms_x100 = frame_ms_x100 > measured_ms_x100
                ? frame_ms_x100 - measured_ms_x100 : 0;
            ps4_log("[HMBTN PS4 PERF] measured FPS: %llu.%02llu | update=%llu.%02llums copy=%llu.%02llums present=%llu.%02llums other=%llu.%02llums\n",
                    (unsigned long long)(fps_x100 / 100),
                    (unsigned long long)(fps_x100 % 100),
                    (unsigned long long)(update_ms_x100 / 100),
                    (unsigned long long)(update_ms_x100 % 100),
                    (unsigned long long)(copy_ms_x100 / 100),
                    (unsigned long long)(copy_ms_x100 % 100),
                    (unsigned long long)(present_ms_x100 / 100),
                    (unsigned long long)(present_ms_x100 % 100),
                    (unsigned long long)(other_ms_x100 / 100),
                    (unsigned long long)(other_ms_x100 % 100));
            s_fps_window_start = now;
            s_fps_window_frames = 0;
            s_perf_update_ticks = 0;
            s_perf_copy_ticks = 0;
            s_perf_present_ticks = 0;
        }
    }
}

__attribute__((ms_abi)) void hook_SDL_DestroyTexture(SDL_Texture* texture) {
    if (texture) SDL_DestroyTexture(texture);
}

__attribute__((ms_abi)) int hook_SDL_SetWindowFullscreen(SDL_Window* window, uint32_t flags) {
    ps4_log("[HMBTN PS4] SDL_SetWindowFullscreen called (window: %p, flags: 0x%X)\n", window, flags);
    if (g_log_fp) fflush(g_log_fp);
    return 0; /* PS4 window is already fullscreen native 1080p */
}

__attribute__((ms_abi)) SDL_bool hook_SDL_SetHint(const char* name, const char* value) {
    if (name && strcmp(name, SDL_HINT_RENDER_SCALE_QUALITY) == 0) {
        return SDL_SetHint(name, "nearest");
    }
    return SDL_SetHint(name, value);
}

__attribute__((ms_abi)) void hook_SDL_Delay(uint32_t ms) {
    /* Clamp excessive delays that would kill FPS.
     * Game might call SDL_Delay(100+) for timing - clamp to max 16ms for 60 FPS target. */
    if (ms > 16) {
        ps4_log("[HMBTN PS4] Clamping SDL_Delay(%u) -> 16\n", ms);
        ms = 16;
    }
    SDL_Delay(ms);
}

__attribute__((ms_abi)) uint64_t hook_SDL_GetPerformanceCounter(void) {
    return SDL_GetPerformanceCounter();
}

__attribute__((ms_abi)) uint64_t hook_SDL_GetPerformanceFrequency(void) {
    return SDL_GetPerformanceFrequency();
}

__attribute__((ms_abi)) int hook_SDL_GetCPUCount(void) {
    return 8; /* 8-core AMD Jaguar */
}

__attribute__((ms_abi)) const char* hook_SDL_GetError(void) {
    return SDL_GetError();
}

__attribute__((ms_abi)) char* hook_SDL_GetBasePath(void) {
    ps4_log("[HMBTN PS4] SDL_GetBasePath called\n");
    if (g_log_fp) fflush(g_log_fp);
    char* p = (char*)malloc(sizeof(g_base_path));
    if (p) strcpy(p, g_base_path);
    return p;
}

__attribute__((ms_abi)) void hook_SDL_free(void* mem) {
    if (!mem) return;
    if (mem == g_base_path) {
        ps4_log("[HMBTN PS4 WARNING] Ignored attempt to free static g_base_path!\n");
        if (g_log_fp) fflush(g_log_fp);
        return;
    }
    free(mem);
}

/* ---------------------------------------------------------------
 * DualShock 4 Controller & Event Handling
 * --------------------------------------------------------------- */
#define SYNTH_QUEUE_SIZE 64
static SDL_Event s_synth_events[SYNTH_QUEUE_SIZE];
static int s_synth_head = 0;
static int s_synth_tail = 0;

static void enqueue_synth_event(const SDL_Event* ev) {
    int next = (s_synth_tail + 1) % SYNTH_QUEUE_SIZE;
    if (next != s_synth_head) {
        s_synth_events[s_synth_tail] = *ev;
        s_synth_tail = next;
    }
}

static void push_keyboard_event(uint32_t type, SDL_Keycode sym, SDL_Scancode scancode) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.key.state = (type == SDL_KEYDOWN) ? SDL_PRESSED : SDL_RELEASED;
    ev.key.repeat = 0;
    ev.key.keysym.sym = sym;
    ev.key.keysym.scancode = scancode;
    ev.key.keysym.mod = KMOD_NONE;
    if (sym == SDLK_F1) {
        ps4_log("[HMBTN PS4 INPUT] Queued F1 %s for cheat menu\n",
                type == SDL_KEYDOWN ? "DOWN" : "UP");
    }
    enqueue_synth_event(&ev);
}

#define CHEAT_SOURCE_DIRECT_L3      (1u << 0)
#define CHEAT_SOURCE_DIRECT_R3      (1u << 1)
#define CHEAT_SOURCE_CONTROLLER_L3  (1u << 2)
#define CHEAT_SOURCE_CONTROLLER_R3  (1u << 3)
#define CHEAT_SOURCE_JOYSTICK_L3    (1u << 4)
#define CHEAT_SOURCE_JOYSTICK_R3    (1u << 5)

static uint32_t s_cheat_sources = 0;

static void update_cheat_menu_key(uint32_t source, int pressed) {
    uint32_t previous_sources = s_cheat_sources;
    if (pressed) {
        s_cheat_sources |= source;
    } else {
        s_cheat_sources &= ~source;
    }

    /* All backends may report the same physical edge. Emit one logical F1. */
    if (previous_sources == 0 && s_cheat_sources != 0) {
        push_keyboard_event(SDL_KEYDOWN, SDLK_F1, SDL_SCANCODE_F1);
    } else if (previous_sources != 0 && s_cheat_sources == 0) {
        push_keyboard_event(SDL_KEYUP, SDLK_F1, SDL_SCANCODE_F1);
    }
}

static void push_controller_button_event(uint32_t type, uint8_t button) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.cbutton.state = (type == SDL_CONTROLLERBUTTONDOWN) ? SDL_PRESSED : SDL_RELEASED;
    ev.cbutton.button = button;
    ev.cbutton.which = 0;
    enqueue_synth_event(&ev);
}

static int s_pad_handle = -1;
static uint32_t s_prev_pad_buttons = 0;

static int get_ds4_handle(void) {
    if (s_pad_handle > 0) return s_pad_handle;
    int32_t userId = 0;
    if (sceUserServiceGetInitialUser(&userId) == 0 && userId > 0) {
        int h = scePadGetHandle(userId, 0, 0);
        if (h > 0) {
            s_pad_handle = h;
            ps4_log("[HMBTN PS4 INPUT] Direct DS4 handle: %d (userId=0x%X)\n", h, userId);
            if (g_log_fp) fflush(g_log_fp);
            return s_pad_handle;
        }
        h = scePadOpen(userId, 0, 0, NULL);
        if (h > 0) {
            s_pad_handle = h;
            ps4_log("[HMBTN PS4 INPUT] Direct scePadOpen: %d (userId=0x%X)\n", h, userId);
            if (g_log_fp) fflush(g_log_fp);
            return s_pad_handle;
        }
    }
    OrbisUserServiceLoginUserIdList userList;
    memset(&userList, 0, sizeof(userList));
    if (sceUserServiceGetLoginUserIdList(&userList) == 0) {
        for (int i = 0; i < 4; i++) {
            if (userList.userId[i] > 0) {
                int h = scePadGetHandle(userList.userId[i], 0, 0);
                if (h <= 0) h = scePadOpen(userList.userId[i], 0, 0, NULL);
                if (h > 0) {
                    s_pad_handle = h;
                    ps4_log("[HMBTN PS4 INPUT] Direct DS4 handle from userList[%d]: %d (userId=0x%X)\n",
                            i, h, userList.userId[i]);
                    if (g_log_fp) fflush(g_log_fp);
                    return s_pad_handle;
                }
            }
        }
    }
    return -1;
}

static void poll_direct_ds4(void) {
    int handle = get_ds4_handle();
    if (handle <= 0) return;

    OrbisPadData data;
    memset(&data, 0, sizeof(data));
    int res = scePadReadState(handle, &data);
    if (res != 0 || !data.connected) return;

    uint32_t curr = data.buttons;
    uint32_t changed = curr ^ s_prev_pad_buttons;
    if (!changed) return;

    s_prev_pad_buttons = curr;

    struct ButtonMap {
        uint32_t mask;
        uint8_t ctrl_btn;
        SDL_Keycode sym;
        SDL_Scancode scan;
        const char* name;
    };

    static const struct ButtonMap bmaps[] = {
        { ORBIS_PAD_BUTTON_CROSS,     SDL_CONTROLLER_BUTTON_A,             SDLK_RETURN, SDL_SCANCODE_RETURN, "CROSS" },
        { ORBIS_PAD_BUTTON_CIRCLE,    SDL_CONTROLLER_BUTTON_B,             SDLK_UNKNOWN, SDL_SCANCODE_UNKNOWN, "CIRCLE" },
        { ORBIS_PAD_BUTTON_SQUARE,    SDL_CONTROLLER_BUTTON_X,             SDLK_a,      SDL_SCANCODE_A,      "SQUARE" },
        { ORBIS_PAD_BUTTON_TRIANGLE,  SDL_CONTROLLER_BUTTON_Y,             SDLK_s,      SDL_SCANCODE_S,      "TRIANGLE" },
        { ORBIS_PAD_BUTTON_OPTIONS,   SDL_CONTROLLER_BUTTON_START,         SDLK_RETURN, SDL_SCANCODE_RETURN, "OPTIONS" },
        { ORBIS_PAD_BUTTON_TOUCH_PAD, SDL_CONTROLLER_BUTTON_BACK,          SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE, "TOUCHPAD" },
        { ORBIS_PAD_BUTTON_UP,        SDL_CONTROLLER_BUTTON_DPAD_UP,       SDLK_UP,     SDL_SCANCODE_UP,     "UP" },
        { ORBIS_PAD_BUTTON_DOWN,      SDL_CONTROLLER_BUTTON_DPAD_DOWN,     SDLK_DOWN,   SDL_SCANCODE_DOWN,   "DOWN" },
        { ORBIS_PAD_BUTTON_LEFT,      SDL_CONTROLLER_BUTTON_DPAD_LEFT,     SDLK_LEFT,   SDL_SCANCODE_LEFT,   "LEFT" },
        { ORBIS_PAD_BUTTON_RIGHT,     SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    SDLK_RIGHT,  SDL_SCANCODE_RIGHT,  "RIGHT" },
        { ORBIS_PAD_BUTTON_L1,        SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  SDLK_q,      SDL_SCANCODE_Q,      "L1" },
        { ORBIS_PAD_BUTTON_R1,        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDLK_e,      SDL_SCANCODE_E,      "R1" },
        { ORBIS_PAD_BUTTON_L3,        SDL_CONTROLLER_BUTTON_LEFTSTICK,     SDLK_F1,     SDL_SCANCODE_F1,     "L3" },
        { ORBIS_PAD_BUTTON_R3,        SDL_CONTROLLER_BUTTON_RIGHTSTICK,    SDLK_F1,     SDL_SCANCODE_F1,     "R3" },
    };

    for (size_t i = 0; i < sizeof(bmaps)/sizeof(bmaps[0]); i++) {
        if (changed & bmaps[i].mask) {
            int pressed = (curr & bmaps[i].mask) ? 1 : 0;
            ps4_log("[HMBTN PS4 INPUT] Button %s %s (mask=0x%X)\n",
                    bmaps[i].name, pressed ? "DOWN" : "UP", bmaps[i].mask);
            if (g_log_fp) fflush(g_log_fp);

            uint32_t ctype = pressed ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
            uint32_t ktype = pressed ? SDL_KEYDOWN : SDL_KEYUP;

            push_controller_button_event(ctype, bmaps[i].ctrl_btn);
            if (bmaps[i].mask == ORBIS_PAD_BUTTON_L3 || bmaps[i].mask == ORBIS_PAD_BUTTON_R3) {
                uint32_t source = (bmaps[i].mask == ORBIS_PAD_BUTTON_L3)
                    ? CHEAT_SOURCE_DIRECT_L3 : CHEAT_SOURCE_DIRECT_R3;
                update_cheat_menu_key(source, pressed);
            } else if (bmaps[i].sym != SDLK_UNKNOWN) {
                push_keyboard_event(ktype, bmaps[i].sym, bmaps[i].scan);
            }

            /* Also push SPACE for CROSS/OPTIONS to guarantee welcome/intro screen advances */
            if (bmaps[i].mask == ORBIS_PAD_BUTTON_CROSS || bmaps[i].mask == ORBIS_PAD_BUTTON_OPTIONS) {
                push_keyboard_event(ktype, SDLK_SPACE, SDL_SCANCODE_SPACE);
            }
        }
    }
}

__attribute__((ms_abi)) int hook_SDL_PollEvent(SDL_Event* event) {
    if (!event) return 0;

    /* 1. Return pending synthetic event if any */
    if (s_synth_head != s_synth_tail) {
        *event = s_synth_events[s_synth_head];
        s_synth_head = (s_synth_head + 1) % SYNTH_QUEUE_SIZE;
        if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) &&
            event->key.keysym.sym == SDLK_F1) {
            ps4_log("[HMBTN PS4 INPUT] Delivered F1 %s to game\n",
                    event->type == SDL_KEYDOWN ? "DOWN" : "UP");
        }
        return 1;
    }

    /* 2. Poll hardware DS4 state to detect button changes */
    poll_direct_ds4();

    if (s_synth_head != s_synth_tail) {
        *event = s_synth_events[s_synth_head];
        s_synth_head = (s_synth_head + 1) % SYNTH_QUEUE_SIZE;
        if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) &&
            event->key.keysym.sym == SDLK_F1) {
            ps4_log("[HMBTN PS4 INPUT] Delivered F1 %s to game\n",
                    event->type == SDL_KEYDOWN ? "DOWN" : "UP");
        }
        return 1;
    }

    /* 3. Call SDL's real PollEvent */
    int ret = SDL_PollEvent(event);
    if (!ret) return 0;

    /* 4. If SDL produced a controller/joystick event, sync keyboard events */
    if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        ps4_log("[HMBTN PS4 SDL] Controller button DOWN: %d\n", (int)event->cbutton.button);
        if (g_log_fp) fflush(g_log_fp);

        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_A ||
            event->cbutton.button == SDL_CONTROLLER_BUTTON_START) {
            push_keyboard_event(SDL_KEYDOWN, SDLK_RETURN, SDL_SCANCODE_RETURN);
            push_keyboard_event(SDL_KEYDOWN, SDLK_SPACE, SDL_SCANCODE_SPACE);
        } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
            push_keyboard_event(SDL_KEYDOWN, SDLK_LEFT, SDL_SCANCODE_LEFT);
        } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            push_keyboard_event(SDL_KEYDOWN, SDLK_RIGHT, SDL_SCANCODE_RIGHT);
        } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            push_keyboard_event(SDL_KEYDOWN, SDLK_UP, SDL_SCANCODE_UP);
        } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            push_keyboard_event(SDL_KEYDOWN, SDLK_DOWN, SDL_SCANCODE_DOWN);
        } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK ||
                   event->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) {
            uint32_t source = (event->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK)
                ? CHEAT_SOURCE_CONTROLLER_L3 : CHEAT_SOURCE_CONTROLLER_R3;
            update_cheat_menu_key(source, 1);
        }
    } else if (event->type == SDL_CONTROLLERBUTTONUP) {
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_A ||
            event->cbutton.button == SDL_CONTROLLER_BUTTON_START) {
            push_keyboard_event(SDL_KEYUP, SDLK_RETURN, SDL_SCANCODE_RETURN);
            push_keyboard_event(SDL_KEYUP, SDLK_SPACE, SDL_SCANCODE_SPACE);
        } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
            push_keyboard_event(SDL_KEYUP, SDLK_LEFT, SDL_SCANCODE_LEFT);
        } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            push_keyboard_event(SDL_KEYUP, SDLK_RIGHT, SDL_SCANCODE_RIGHT);
        } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            push_keyboard_event(SDL_KEYUP, SDLK_UP, SDL_SCANCODE_UP);
        } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            push_keyboard_event(SDL_KEYUP, SDLK_DOWN, SDL_SCANCODE_DOWN);
        } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK ||
                   event->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) {
            uint32_t source = (event->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK)
                ? CHEAT_SOURCE_CONTROLLER_L3 : CHEAT_SOURCE_CONTROLLER_R3;
            update_cheat_menu_key(source, 0);
        }
    } else if (event->type == SDL_JOYBUTTONDOWN) {
        ps4_log("[HMBTN PS4 SDL] Joystick button DOWN: %d\n", (int)event->jbutton.button);
        if (g_log_fp) fflush(g_log_fp);

        if (event->jbutton.button == 0 || event->jbutton.button == 9) {
            push_keyboard_event(SDL_KEYDOWN, SDLK_RETURN, SDL_SCANCODE_RETURN);
            push_keyboard_event(SDL_KEYDOWN, SDLK_SPACE, SDL_SCANCODE_SPACE);
            push_controller_button_event(SDL_CONTROLLERBUTTONDOWN,
                (event->jbutton.button == 0) ? SDL_CONTROLLER_BUTTON_A : SDL_CONTROLLER_BUTTON_START);
        } else if (event->jbutton.button == 11 || event->jbutton.button == 12) {
            /* SDL's raw joystick backend reports L3/R3 as buttons 11/12. */
            uint32_t source = (event->jbutton.button == 11)
                ? CHEAT_SOURCE_JOYSTICK_L3 : CHEAT_SOURCE_JOYSTICK_R3;
            update_cheat_menu_key(source, 1);
        }
    } else if (event->type == SDL_JOYBUTTONUP) {
        if (event->jbutton.button == 11 || event->jbutton.button == 12) {
            uint32_t source = (event->jbutton.button == 11)
                ? CHEAT_SOURCE_JOYSTICK_L3 : CHEAT_SOURCE_JOYSTICK_R3;
            update_cheat_menu_key(source, 0);
        }
    }

    return ret;
}

__attribute__((ms_abi)) int hook_SDL_NumJoysticks(void) {
    int n = SDL_NumJoysticks();
    ps4_log("[HMBTN PS4] SDL_NumJoysticks() -> %d\n", n);
    if (g_log_fp) fflush(g_log_fp);
    return n;
}

__attribute__((ms_abi)) SDL_bool hook_SDL_IsGameController(int joystick_index) {
    SDL_bool b = SDL_IsGameController(joystick_index);
    ps4_log("[HMBTN PS4] SDL_IsGameController(%d) raw -> %d\n", joystick_index, (int)b);
    /* DualShock 4 is always a game controller on PS4 */
    if (joystick_index == 0 && SDL_NumJoysticks() > 0) {
        b = SDL_TRUE;
        ps4_log("[HMBTN PS4] SDL_IsGameController forced -> %d (DualShock 4)\n", (int)b);
    }
    if (g_log_fp) fflush(g_log_fp);
    return b;
}

__attribute__((ms_abi)) SDL_GameController* hook_SDL_GameControllerOpen(int joystick_index) {
    ps4_log("[HMBTN PS4] SDL_GameControllerOpen(%d)...\n", joystick_index);
    if (g_log_fp) fflush(g_log_fp);

    /* Ensure mapping exists for PS4 joystick */
    SDL_Joystick* j = SDL_JoystickOpen(joystick_index);
    if (j) {
        char guid_str[64];
        SDL_JoystickGUID guid = SDL_JoystickGetGUID(j);
        SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
        ps4_log("[HMBTN PS4] Joystick %d GUID: %s\n", joystick_index, guid_str);

        char mapping[512];
        snprintf(mapping, sizeof(mapping),
            "%s,PS4 Controller,a:b0,b:b1,x:b2,y:b3,back:b17,start:b9,leftshoulder:b4,rightshoulder:b5,leftstick:b11,rightstick:b12,lefttrigger:b18,righttrigger:b19,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,leftx:a0,lefty:a1,rightx:a2,righty:a5,",
            guid_str);
        int res = SDL_GameControllerAddMapping(mapping);
        ps4_log("[HMBTN PS4] SDL_GameControllerAddMapping -> %d\n", res);
        SDL_JoystickClose(j);
    }

    SDL_GameController* c = SDL_GameControllerOpen(joystick_index);
    ps4_log("[HMBTN PS4] SDL_GameControllerOpen(%d) -> %p\n", joystick_index, c);
    if (c) {
        ps4_log("[HMBTN PS4] DualShock 4 Controller connected: %s\n", SDL_GameControllerName(c));
    }
    if (g_log_fp) fflush(g_log_fp);
    return c;
}

__attribute__((ms_abi)) int16_t hook_SDL_GameControllerGetAxis(SDL_GameController* controller, SDL_GameControllerAxis axis) {
    int16_t res = SDL_GameControllerGetAxis(controller, axis);
    if (res != 0) return res;

    int handle = get_ds4_handle();
    if (handle > 0) {
        OrbisPadData data;
        memset(&data, 0, sizeof(data));
        if (scePadReadState(handle, &data) == 0 && data.connected) {
            switch (axis) {
                case SDL_CONTROLLER_AXIS_LEFTX:
                    if (data.leftStick.x < 110 || data.leftStick.x > 146) {
                        return (int16_t)(((int)data.leftStick.x - 128) * 256);
                    }
                    break;
                case SDL_CONTROLLER_AXIS_LEFTY:
                    if (data.leftStick.y < 110 || data.leftStick.y > 146) {
                        return (int16_t)(((int)data.leftStick.y - 128) * 256);
                    }
                    break;
                case SDL_CONTROLLER_AXIS_RIGHTX:
                    if (data.rightStick.x < 110 || data.rightStick.x > 146) {
                        return (int16_t)(((int)data.rightStick.x - 128) * 256);
                    }
                    break;
                case SDL_CONTROLLER_AXIS_RIGHTY:
                    if (data.rightStick.y < 110 || data.rightStick.y > 146) {
                        return (int16_t)(((int)data.rightStick.y - 128) * 256);
                    }
                    break;
                case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
                    if (data.buttons & ORBIS_PAD_BUTTON_L2) return 32767;
                    break;
                case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
                    if (data.buttons & ORBIS_PAD_BUTTON_R2) return 32767;
                    break;
                default: break;
            }
        }
    }
    return 0;
}

__attribute__((ms_abi)) uint8_t hook_SDL_GameControllerGetButton(SDL_GameController* controller, SDL_GameControllerButton button) {
    /* Override special mappings per user request (SDL2 enum values):
     * Options -> START (6) = Pause Menu
     * Touchpad -> BACK (4) = Mod Menu
     * L3/R3 -> GUIDE (5) and F1 event = Cheat Menu
     */
    if (button == SDL_CONTROLLER_BUTTON_START) {        /* 6 - Pause Menu */
        if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_OPTIONS) return 1;
    }
    if (button == SDL_CONTROLLER_BUTTON_BACK) {         /* 4 - Mod Menu */
        if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_TOUCH_PAD) return 1;
    }
    if (button == SDL_CONTROLLER_BUTTON_GUIDE) {        /* 5 - Cheat Menu alias */
        if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_L3) return 1;
        if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_R3) return 1;
    }

    /* Standard DS4 -> SDL mapping for all other buttons */
    if (s_prev_pad_buttons != 0) {
        switch (button) {
            case SDL_CONTROLLER_BUTTON_A:               /* 0 - Cross */
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_CROSS) return 1;
                break;
            case SDL_CONTROLLER_BUTTON_B:               /* 1 - Circle */
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_CIRCLE) return 1;
                break;
            case SDL_CONTROLLER_BUTTON_X:               /* 2 - Square */
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_SQUARE) return 1;
                break;
            case SDL_CONTROLLER_BUTTON_Y:               /* 3 - Triangle */
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_TRIANGLE) return 1;
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:    /* 9 - L1 */
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_L1) return 1;
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:   /* 10 - R1 */
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_R1) return 1;
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSTICK:       /* 7 - L3 click */
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_L3) return 1;
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK:      /* 8 - R3 click */
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_R3) return 1;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_UP) return 1;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_DOWN) return 1;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_LEFT) return 1;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_RIGHT) return 1;
                break;
            default: break;
        }
    }
    return 0;
}

__attribute__((ms_abi)) SDL_AudioDeviceID hook_SDL_OpenAudioDevice(const char* device, int iscapture, const SDL_AudioSpec* desired, SDL_AudioSpec* obtained, int allowed_changes) {
    if (!desired) {
        ps4_log("[HMBTN PS4] SDL_OpenAudioDevice called with NULL desired spec!\n");
        return 0;
    }
    
    /* Force proper audio spec for PS4: 48kHz, stereo, 1024 samples */
    static SDL_AudioSpec fixed_desired = {0};
    static int spec_initialized = 0;
    if (!spec_initialized) {
        fixed_desired.freq = 48000;
        fixed_desired.format = AUDIO_S16SYS;
        fixed_desired.channels = 2;
        fixed_desired.silence = 0;
        fixed_desired.samples = 1024;
        fixed_desired.padding = 0;
        fixed_desired.size = 0;
        fixed_desired.callback = desired->callback;
        fixed_desired.userdata = desired->userdata;
        spec_initialized = 1;
        ps4_log("[HMBTN PS4] Forcing audio spec: 48000 Hz, %d channels, 1024 samples, callback=%p\n",
            fixed_desired.channels, (void*)fixed_desired.callback);
    }
    
    ps4_log("[HMBTN PS4] Opening Audio Device: %d Hz, %d channels, callback=%p...\n",
        fixed_desired.freq, fixed_desired.channels, (void*)fixed_desired.callback);
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(device, iscapture, &fixed_desired, obtained, allowed_changes);
    ps4_log("[HMBTN PS4] SDL_OpenAudioDevice returned: %u\n", (unsigned)dev);
    if (dev > 0) {
        /* Unpause audio device - game might forget to call SDL_PauseAudioDevice(dev, 0) */
        SDL_PauseAudioDevice(dev, 0);
        ps4_log("[HMBTN PS4] Audio device unpaused\n");
    }
    if (g_log_fp) fflush(g_log_fp);
    return dev;
}

__attribute__((ms_abi)) void hook_SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) {
    SDL_PauseAudioDevice(dev, pause_on);
}

__attribute__((ms_abi)) int hook_SDL_QueueAudio(SDL_AudioDeviceID dev, const void* data, uint32_t len) {
    return SDL_QueueAudio(dev, data, len);
}

__attribute__((ms_abi)) void hook_SDL_ClearQueuedAudio(SDL_AudioDeviceID dev) {
    SDL_ClearQueuedAudio(dev);
}

__attribute__((ms_abi)) uint32_t hook_SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev) {
    return SDL_GetQueuedAudioSize(dev);
}

__attribute__((ms_abi)) const char* hook_SDL_GetKeyName(SDL_Keycode key) {
    return SDL_GetKeyName(key);
}

__attribute__((ms_abi)) SDL_Keycode hook_SDL_GetKeyFromName(const char* name) {
    return SDL_GetKeyFromName(name);
}

__attribute__((ms_abi)) int hook_SDL_ShowSimpleMessageBox(uint32_t flags, const char* title, const char* message, SDL_Window* window) {
    printf("[HMBTN PS4 DIALOG] [%s] %s\n", title, message);
    return 0;
}

/* Message boxes cannot be shown on PS4: log and report failure (-1), which the game treats as "no / cancelled". */
__attribute__((ms_abi)) int hook_SDL_ShowMessageBox(const SDL_MessageBoxData* data, int* buttonid) {
    if (data) printf("[HMBTN PS4 DIALOG] [%s] %s\n", data->title ? data->title : "", data->message ? data->message : "");
    if (buttonid) *buttonid = -1;
    return -1;
}

__attribute__((ms_abi)) void hook_SDL_DestroyRenderer(SDL_Renderer* renderer) {
    if (renderer == g_ps4_renderer) g_ps4_renderer = NULL;
    SDL_DestroyRenderer(renderer);
}

__attribute__((ms_abi)) void hook_SDL_DestroyWindow(SDL_Window* window) {
    if (window == g_ps4_window) g_ps4_window = NULL;
    SDL_DestroyWindow(window);
}

__attribute__((ms_abi)) void hook_SDL_SetWindowTitle(SDL_Window* window, const char* title) {
    SDL_SetWindowTitle(window, title);
}

__attribute__((ms_abi)) int hook_SDL_SetRenderDrawColor(SDL_Renderer* r, uint8_t red, uint8_t g, uint8_t b, uint8_t a) {
    if (!r) return -1;
    return SDL_SetRenderDrawColor(r, red, g, b, a);
}

__attribute__((ms_abi)) int hook_SDL_RenderDrawRect(SDL_Renderer* r, const SDL_Rect* rect) {
    if (!r) return -1;
    return SDL_RenderDrawRect(r, rect);
}

__attribute__((ms_abi)) int hook_SDL_RenderFillRect(SDL_Renderer* r, const SDL_Rect* rect) {
    if (!r) return -1;
    return SDL_RenderFillRect(r, rect);
}

/* Writable SDL file access goes through the same /app0 -> /data redirection. */
__attribute__((ms_abi)) SDL_RWops* hook_SDL_RWFromFile(const char* file, const char* mode) {
    ps4_log("[HMBTN PS4] SDL_RWFromFile('%s', '%s')\n", file ? file : "null", mode ? mode : "null");
    if (g_log_fp) fflush(g_log_fp);
    bool for_write = mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'));
    const char* real_path = redirect_path(file, for_write);
    SDL_RWops* rw = SDL_RWFromFile(real_path, mode);
    if (!rw && !for_write && real_path != file) rw = SDL_RWFromFile(file, mode);
    ps4_log("[HMBTN PS4] SDL_RWFromFile -> %p\n", rw);
    if (g_log_fp) fflush(g_log_fp);
    return rw;
}

/* DualShock 4 only: the PS4 SDL backend already ships the native DS4 mapping.
 * The external gamecontrollerdb.txt (mappings for other pads) is ignored so it can never override it. */
__attribute__((ms_abi)) int hook_SDL_GameControllerAddMappingsFromRW(SDL_RWops* rw, int freerw) {
    ps4_log("[HMBTN PS4] SDL_GameControllerAddMappingsFromRW(rw=%p, freerw=%d)\n", rw, freerw);
    if (rw && freerw) SDL_RWclose(rw);
    ps4_log("[HMBTN PS4] External controller mappings ignored (DualShock 4 native mapping only)\n");
    if (g_log_fp) fflush(g_log_fp);
    return 0;
}

__attribute__((ms_abi)) void hook_SDL_GameControllerClose(SDL_GameController* c) { SDL_GameControllerClose(c); }
__attribute__((ms_abi)) SDL_Joystick* hook_SDL_GameControllerGetJoystick(SDL_GameController* c) { return SDL_GameControllerGetJoystick(c); }
__attribute__((ms_abi)) const char* hook_SDL_GameControllerName(SDL_GameController* c) {
    const char* name = SDL_GameControllerName(c);
    return name ? name : "DualShock 4";
}
__attribute__((ms_abi)) const char* hook_SDL_JoystickName(SDL_Joystick* j) {
    const char* name = SDL_JoystickName(j);
    return name ? name : "DualShock 4";
}
__attribute__((ms_abi)) SDL_Joystick* hook_SDL_JoystickOpen(int idx) { return SDL_JoystickOpen(idx); }
__attribute__((ms_abi)) void hook_SDL_JoystickClose(SDL_Joystick* j) { SDL_JoystickClose(j); }
__attribute__((ms_abi)) int16_t hook_SDL_JoystickGetAxis(SDL_Joystick* j, int axis) { return SDL_JoystickGetAxis(j, axis); }
__attribute__((ms_abi)) uint8_t hook_SDL_JoystickGetButton(SDL_Joystick* j, int button) { return SDL_JoystickGetButton(j, button); }
__attribute__((ms_abi)) uint8_t hook_SDL_JoystickGetHat(SDL_Joystick* j, int hat) { return SDL_JoystickGetHat(j, hat); }
__attribute__((ms_abi)) int32_t hook_SDL_JoystickInstanceID(SDL_Joystick* j) { return (int32_t)SDL_JoystickInstanceID(j); }
__attribute__((ms_abi)) int hook_SDL_JoystickNumAxes(SDL_Joystick* j) { return SDL_JoystickNumAxes(j); }
__attribute__((ms_abi)) int hook_SDL_JoystickNumButtons(SDL_Joystick* j) { return SDL_JoystickNumButtons(j); }
__attribute__((ms_abi)) int hook_SDL_JoystickNumHats(SDL_Joystick* j) { return SDL_JoystickNumHats(j); }

/* Writable per-user directory on PS4 (freed by the game through SDL_free). */
__attribute__((ms_abi)) char* hook_SDL_GetPrefPath(const char* org, const char* app) {
    mkdir("/data", 0777);
    mkdir("/data/HarvestMoonBTN", 0777);
    const char* p = "/data/HarvestMoonBTN/";
    char* out = (char*)SDL_malloc(strlen(p) + 1);
    if (out) strcpy(out, p);
    return out;
}

/* --- WinHTTP: no network updater on PS4. Every call fails, so the update thread exits quietly. --- */
__attribute__((ms_abi)) void* hook_WinHttpOpen(void) { return NULL; }
__attribute__((ms_abi)) void* hook_WinHttpConnect(void) { return NULL; }
__attribute__((ms_abi)) void* hook_WinHttpOpenRequest(void) { return NULL; }
__attribute__((ms_abi)) int hook_WinHttpCrackUrl(void) { return 0; }
__attribute__((ms_abi)) int hook_WinHttpSendRequest(void) { return 0; }
__attribute__((ms_abi)) int hook_WinHttpReceiveResponse(void) { return 0; }
__attribute__((ms_abi)) int hook_WinHttpQueryHeaders(void) { return 0; }
__attribute__((ms_abi)) int hook_WinHttpQueryDataAvailable(void) { return 0; }
__attribute__((ms_abi)) int hook_WinHttpReadData(void) { return 0; }
__attribute__((ms_abi)) int hook_WinHttpSetTimeouts(void) { return 0; }
__attribute__((ms_abi)) int hook_WinHttpCloseHandle(void* h) { return 1; }

__attribute__((ms_abi)) void* hook_SDL_memcpy(void* dst, const void* src, size_t len) {
    return memcpy(dst, src, len);
}

__attribute__((ms_abi)) size_t hook_SDL_strlen(const char* str) {
    return strlen(str);
}

__attribute__((ms_abi)) size_t hook_SDL_wcslen(const void* str) {
    const uint16_t* s = (const uint16_t*)str;
    size_t len = 0;
    while (*s++) len++;
    return len;
}

__attribute__((ms_abi)) size_t hook_wcslen(const void* str) {
    return hook_SDL_wcslen(str);
}

__attribute__((ms_abi)) char* hook_SDL_iconv_string(const char* tocode, const char* fromcode, const char* inbuf, size_t inbytesleft) {
    return strdup(inbuf);
}

/* --- MS ABI Wrappers for KERNEL32 --- */
__attribute__((ms_abi)) void* hook_GetProcessHeap(void) {
    return (void*)1;
}

__attribute__((ms_abi)) void* hook_HeapAlloc(void* hHeap, uint32_t dwFlags, size_t dwBytes) {
    if (dwFlags & 8) return calloc(1, dwBytes);
    return malloc(dwBytes);
}

__attribute__((ms_abi)) int hook_HeapFree(void* hHeap, uint32_t dwFlags, void* lpMem) {
    free(lpMem);
    return 1;
}

__attribute__((ms_abi)) void* hook_LocalFree(void* hMem) {
    free(hMem);
    return NULL;
}

typedef struct {
    void*     BaseAddress;
    void*     AllocationBase;
    uint32_t  AllocationProtect;
    uint32_t  __align1;
    uint64_t  RegionSize;
    uint32_t  State;
    uint32_t  Protect;
    uint32_t  Type;
    uint32_t  __align2;
} win64_MBI;

#define MAX_VALLOCS 128
typedef struct {
    void* addr;
    size_t size;
} VAllocTrack;
static VAllocTrack g_vallocs[MAX_VALLOCS];

__attribute__((ms_abi)) void* hook_VirtualAlloc(void* lpAddress, size_t dwSize, uint32_t flAllocationType, uint32_t flProtect) {
    ps4_log("[HMBTN PS4] VirtualAlloc(addr=%p, size=0x%llX, type=0x%X, prot=0x%X)\n",
            lpAddress, (unsigned long long)dwSize, flAllocationType, flProtect);

    /* If lpAddress is provided, caller is committing or altering existing allocation */
    if (lpAddress != NULL) {
        uintptr_t base = (uintptr_t)lpAddress & ~4095ULL;
        size_t len = ((uintptr_t)lpAddress + dwSize - base + 4095ULL) & ~4095ULL;
        mprotect((void*)base, len, PROT_READ | PROT_WRITE);
        ps4_log("[HMBTN PS4] VirtualAlloc commit -> %p\n", lpAddress);
        if (g_log_fp) fflush(g_log_fp);
        return lpAddress;
    }

    /* Otherwise, new allocation with mmap */
    void* ptr = MAP_FAILED;
    size_t try_sizes[] = {
        dwSize,
        0x100000000ULL, /* 4 GB */
        0x80000000ULL,  /* 2 GB */
        0x40000000ULL,  /* 1 GB */
        0x20000000ULL,  /* 512 MB */
        0x10000000ULL,  /* 256 MB */
        0x4000000ULL    /* 64 MB */
    };
    int num_tries = sizeof(try_sizes) / sizeof(try_sizes[0]);
    size_t alloc_size = 0;

    for (int i = 0; i < num_tries; i++) {
        if (try_sizes[i] > dwSize && i > 0) continue;
        size_t s = (try_sizes[i] + 4095ULL) & ~4095ULL;
        ptr = mmap(NULL, s, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr != MAP_FAILED) {
            alloc_size = s;
            ps4_log("[HMBTN PS4] VirtualAlloc mmap success: requested 0x%llX, got 0x%llX at %p\n",
                    (unsigned long long)dwSize, (unsigned long long)alloc_size, ptr);
            break;
        }
    }

    if (ptr == MAP_FAILED) {
        ps4_log("[HMBTN PS4 ERROR] VirtualAlloc mmap failed for size 0x%llX: %s\n",
                (unsigned long long)dwSize, strerror(errno));
        if (g_log_fp) fflush(g_log_fp);
        return NULL;
    }

    /* Record in tracking table */
    for (int i = 0; i < MAX_VALLOCS; i++) {
        if (g_vallocs[i].addr == NULL) {
            g_vallocs[i].addr = ptr;
            g_vallocs[i].size = alloc_size;
            break;
        }
    }

    if (g_log_fp) fflush(g_log_fp);
    return ptr;
}

__attribute__((ms_abi)) int hook_VirtualFree(void* lpAddress, size_t dwSize, uint32_t dwFreeType) {
    ps4_log("[HMBTN PS4] VirtualFree(addr=%p, size=0x%llX, type=0x%X)\n",
            lpAddress, (unsigned long long)dwSize, dwFreeType);
    if (!lpAddress) return 1;

    for (int i = 0; i < MAX_VALLOCS; i++) {
        if (g_vallocs[i].addr == lpAddress) {
            munmap(lpAddress, g_vallocs[i].size);
            g_vallocs[i].addr = NULL;
            g_vallocs[i].size = 0;
            if (g_log_fp) fflush(g_log_fp);
            return 1;
        }
    }
    if (g_log_fp) fflush(g_log_fp);
    return 1;
}

__attribute__((ms_abi)) int hook_VirtualProtect(void* lpAddress, size_t dwSize, uint32_t flNewProtect, uint32_t* lpflOldProtect) {
    if (lpflOldProtect) *lpflOldProtect = 0x40; /* PAGE_EXECUTE_READWRITE */
    if (lpAddress && dwSize > 0) {
        uintptr_t base = (uintptr_t)lpAddress & ~4095ULL;
        size_t len = ((uintptr_t)lpAddress + dwSize - base + 4095ULL) & ~4095ULL;
        mprotect((void*)base, len, PROT_READ | PROT_WRITE | PROT_EXEC);
    }
    return 1;
}

__attribute__((ms_abi)) size_t hook_VirtualQuery(void* lpAddress, void* lpBuffer, size_t dwLength) {
    if (!lpBuffer || dwLength < sizeof(win64_MBI)) return 0;
    win64_MBI* mbi = (win64_MBI*)lpBuffer;
    mbi->BaseAddress = (void*)((uintptr_t)lpAddress & ~4095ULL);
    mbi->AllocationBase = mbi->BaseAddress;
    mbi->AllocationProtect = 0x40; /* PAGE_EXECUTE_READWRITE */
    mbi->__align1 = 0;
    mbi->RegionSize = 0x1000000;   /* 16 MB default region */
    mbi->State = 0x1000;            /* MEM_COMMIT */
    mbi->Protect = 0x40;          /* PAGE_EXECUTE_READWRITE */
    mbi->Type = 0x20000;           /* MEM_PRIVATE */
    mbi->__align2 = 0;
    return sizeof(win64_MBI);
}

__attribute__((ms_abi)) void hook_Sleep(uint32_t dwMilliseconds) {
    usleep(dwMilliseconds * 1000);
}

__attribute__((ms_abi)) uint64_t hook_GetTickCount64(void) {
    return (uint64_t)SDL_GetTicks();
}

__attribute__((ms_abi)) void* hook_GetCurrentProcess(void) { return (void*)(intptr_t)-1; }
__attribute__((ms_abi)) uint32_t hook_GetCurrentProcessId(void) { return 1; }
__attribute__((ms_abi)) void* hook_GetCurrentThread(void) { return (void*)(intptr_t)-2; }
__attribute__((ms_abi)) uint32_t hook_GetCurrentThreadId(void) { return (uint32_t)(uintptr_t)pthread_self(); }

__attribute__((ms_abi)) void hook_InitializeCriticalSection(pthread_mutex_t* cs) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(cs, &attr);
    pthread_mutexattr_destroy(&attr);
}

__attribute__((ms_abi)) void hook_DeleteCriticalSection(pthread_mutex_t* cs) {
    pthread_mutex_destroy(cs);
}

__attribute__((ms_abi)) void hook_EnterCriticalSection(pthread_mutex_t* cs) {
    pthread_mutex_lock(cs);
}

__attribute__((ms_abi)) void hook_LeaveCriticalSection(pthread_mutex_t* cs) {
    pthread_mutex_unlock(cs);
}

__attribute__((ms_abi)) int hook_TryEnterCriticalSection(pthread_mutex_t* cs) {
    return pthread_mutex_trylock(cs) == 0;
}

__attribute__((ms_abi)) uint32_t hook_TlsAlloc(void) {
    pthread_key_t key;
    pthread_key_create(&key, NULL);
    return (uint32_t)key;
}

__attribute__((ms_abi)) void* hook_TlsGetValue(uint32_t dwTlsIndex) {
    return pthread_getspecific((pthread_key_t)dwTlsIndex);
}

__attribute__((ms_abi)) int hook_TlsSetValue(uint32_t dwTlsIndex, void* lpTlsValue) {
    return pthread_setspecific((pthread_key_t)dwTlsIndex, lpTlsValue) == 0;
}

__attribute__((ms_abi)) uint32_t hook_GetModuleFileNameA(void* hModule, char* lpFilename, uint32_t nSize) {
    snprintf(lpFilename, nSize, "/app0/HarvestMoonBTN.exe");
    return strlen(lpFilename);
}

static const uint16_t g_cmdline_w[] = { 'H','a','r','v','e','s','t','M','o','o','n','B','T','N','.','e','x','e', 0 };
__attribute__((ms_abi)) const uint16_t* hook_GetCommandLineW(void) {
    return g_cmdline_w;
}

__attribute__((ms_abi)) uint32_t hook_GetFileAttributesA(const char* lpFileName) {
    const char* real_path = redirect_path(lpFileName, false);
    struct stat st;
    if (stat(real_path, &st) == 0 || (real_path != lpFileName && stat(lpFileName, &st) == 0)) {
        if (S_ISDIR(st.st_mode)) return 0x10; // FILE_ATTRIBUTE_DIRECTORY
        return 0x80; // FILE_ATTRIBUTE_NORMAL
    }
    return 0xFFFFFFFF; // INVALID_FILE_ATTRIBUTES
}

__attribute__((ms_abi)) void hook_OutputDebugStringA(const char* lpOutputString) {
    ps4_log("[WIN_DBG] %s\n", lpOutputString ? lpOutputString : "(null)");
}

__attribute__((ms_abi)) uint32_t hook_GetLastError(void) { return errno; }
__attribute__((ms_abi)) void hook_SetLastError(uint32_t dwErrCode) { errno = dwErrCode; }

__attribute__((ms_abi)) void* hook_CreateEventA(void* lpEventAttributes, int bManualReset, int bInitialState, const char* lpName) {
    int* ev = malloc(sizeof(int));
    *ev = bInitialState;
    return ev;
}

__attribute__((ms_abi)) int hook_SetEvent(void* hEvent) {
    if (hEvent) *(int*)hEvent = 1;
    return 1;
}

__attribute__((ms_abi)) int hook_ResetEvent(void* hEvent) {
    if (hEvent) *(int*)hEvent = 0;
    return 1;
}

__attribute__((ms_abi)) int hook_CloseHandle(void* hObject) {
    if (hm_is_thread_handle(hObject)) {
        /* Thread struct is still referenced by the running thread: detach and leak it. */
        pthread_detach(((hm_thread_t*)hObject)->th);
        return 1;
    }
    if (hObject) free(hObject);
    return 1;
}

__attribute__((ms_abi)) uint32_t hook_WaitForSingleObject(void* hHandle, uint32_t dwMilliseconds) {
    if (hm_is_thread_handle(hHandle)) {
        hm_thread_t* t = (hm_thread_t*)hHandle;
        uint32_t waited = 0;
        while (!t->done) {
            if (dwMilliseconds != 0xFFFFFFFFu && waited >= dwMilliseconds) return 0x102; // WAIT_TIMEOUT
            usleep(1000);
            waited++;
        }
    }
    return 0; // WAIT_OBJECT_0
}

__attribute__((ms_abi)) void* hook_CreateThread(void* lpAttr, size_t stackSize, void* lpStart, void* lpParam, uint32_t flags, uint32_t* lpThreadId) {
    hm_thread_t* t = calloc(1, sizeof(hm_thread_t));
    if (!t) return NULL;
    t->magic = HM_THREAD_MAGIC;
    t->fn = lpStart;
    t->arg = lpParam;
    if (pthread_create(&t->th, NULL, hm_thread_tramp, t) != 0) {
        free(t);
        return NULL;
    }
    if (lpThreadId) *lpThreadId = (uint32_t)(uintptr_t)t->th;
    return t;
}

__attribute__((ms_abi)) void hook_ExitProcess(uint32_t code) {
    printf("[HMBTN PS4] ExitProcess(%u)\n", code);
    exit((int)code);
}

__attribute__((ms_abi)) uint32_t hook_GetTempPathA(uint32_t nBufferLength, char* lpBuffer) {
    mkdir("/data", 0777);
    mkdir("/data/HarvestMoonBTN", 0777);
    mkdir("/data/HarvestMoonBTN/tmp", 0777);
    const char* tmp = "/data/HarvestMoonBTN/tmp/";
    if (lpBuffer && nBufferLength > strlen(tmp)) strcpy(lpBuffer, tmp);
    return (uint32_t)strlen(tmp);
}

__attribute__((ms_abi)) uint32_t hook_WaitForMultipleObjects(uint32_t nCount, void** lpHandles, int bWaitAll, uint32_t dwMilliseconds) {
    return 0; // WAIT_OBJECT_0
}

__attribute__((ms_abi)) void* hook_CreateSemaphoreA(void* lpSemaphoreAttributes, int32_t lInitialCount, int32_t lMaximumCount, const char* lpName) {
    return malloc(16);
}

__attribute__((ms_abi)) int hook_ReleaseSemaphore(void* hSemaphore, int32_t lReleaseCount, int32_t* lpPreviousCount) {
    if (lpPreviousCount) *lpPreviousCount = 1;
    return 1;
}

__attribute__((ms_abi)) int hook_DuplicateHandle(void* hSrcProc, void* hSrc, void* hTgtProc, void** lpTgt, uint32_t access, int inherit, uint32_t options) {
    if (lpTgt) *lpTgt = hSrc;
    return 1;
}

__attribute__((ms_abi)) int hook_GetHandleInformation(void* hObject, uint32_t* lpdwFlags) {
    if (lpdwFlags) *lpdwFlags = 0;
    return 1;
}

__attribute__((ms_abi)) int hook_IsDebuggerPresent(void) { return 0; }
__attribute__((ms_abi)) void* hook_SetUnhandledExceptionFilter(void* lpTopLevelExceptionFilter) { return NULL; }
__attribute__((ms_abi)) void* hook_AddVectoredExceptionHandler(uint32_t First, void* Handler) { return (void*)1; }
__attribute__((ms_abi)) uint32_t hook_RemoveVectoredExceptionHandler(void* Handle) { return 1; }
__attribute__((ms_abi)) void hook_RaiseException(uint32_t dwExceptionCode, uint32_t dwExceptionFlags, uint32_t nNumberOfArguments, const uint64_t* lpArguments) {
    printf("[HMBTN PS4] Exception raised: 0x%X\n", dwExceptionCode);
}

__attribute__((ms_abi)) int hook_GetProcessAffinityMask(void* hProcess, uint64_t* lpProcessAffinityMask, uint64_t* lpSystemAffinityMask) {
    if (lpProcessAffinityMask) *lpProcessAffinityMask = 0xFF; // 8 cores
    if (lpSystemAffinityMask) *lpSystemAffinityMask = 0xFF;
    return 1;
}

__attribute__((ms_abi)) int hook_SetProcessAffinityMask(void* hProcess, uint64_t dwProcessAffinityMask) { return 1; }
__attribute__((ms_abi)) int hook_GetThreadPriority(void* hThread) { return 0; }
__attribute__((ms_abi)) int hook_SetThreadPriority(void* hThread, int nPriority) { return 1; }
__attribute__((ms_abi)) int hook_GetThreadContext(void* hThread, void* lpContext) { return 0; }
__attribute__((ms_abi)) int hook_SetThreadContext(void* hThread, const void* lpContext) { return 0; }
__attribute__((ms_abi)) uint32_t hook_SuspendThread(void* hThread) { return 0; }
__attribute__((ms_abi)) uint32_t hook_ResumeThread(void* hThread) { return 0; }
__attribute__((ms_abi)) void* hook_OpenProcess(uint32_t dwDesiredAccess, int bInheritHandle, uint32_t dwProcessId) { return (void*)1; }

__attribute__((ms_abi)) void hook_GetStartupInfoA(void* lpStartupInfo) {
    memset(lpStartupInfo, 0, 104);
}

__attribute__((ms_abi)) void hook_GetSystemTimeAsFileTime(uint64_t* lpSystemTimeAsFileTime) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t intervals = ((uint64_t)ts.tv_sec * 10000000ULL) + ((uint64_t)ts.tv_nsec / 100ULL) + 116444736000000000ULL;
    if (lpSystemTimeAsFileTime) *lpSystemTimeAsFileTime = intervals;
}

__attribute__((ms_abi)) uint32_t hook_FormatMessageA(uint32_t dwFlags, const void* lpSource, uint32_t dwMessageId, uint32_t dwLanguageId, char* lpBuffer, uint32_t nSize, void* Arguments) {
    return snprintf(lpBuffer, nSize, "Error 0x%X", dwMessageId);
}

__attribute__((ms_abi)) int hook_MultiByteToWideChar(uint32_t CodePage, uint32_t dwFlags, const char* lpMultiByteStr, int cbMultiByte, uint16_t* lpWideCharStr, int cchWideChar) {
    if (!lpMultiByteStr) return 0;
    int len = strlen(lpMultiByteStr);
    if (!lpWideCharStr || cchWideChar == 0) return len + 1;
    int n = (len < cchWideChar - 1) ? len : (cchWideChar - 1);
    for (int i = 0; i < n; i++) lpWideCharStr[i] = (uint16_t)(uint8_t)lpMultiByteStr[i];
    lpWideCharStr[n] = 0;
    return n + 1;
}

__attribute__((ms_abi)) int hook_WideCharToMultiByte(uint32_t CodePage, uint32_t dwFlags, const uint16_t* lpWideCharStr, int cchWideChar, char* lpMultiByteStr, int cbMultiByte, const char* lpDefaultChar, int* lpUsedDefaultChar) {
    if (!lpWideCharStr) return 0;
    int len = 0;
    while (lpWideCharStr[len]) len++;
    if (!lpMultiByteStr || cbMultiByte == 0) return len + 1;
    int n = (len < cbMultiByte - 1) ? len : (cbMultiByte - 1);
    for (int i = 0; i < n; i++) lpMultiByteStr[i] = (char)lpWideCharStr[i];
    lpMultiByteStr[n] = 0;
    return n + 1;
}

__attribute__((ms_abi)) int hook_IsDBCSLeadByteEx(uint32_t CodePage, uint8_t TestChar) {
    return 0;
}

/* --- MS ABI Wrappers for comdlg32 & SHELL32 --- */
/* OPENFILENAMEA (x64): lpstrFilter @0x18, lpstrFile @0x30, nMaxFile @0x38.
 * The disc image picker gets the disc bundled in the package; every other
 * picker (e.g. memory-card import) is cancelled because PS4 has no file dialog. */
__attribute__((ms_abi)) int hook_GetOpenFileNameA(void* lpofn) {
    if (!lpofn) return 0;
    const char* filter = *(const char**)((uint8_t*)lpofn + 0x18);
    char* file = *(char**)((uint8_t*)lpofn + 0x30);
    uint32_t max_file = *(uint32_t*)((uint8_t*)lpofn + 0x38);
    if (filter && file && max_file > 32 && strstr(filter, "Disc image")) {
        snprintf(file, max_file, "%s", HM_DISC_CUE);
        return 1;
    }
    return 0;
}

__attribute__((ms_abi)) void* hook_ShellExecuteA(void* hwnd, const char* op, const char* file, const char* params, const char* dir, int show) {
    printf("[HMBTN PS4] ShellExecute blocked: %s\n", file ? file : "(null)");
    return (void*)(intptr_t)2; /* <= 32 means failure */
}

__attribute__((ms_abi)) int hook_MessageBoxW(void* hwnd, const uint16_t* text, const uint16_t* caption, uint32_t type) {
    char buf[256];
    int i = 0;
    if (text) for (; text[i] && i < 255; i++) buf[i] = (text[i] < 128) ? (char)text[i] : '?';
    buf[i] = 0;
    printf("[HMBTN PS4 DIALOG] %s\n", buf);
    uint32_t kind = type & 0xF;
    if (kind == 3 || kind == 4) return 7; /* Yes/No(/Cancel): answer No (IDNO) */
    return 1; /* IDOK */
}

/* The game passes the bundled disc image on its command line. */
static const uint16_t s_app_path[] = { '/','a','p','p','0','/','H','a','r','v','e','s','t','M','o','o','n','B','T','N','.','e','x','e', 0 };
static const uint16_t s_disc_path[] = { '/','a','p','p','0','/','d','i','s','c','.','c','u','e', 0 };
static const uint16_t* s_wargv[3] = { s_app_path, s_disc_path, NULL };
__attribute__((ms_abi)) const uint16_t** hook_CommandLineToArgvW(const uint16_t* lpCmdLine, int* pNumArgs) {
    if (pNumArgs) *pNumArgs = 2;
    return s_wargv;
}

/* --- MS ABI Wrappers for msvcrt --- */
__attribute__((ms_abi)) FILE** hook___iob_func(void) {
    g_msvcrt_iob[0]._file = 0; g_msvcrt_iob[0]._flag = 1;
    g_msvcrt_iob[1]._file = 1; g_msvcrt_iob[1]._flag = 2;
    g_msvcrt_iob[2]._file = 2; g_msvcrt_iob[2]._flag = 2;
    return (FILE**)g_msvcrt_iob;
}

__attribute__((ms_abi)) int* hook__errno(void) { return &errno; }
__attribute__((ms_abi)) void hook__lock(int no) {}
__attribute__((ms_abi)) void hook__unlock(int no) {}
__attribute__((ms_abi)) void hook__initterm(void** a, void** b) {}
__attribute__((ms_abi)) void hook__cexit(void) {}
__attribute__((ms_abi)) void hook__amsg_exit(int no) {}
__attribute__((ms_abi)) void hook___set_app_type(int at) {}
__attribute__((ms_abi)) void hook___setusermatherr(void* fn) {}
__attribute__((ms_abi)) char** hook__acmdln(void) { static char* a = "HarvestMoonBTN.exe"; return &a; }
static int s_fmode = 0;
__attribute__((ms_abi)) int* hook__fmode(void) { return &s_fmode; }
static int s_commode = 0;
__attribute__((ms_abi)) int* hook__commode(void) { return &s_commode; }
__attribute__((ms_abi)) int hook__ismbblead(uint32_t c) { return 0; }
__attribute__((ms_abi)) uint32_t hook____lc_codepage_func(void) { return 65001; } // UTF-8
__attribute__((ms_abi)) int hook____mb_cur_max_func(void) { return 1; }
__attribute__((ms_abi)) int hook___getmainargs(int* argc, char*** argv, char*** env, int do_wildcard, void* start_info) {
    static char* my_argv[] = {
        "/app0/HarvestMoonBTN.exe", "--lang", NULL, HM_DISC_CUE, NULL
    };
    my_argv[2] = (char*)load_language_preference();
    if (argc) *argc = 4;
    if (argv) *argv = my_argv;
    return 0;
}
__attribute__((ms_abi)) void* hook___C_specific_handler(void) { return NULL; }
__attribute__((ms_abi)) void* hook__onexit(void* fn) { return fn; }

static char* s_initenv_val[] = { NULL };
__attribute__((ms_abi)) char*** hook___initenv(void) { static char** p = s_initenv_val; return &p; }
__attribute__((ms_abi)) uintptr_t hook__beginthreadex(void* sec, unsigned st_sz, void* fn, void* arg, unsigned flags, unsigned* thid) {
    hm_thread_t* t = (hm_thread_t*)calloc(1, sizeof(hm_thread_t));
    if (!t) return 0;
    t->magic = HM_THREAD_MAGIC;
    t->fn = fn;
    t->arg = arg;
    if (pthread_create(&t->th, NULL, hm_thread_tramp, t) != 0) {
        free(t);
        return 0;
    }
    if (thid) *thid = (unsigned)(uintptr_t)t->th;
    return (uintptr_t)t;
}
__attribute__((ms_abi)) void hook__endthreadex(unsigned retval) {
    pthread_exit((void*)(uintptr_t)retval);
}
__attribute__((naked)) int hook__setjmp(void* buf) {
    __asm__ volatile (
        "movq %rbx, 0(%rcx)\n\t"
        "leaq 8(%rsp), %rax\n\t"
        "movq %rax, 8(%rcx)\n\t"
        "movq %rbp, 16(%rcx)\n\t"
        "movq %rsi, 24(%rcx)\n\t"
        "movq %rdi, 32(%rcx)\n\t"
        "movq %r12, 40(%rcx)\n\t"
        "movq %r13, 48(%rcx)\n\t"
        "movq %r14, 56(%rcx)\n\t"
        "movq %r15, 64(%rcx)\n\t"
        "movq (%rsp), %rax\n\t"
        "movq %rax, 72(%rcx)\n\t"
        "xorl %eax, %eax\n\t"
        "ret\n\t"
    );
}
__attribute__((naked)) void hook_longjmp(void* env, int val) {
    __asm__ volatile (
        "movl %edx, %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "movl $1, %eax\n\t"
        "1:\n\t"
        "movq 0(%rcx), %rbx\n\t"
        "movq 8(%rcx), %rsp\n\t"
        "movq 16(%rcx), %rbp\n\t"
        "movq 24(%rcx), %rsi\n\t"
        "movq 32(%rcx), %rdi\n\t"
        "movq 40(%rcx), %r12\n\t"
        "movq 48(%rcx), %r13\n\t"
        "movq 56(%rcx), %r14\n\t"
        "movq 64(%rcx), %r15\n\t"
        "movq 72(%rcx), %rdx\n\t"
        "jmpq *%rdx\n\t"
    );
}
__attribute__((ms_abi)) void* hook_signal(int sig, void* func) { return NULL; }

__attribute__((ms_abi)) int hook_stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
__attribute__((ms_abi)) int hook_strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
__attribute__((ms_abi)) char* hook_strdup(const char* s) { return strdup(s); }
__attribute__((ms_abi)) int hook_mkdir(const char* p) {
    const char* real_p = redirect_path(p, true);
    return mkdir(real_p, 0777);
}

__attribute__((ms_abi)) int hook_stat64(const char* path, struct _stat64_win* buffer) {
    const char* real_path = redirect_path(path, false);
    int fd = open(real_path, O_RDONLY);
    if (fd < 0 && real_path != path) {
        fd = open(path, O_RDONLY);
    }
    if (fd < 0) {
        return -1;
    }
    off_t fsize = lseek(fd, 0, SEEK_END);
    close(fd);

    memset(buffer, 0, sizeof(*buffer));
    buffer->st_mode = 0100666; /* S_IFREG | 0666 */
    buffer->st_size = fsize;
    buffer->st_mtime = time(NULL);
    buffer->st_atime = time(NULL);
    buffer->st_ctime = time(NULL);
    return 0;
}

__attribute__((ms_abi)) int64_t hook_findfirst64(const char* filespec, struct _finddata64_t* fileinfo) {
    find_handle_t* h = malloc(sizeof(find_handle_t));
    char dirpath[260];
    const char* last_slash = strrchr(filespec, '/');
    if (!last_slash) last_slash = strrchr(filespec, '\\');
    if (last_slash) {
        size_t len = last_slash - filespec;
        memcpy(dirpath, filespec, len);
        dirpath[len] = '\0';
    } else {
        strcpy(dirpath, ".");
    }
    h->dir = opendir(dirpath);
    if (!h->dir) { free(h); return -1; }
    struct dirent* de = readdir(h->dir);
    if (!de) { closedir(h->dir); free(h); return -1; }
    memset(fileinfo, 0, sizeof(*fileinfo));
    strncpy(fileinfo->name, de->d_name, 259);
    return (int64_t)(intptr_t)h;
}

__attribute__((ms_abi)) int hook_findnext64(int64_t handle, struct _finddata64_t* fileinfo) {
    find_handle_t* h = (find_handle_t*)(intptr_t)handle;
    if (!h || !h->dir) return -1;
    struct dirent* de = readdir(h->dir);
    if (!de) return -1;
    memset(fileinfo, 0, sizeof(*fileinfo));
    strncpy(fileinfo->name, de->d_name, 259);
    return 0;
}

__attribute__((ms_abi)) int hook_findclose(int64_t handle) {
    find_handle_t* h = (find_handle_t*)(intptr_t)handle;
    if (h) {
        if (h->dir) closedir(h->dir);
        free(h);
    }
    return 0;
}

__attribute__((ms_abi)) struct tm* hook_localtime64(const int64_t* timep) {
    time_t t = (time_t)*timep;
    return localtime(&t);
}

__attribute__((ms_abi)) char* hook_ultoa(unsigned long val, char* buf, int radix) {
    if (radix == 10) sprintf(buf, "%lu", val);
    else if (radix == 16) sprintf(buf, "%lx", val);
    else sprintf(buf, "%lu", val);
    return buf;
}

__attribute__((ms_abi)) char* hook_fullpath(char* userBuf, const char* path, size_t maxLen) {
    return realpath(path, userBuf);
}

__attribute__((ms_abi)) void* hook_malloc(size_t size) { return malloc(size); }
__attribute__((ms_abi)) void hook_free(void* ptr) { free(ptr); }
__attribute__((ms_abi)) void* hook_calloc(size_t num, size_t size) { return calloc(num, size); }
__attribute__((ms_abi)) void* hook_realloc(void* ptr, size_t size) { return realloc(ptr, size); }
__attribute__((ms_abi)) void* hook_memcpy(void* dst, const void* src, size_t n) { return memcpy(dst, src, n); }
__attribute__((ms_abi)) void* hook_memmove(void* dst, const void* src, size_t n) { return memmove(dst, src, n); }
__attribute__((ms_abi)) void* hook_memset(void* s, int c, size_t n) { return memset(s, c, n); }
__attribute__((ms_abi)) int hook_memcmp(const void* s1, const void* s2, size_t n) { return memcmp(s1, s2, n); }

__attribute__((ms_abi)) size_t hook_strlen(const char* s) {
    if (!s) return 0;
    return strlen(s);
}
__attribute__((ms_abi)) char* hook_strcpy(char* d, const char* s) {
    if (!d) return NULL;
    if (!s) { *d = 0; return d; }
    return strcpy(d, s);
}
__attribute__((ms_abi)) char* hook_strncpy(char* d, const char* s, size_t n) {
    if (!d || n == 0) return d;
    if (!s) { *d = 0; return d; }
    return strncpy(d, s, n);
}
__attribute__((ms_abi)) int hook_strcmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return (!s1 && !s2) ? 0 : (s1 ? 1 : -1);
    return strcmp(s1, s2);
}
__attribute__((ms_abi)) int hook_strncmp(const char* s1, const char* s2, size_t n) {
    if (!s1 || !s2 || n == 0) return (!s1 && !s2) ? 0 : (s1 ? 1 : -1);
    return strncmp(s1, s2, n);
}
__attribute__((ms_abi)) char* hook_strchr(const char* s, int c) {
    if (!s) return NULL;
    return strchr(s, c);
}
__attribute__((ms_abi)) char* hook_strrchr(const char* s, int c) {
    if (!s) return NULL;
    return strrchr(s, c);
}
__attribute__((ms_abi)) char* hook_strstr(const char* h, const char* n) {
    if (!h || !n) return NULL;
    return strstr(h, n);
}
__attribute__((ms_abi)) long hook_strtol(const char* s, char** e, int b) {
    if (!s) return 0;
    return strtol(s, e, b);
}
__attribute__((ms_abi)) int hook_atoi(const char* s) {
    if (!s) return 0;
    return atoi(s);
}
__attribute__((ms_abi)) long hook_atol(const char* s) {
    if (!s) return 0;
    return atol(s);
}
__attribute__((ms_abi)) int hook_toupper(int c) { return toupper(c); }

__attribute__((ms_abi)) FILE* hook_fopen(const char* path, const char* mode) {
    if (!path || !mode) return NULL;
    bool for_write = (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'));
    const char* real_path = redirect_path(path, for_write);
    FILE* f = fopen(real_path, mode);
    if (!f && !for_write && real_path != path) {
        f = fopen(path, mode);
    }
    if (!f && !for_write && path[0] != '/') {
        char fallback_app0[512];
        snprintf(fallback_app0, sizeof(fallback_app0), "/app0/%s", path);
        f = fopen(fallback_app0, mode);
    }
    ps4_log("[HMBTN PS4] fopen('%s', '%s') -> %p\n", path, mode, f);
    return f;
}

static inline int is_msvcrt_std(FILE* stream) {
    if (!stream || stream == stderr || stream == stdout || stream == stdin) return 1;
    if ((uintptr_t)stream >= (uintptr_t)g_msvcrt_iob &&
        (uintptr_t)stream < (uintptr_t)(g_msvcrt_iob + 20)) return 1;
    return 0;
}

__attribute__((ms_abi)) int hook_fclose(FILE* stream) {
    if (is_msvcrt_std(stream)) return 0;
    return fclose(stream);
}
__attribute__((ms_abi)) size_t hook_fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (is_msvcrt_std(stream)) return 0;
    return fread(ptr, size, nmemb, stream);
}
__attribute__((ms_abi)) size_t hook_fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (is_msvcrt_std(stream)) {
        if (g_log_fp && ptr && size && nmemb) {
            fwrite(ptr, size, nmemb, g_log_fp);
            fflush(g_log_fp);
        }
        return nmemb;
    }
    return fwrite(ptr, size, nmemb, stream);
}
__attribute__((ms_abi)) int hook_fseek(FILE* stream, int32_t offset, int whence) {
    if (is_msvcrt_std(stream)) return 0;
    return fseek(stream, (long)offset, whence);
}
__attribute__((ms_abi)) long hook_ftell(FILE* stream) {
    if (is_msvcrt_std(stream)) return 0;
    return ftell(stream);
}
__attribute__((ms_abi)) char* hook_fgets(char* s, int size, FILE* stream) {
    if (is_msvcrt_std(stream)) return NULL;
    return fgets(s, size, stream);
}
__attribute__((ms_abi)) int hook_fputc(int c, FILE* stream) {
    if (is_msvcrt_std(stream)) {
        if (g_log_fp) { fputc(c, g_log_fp); fflush(g_log_fp); }
        return c;
    }
    return fputc(c, stream);
}
__attribute__((ms_abi)) int hook_fputs(const char* s, FILE* stream) {
    if (is_msvcrt_std(stream)) {
        if (g_log_fp && s) { fputs(s, g_log_fp); fflush(g_log_fp); }
        return 0;
    }
    return fputs(s, stream);
}
__attribute__((ms_abi)) int hook_fflush(FILE* stream) {
    if (is_msvcrt_std(stream)) {
        if (g_log_fp) fflush(g_log_fp);
        return 0;
    }
    return fflush(stream);
}
__attribute__((ms_abi)) void hook_rewind(FILE* stream) {
    if (is_msvcrt_std(stream)) return;
    rewind(stream);
}
__attribute__((ms_abi)) int hook__fileno(FILE* stream) {
    if (is_msvcrt_std(stream)) return 1;
    return fileno(stream);
}
__attribute__((ms_abi)) int hook__commit(int fd) { return fsync(fd); }
__attribute__((ms_abi)) int64_t hook__time64(int64_t* t) {
    int64_t now = (int64_t)time(NULL);
    if (t) *t = now;
    return now;
}

__attribute__((ms_abi)) int hook_printf(const char* fmt, void* a1, void* a2, void* a3, void* a4) {
    ps4_log(fmt, a1, a2, a3, a4);
    return 0;
}
__attribute__((ms_abi)) int hook_fprintf(FILE* stream, const char* fmt, void* a1, void* a2, void* a3, void* a4) {
    if (is_msvcrt_std(stream)) {
        ps4_log(fmt, a1, a2, a3, a4);
        return 0;
    }
    return fprintf(stream, fmt, a1, a2, a3, a4);
}
__attribute__((ms_abi)) int hook_vfprintf(FILE* stream, const char* fmt, void* ap) {
    if (fmt) ps4_log("%s", fmt);
    return 0;
}

__attribute__((ms_abi)) void hook_exit(int status) {
    ps4_log("[HMBTN PS4] exit(%d) called\n", status);
    exit(status);
}
__attribute__((ms_abi)) void hook_abort(void) {
    ps4_log("[HMBTN PS4] abort() called!\n");
    abort();
}
__attribute__((ms_abi)) char* hook_getenv(const char* name) {
    if (name && strcmp(name, "HMBTN_LANG") == 0) {
        return (char*)load_language_preference();
    }
    return getenv(name);
}
__attribute__((ms_abi)) int hook_remove(const char* path) {
    const char* real_path = redirect_path(path, true);
    return remove(real_path);
}
__attribute__((ms_abi)) int hook_rename(const char* oldp, const char* newp) {
    const char* real_old = redirect_path(oldp, false);
    const char* real_new = redirect_path(newp, true);
    return rename(real_old, real_new);
}
__attribute__((ms_abi)) size_t hook_strftime(char* s, size_t max, const char* format, const struct tm* tm) { return strftime(s, max, format, tm); }
__attribute__((ms_abi)) void* hook_localeconv(void) { return ""; }
__attribute__((ms_abi)) char* hook_strerror(int errnum) { return strerror(errnum); }
__attribute__((ms_abi)) void hook__assert(const char* msg, const char* file, unsigned line) {
    printf("[HMBTN PS4 ASSERT] %s:%u: %s\n", file, line, msg);
}

/* --- Import Lookup Table --- */
typedef struct {
    const char* dll;
    const char* sym;
    void* func;
} ImportMapping;

static const ImportMapping g_imports[] = {
    { "SDL2.dll", "SDL_ClearQueuedAudio", (void*)hook_SDL_ClearQueuedAudio },
    { "SDL2.dll", "SDL_CreateRenderer", (void*)hook_SDL_CreateRenderer },
    { "SDL2.dll", "SDL_CreateTexture", (void*)hook_SDL_CreateTexture },
    { "SDL2.dll", "SDL_CreateWindow", (void*)hook_SDL_CreateWindow },
    { "SDL2.dll", "SDL_Delay", (void*)hook_SDL_Delay },
    { "SDL2.dll", "SDL_DestroyRenderer", (void*)hook_SDL_DestroyRenderer },
    { "SDL2.dll", "SDL_DestroyTexture", (void*)hook_SDL_DestroyTexture },
    { "SDL2.dll", "SDL_DestroyWindow", (void*)hook_SDL_DestroyWindow },
    { "SDL2.dll", "SDL_GameControllerAddMappingsFromRW", (void*)hook_SDL_GameControllerAddMappingsFromRW },
    { "SDL2.dll", "SDL_GameControllerClose", (void*)hook_SDL_GameControllerClose },
    { "SDL2.dll", "SDL_GameControllerGetAxis", (void*)hook_SDL_GameControllerGetAxis },
    { "SDL2.dll", "SDL_GameControllerGetButton", (void*)hook_SDL_GameControllerGetButton },
    { "SDL2.dll", "SDL_GameControllerGetJoystick", (void*)hook_SDL_GameControllerGetJoystick },
    { "SDL2.dll", "SDL_GameControllerName", (void*)hook_SDL_GameControllerName },
    { "SDL2.dll", "SDL_GameControllerOpen", (void*)hook_SDL_GameControllerOpen },
    { "SDL2.dll", "SDL_GetBasePath", (void*)hook_SDL_GetBasePath },
    { "SDL2.dll", "SDL_GetCPUCount", (void*)hook_SDL_GetCPUCount },
    { "SDL2.dll", "SDL_GetError", (void*)hook_SDL_GetError },
    { "SDL2.dll", "SDL_GetKeyFromName", (void*)hook_SDL_GetKeyFromName },
    { "SDL2.dll", "SDL_GetKeyName", (void*)hook_SDL_GetKeyName },
    { "SDL2.dll", "SDL_GetPerformanceCounter", (void*)hook_SDL_GetPerformanceCounter },
    { "SDL2.dll", "SDL_GetPerformanceFrequency", (void*)hook_SDL_GetPerformanceFrequency },
    { "SDL2.dll", "SDL_GetPrefPath", (void*)hook_SDL_GetPrefPath },
    { "SDL2.dll", "SDL_GetQueuedAudioSize", (void*)hook_SDL_GetQueuedAudioSize },
    { "SDL2.dll", "SDL_Init", (void*)hook_SDL_Init },
    { "SDL2.dll", "SDL_InitSubSystem", (void*)hook_SDL_InitSubSystem },
    { "SDL2.dll", "SDL_IsGameController", (void*)hook_SDL_IsGameController },
    { "SDL2.dll", "SDL_JoystickClose", (void*)hook_SDL_JoystickClose },
    { "SDL2.dll", "SDL_JoystickGetAxis", (void*)hook_SDL_JoystickGetAxis },
    { "SDL2.dll", "SDL_JoystickGetButton", (void*)hook_SDL_JoystickGetButton },
    { "SDL2.dll", "SDL_JoystickGetHat", (void*)hook_SDL_JoystickGetHat },
    { "SDL2.dll", "SDL_JoystickInstanceID", (void*)hook_SDL_JoystickInstanceID },
    { "SDL2.dll", "SDL_JoystickName", (void*)hook_SDL_JoystickName },
    { "SDL2.dll", "SDL_JoystickNumAxes", (void*)hook_SDL_JoystickNumAxes },
    { "SDL2.dll", "SDL_JoystickNumButtons", (void*)hook_SDL_JoystickNumButtons },
    { "SDL2.dll", "SDL_JoystickNumHats", (void*)hook_SDL_JoystickNumHats },
    { "SDL2.dll", "SDL_JoystickOpen", (void*)hook_SDL_JoystickOpen },
    { "SDL2.dll", "SDL_NumJoysticks", (void*)hook_SDL_NumJoysticks },
    { "SDL2.dll", "SDL_OpenAudioDevice", (void*)hook_SDL_OpenAudioDevice },
    { "SDL2.dll", "SDL_PauseAudioDevice", (void*)hook_SDL_PauseAudioDevice },
    { "SDL2.dll", "SDL_PollEvent", (void*)hook_SDL_PollEvent },
    { "SDL2.dll", "SDL_QueueAudio", (void*)hook_SDL_QueueAudio },
    { "SDL2.dll", "SDL_RWFromFile", (void*)hook_SDL_RWFromFile },
    { "SDL2.dll", "SDL_RenderClear", (void*)hook_SDL_RenderClear },
    { "SDL2.dll", "SDL_RenderCopy", (void*)hook_SDL_RenderCopy },
    { "SDL2.dll", "SDL_RenderDrawRect", (void*)hook_SDL_RenderDrawRect },
    { "SDL2.dll", "SDL_RenderFillRect", (void*)hook_SDL_RenderFillRect },
    { "SDL2.dll", "SDL_RenderPresent", (void*)hook_SDL_RenderPresent },
    { "SDL2.dll", "SDL_RenderSetLogicalSize", (void*)hook_SDL_RenderSetLogicalSize },
    { "SDL2.dll", "SDL_SetHint", (void*)hook_SDL_SetHint },
    { "SDL2.dll", "SDL_SetMainReady", (void*)hook_SDL_SetMainReady },
    { "SDL2.dll", "SDL_SetRenderDrawColor", (void*)hook_SDL_SetRenderDrawColor },
    { "SDL2.dll", "SDL_SetWindowFullscreen", (void*)hook_SDL_SetWindowFullscreen },
    { "SDL2.dll", "SDL_SetWindowTitle", (void*)hook_SDL_SetWindowTitle },
    { "SDL2.dll", "SDL_ShowMessageBox", (void*)hook_SDL_ShowMessageBox },
    { "SDL2.dll", "SDL_ShowSimpleMessageBox", (void*)hook_SDL_ShowSimpleMessageBox },
    { "SDL2.dll", "SDL_UpdateTexture", (void*)hook_SDL_UpdateTexture },
    { "SDL2.dll", "SDL_free", (void*)hook_SDL_free },
    { "SDL2.dll", "SDL_iconv_string", (void*)hook_SDL_iconv_string },
    { "SDL2.dll", "SDL_memcpy", (void*)hook_SDL_memcpy },
    { "SDL2.dll", "SDL_strlen", (void*)hook_SDL_strlen },
    { "SDL2.dll", "SDL_wcslen", (void*)hook_SDL_wcslen },
    { "comdlg32.dll", "GetOpenFileNameA", (void*)hook_GetOpenFileNameA },
    { "KERNEL32.dll", "AddVectoredExceptionHandler", (void*)hook_AddVectoredExceptionHandler },
    { "KERNEL32.dll", "CloseHandle", (void*)hook_CloseHandle },
    { "KERNEL32.dll", "CreateEventA", (void*)hook_CreateEventA },
    { "KERNEL32.dll", "CreateSemaphoreA", (void*)hook_CreateSemaphoreA },
    { "KERNEL32.dll", "CreateThread", (void*)hook_CreateThread },
    { "KERNEL32.dll", "DeleteCriticalSection", (void*)hook_DeleteCriticalSection },
    { "KERNEL32.dll", "DuplicateHandle", (void*)hook_DuplicateHandle },
    { "KERNEL32.dll", "EnterCriticalSection", (void*)hook_EnterCriticalSection },
    { "KERNEL32.dll", "ExitProcess", (void*)hook_ExitProcess },
    { "KERNEL32.dll", "FormatMessageA", (void*)hook_FormatMessageA },
    { "KERNEL32.dll", "GetCommandLineW", (void*)hook_GetCommandLineW },
    { "KERNEL32.dll", "GetCurrentProcess", (void*)hook_GetCurrentProcess },
    { "KERNEL32.dll", "GetCurrentProcessId", (void*)hook_GetCurrentProcessId },
    { "KERNEL32.dll", "GetCurrentThread", (void*)hook_GetCurrentThread },
    { "KERNEL32.dll", "GetCurrentThreadId", (void*)hook_GetCurrentThreadId },
    { "KERNEL32.dll", "GetFileAttributesA", (void*)hook_GetFileAttributesA },
    { "KERNEL32.dll", "GetHandleInformation", (void*)hook_GetHandleInformation },
    { "KERNEL32.dll", "GetLastError", (void*)hook_GetLastError },
    { "KERNEL32.dll", "GetModuleFileNameA", (void*)hook_GetModuleFileNameA },
    { "KERNEL32.dll", "GetProcessAffinityMask", (void*)hook_GetProcessAffinityMask },
    { "KERNEL32.dll", "GetProcessHeap", (void*)hook_GetProcessHeap },
    { "KERNEL32.dll", "GetStartupInfoA", (void*)hook_GetStartupInfoA },
    { "KERNEL32.dll", "GetSystemTimeAsFileTime", (void*)hook_GetSystemTimeAsFileTime },
    { "KERNEL32.dll", "GetTempPathA", (void*)hook_GetTempPathA },
    { "KERNEL32.dll", "GetThreadContext", (void*)hook_GetThreadContext },
    { "KERNEL32.dll", "GetThreadPriority", (void*)hook_GetThreadPriority },
    { "KERNEL32.dll", "GetTickCount64", (void*)hook_GetTickCount64 },
    { "KERNEL32.dll", "HeapAlloc", (void*)hook_HeapAlloc },
    { "KERNEL32.dll", "HeapFree", (void*)hook_HeapFree },
    { "KERNEL32.dll", "InitializeCriticalSection", (void*)hook_InitializeCriticalSection },
    { "KERNEL32.dll", "IsDBCSLeadByteEx", (void*)hook_IsDBCSLeadByteEx },
    { "KERNEL32.dll", "IsDebuggerPresent", (void*)hook_IsDebuggerPresent },
    { "KERNEL32.dll", "LeaveCriticalSection", (void*)hook_LeaveCriticalSection },
    { "KERNEL32.dll", "LocalFree", (void*)hook_LocalFree },
    { "KERNEL32.dll", "MultiByteToWideChar", (void*)hook_MultiByteToWideChar },
    { "KERNEL32.dll", "OpenProcess", (void*)hook_OpenProcess },
    { "KERNEL32.dll", "OutputDebugStringA", (void*)hook_OutputDebugStringA },
    { "KERNEL32.dll", "RaiseException", (void*)hook_RaiseException },
    { "KERNEL32.dll", "ReleaseSemaphore", (void*)hook_ReleaseSemaphore },
    { "KERNEL32.dll", "RemoveVectoredExceptionHandler", (void*)hook_RemoveVectoredExceptionHandler },
    { "KERNEL32.dll", "ResetEvent", (void*)hook_ResetEvent },
    { "KERNEL32.dll", "ResumeThread", (void*)hook_ResumeThread },
    { "KERNEL32.dll", "SetEvent", (void*)hook_SetEvent },
    { "KERNEL32.dll", "SetLastError", (void*)hook_SetLastError },
    { "KERNEL32.dll", "SetProcessAffinityMask", (void*)hook_SetProcessAffinityMask },
    { "KERNEL32.dll", "SetThreadContext", (void*)hook_SetThreadContext },
    { "KERNEL32.dll", "SetThreadPriority", (void*)hook_SetThreadPriority },
    { "KERNEL32.dll", "SetUnhandledExceptionFilter", (void*)hook_SetUnhandledExceptionFilter },
    { "KERNEL32.dll", "Sleep", (void*)hook_Sleep },
    { "KERNEL32.dll", "SuspendThread", (void*)hook_SuspendThread },
    { "KERNEL32.dll", "TlsAlloc", (void*)hook_TlsAlloc },
    { "KERNEL32.dll", "TlsGetValue", (void*)hook_TlsGetValue },
    { "KERNEL32.dll", "TlsSetValue", (void*)hook_TlsSetValue },
    { "KERNEL32.dll", "TryEnterCriticalSection", (void*)hook_TryEnterCriticalSection },
    { "KERNEL32.dll", "VirtualAlloc", (void*)hook_VirtualAlloc },
    { "KERNEL32.dll", "VirtualFree", (void*)hook_VirtualFree },
    { "KERNEL32.dll", "VirtualProtect", (void*)hook_VirtualProtect },
    { "KERNEL32.dll", "VirtualQuery", (void*)hook_VirtualQuery },
    { "KERNEL32.dll", "WaitForMultipleObjects", (void*)hook_WaitForMultipleObjects },
    { "KERNEL32.dll", "WaitForSingleObject", (void*)hook_WaitForSingleObject },
    { "KERNEL32.dll", "WideCharToMultiByte", (void*)hook_WideCharToMultiByte },
    { "msvcrt.dll", "__C_specific_handler", (void*)hook___C_specific_handler },
    { "msvcrt.dll", "___lc_codepage_func", (void*)hook____lc_codepage_func },
    { "msvcrt.dll", "___mb_cur_max_func", (void*)hook____mb_cur_max_func },
    { "msvcrt.dll", "__getmainargs", (void*)hook___getmainargs },
    { "msvcrt.dll", "__initenv", (void*)hook___initenv },
    { "msvcrt.dll", "__iob_func", (void*)hook___iob_func },
    { "msvcrt.dll", "__set_app_type", (void*)hook___set_app_type },
    { "msvcrt.dll", "__setusermatherr", (void*)hook___setusermatherr },
    { "msvcrt.dll", "_acmdln", (void*)hook__acmdln },
    { "msvcrt.dll", "_amsg_exit", (void*)hook__amsg_exit },
    { "msvcrt.dll", "_assert", (void*)hook__assert },
    { "msvcrt.dll", "_beginthreadex", (void*)hook__beginthreadex },
    { "msvcrt.dll", "_cexit", (void*)hook__cexit },
    { "msvcrt.dll", "_commit", (void*)hook__commit },
    { "msvcrt.dll", "_commode", (void*)hook__commode },
    { "msvcrt.dll", "_endthreadex", (void*)hook__endthreadex },
    { "msvcrt.dll", "_errno", (void*)hook__errno },
    { "msvcrt.dll", "_fileno", (void*)hook__fileno },
    { "msvcrt.dll", "_findclose", (void*)hook_findclose },
    { "msvcrt.dll", "_findfirst64", (void*)hook_findfirst64 },
    { "msvcrt.dll", "_findnext64", (void*)hook_findnext64 },
    { "msvcrt.dll", "_fmode", (void*)hook__fmode },
    { "msvcrt.dll", "_fullpath", (void*)hook_fullpath },
    { "msvcrt.dll", "_initterm", (void*)hook__initterm },
    { "msvcrt.dll", "_ismbblead", (void*)hook__ismbblead },
    { "msvcrt.dll", "_localtime64", (void*)hook_localtime64 },
    { "msvcrt.dll", "_lock", (void*)hook__lock },
    { "msvcrt.dll", "_onexit", (void*)hook__onexit },
    { "msvcrt.dll", "_setjmp", (void*)hook__setjmp },
    { "msvcrt.dll", "_stat64", (void*)hook_stat64 },
    { "msvcrt.dll", "_stricmp", (void*)hook_stricmp },
    { "msvcrt.dll", "_strnicmp", (void*)hook_strnicmp },
    { "msvcrt.dll", "_time64", (void*)hook__time64 },
    { "msvcrt.dll", "_ultoa", (void*)hook_ultoa },
    { "msvcrt.dll", "_unlock", (void*)hook__unlock },
    { "msvcrt.dll", "abort", (void*)hook_abort },
    { "msvcrt.dll", "atoi", (void*)hook_atoi },
    { "msvcrt.dll", "atol", (void*)hook_atol },
    { "msvcrt.dll", "calloc", (void*)hook_calloc },
    { "msvcrt.dll", "exit", (void*)hook_exit },
    { "msvcrt.dll", "fclose", (void*)hook_fclose },
    { "msvcrt.dll", "fflush", (void*)hook_fflush },
    { "msvcrt.dll", "fgets", (void*)hook_fgets },
    { "msvcrt.dll", "fopen", (void*)hook_fopen },
    { "msvcrt.dll", "fprintf", (void*)hook_fprintf },
    { "msvcrt.dll", "fputc", (void*)hook_fputc },
    { "msvcrt.dll", "fputs", (void*)hook_fputs },
    { "msvcrt.dll", "fread", (void*)hook_fread },
    { "msvcrt.dll", "free", (void*)hook_free },
    { "msvcrt.dll", "fseek", (void*)hook_fseek },
    { "msvcrt.dll", "ftell", (void*)hook_ftell },
    { "msvcrt.dll", "fwrite", (void*)hook_fwrite },
    { "msvcrt.dll", "getenv", (void*)hook_getenv },
    { "msvcrt.dll", "localeconv", (void*)hook_localeconv },
    { "msvcrt.dll", "longjmp", (void*)hook_longjmp },
    { "msvcrt.dll", "malloc", (void*)hook_malloc },
    { "msvcrt.dll", "memcmp", (void*)hook_memcmp },
    { "msvcrt.dll", "memcpy", (void*)hook_memcpy },
    { "msvcrt.dll", "memmove", (void*)hook_memmove },
    { "msvcrt.dll", "memset", (void*)hook_memset },
    { "msvcrt.dll", "printf", (void*)hook_printf },
    { "msvcrt.dll", "realloc", (void*)hook_realloc },
    { "msvcrt.dll", "remove", (void*)hook_remove },
    { "msvcrt.dll", "rename", (void*)hook_rename },
    { "msvcrt.dll", "rewind", (void*)hook_rewind },
    { "msvcrt.dll", "signal", (void*)hook_signal },
    { "msvcrt.dll", "strchr", (void*)hook_strchr },
    { "msvcrt.dll", "strcmp", (void*)hook_strcmp },
    { "msvcrt.dll", "strcpy", (void*)hook_strcpy },
    { "msvcrt.dll", "strerror", (void*)hook_strerror },
    { "msvcrt.dll", "strftime", (void*)hook_strftime },
    { "msvcrt.dll", "strlen", (void*)hook_strlen },
    { "msvcrt.dll", "strncmp", (void*)hook_strncmp },
    { "msvcrt.dll", "strncpy", (void*)hook_strncpy },
    { "msvcrt.dll", "strrchr", (void*)hook_strrchr },
    { "msvcrt.dll", "strstr", (void*)hook_strstr },
    { "msvcrt.dll", "strtol", (void*)hook_strtol },
    { "msvcrt.dll", "toupper", (void*)hook_toupper },
    { "msvcrt.dll", "vfprintf", (void*)hook_vfprintf },
    { "msvcrt.dll", "wcslen", (void*)hook_wcslen },
    { "msvcrt.dll", "_strdup", (void*)hook_strdup },
    { "msvcrt.dll", "_mkdir", (void*)hook_mkdir },
    { "SHELL32.dll", "CommandLineToArgvW", (void*)hook_CommandLineToArgvW },
    { "SHELL32.dll", "ShellExecuteA", (void*)hook_ShellExecuteA },
    { "USER32.dll", "MessageBoxW", (void*)hook_MessageBoxW },
    { "WINHTTP.dll", "WinHttpCloseHandle", (void*)hook_WinHttpCloseHandle },
    { "WINHTTP.dll", "WinHttpConnect", (void*)hook_WinHttpConnect },
    { "WINHTTP.dll", "WinHttpCrackUrl", (void*)hook_WinHttpCrackUrl },
    { "WINHTTP.dll", "WinHttpOpen", (void*)hook_WinHttpOpen },
    { "WINHTTP.dll", "WinHttpOpenRequest", (void*)hook_WinHttpOpenRequest },
    { "WINHTTP.dll", "WinHttpQueryDataAvailable", (void*)hook_WinHttpQueryDataAvailable },
    { "WINHTTP.dll", "WinHttpQueryHeaders", (void*)hook_WinHttpQueryHeaders },
    { "WINHTTP.dll", "WinHttpReadData", (void*)hook_WinHttpReadData },
    { "WINHTTP.dll", "WinHttpReceiveResponse", (void*)hook_WinHttpReceiveResponse },
    { "WINHTTP.dll", "WinHttpSendRequest", (void*)hook_WinHttpSendRequest },
    { "WINHTTP.dll", "WinHttpSetTimeouts", (void*)hook_WinHttpSetTimeouts },
    { NULL, NULL, NULL }
};


/* --- PE Loading & Relocation --- */
__attribute__((ms_abi)) static uint64_t generic_stub_func(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    ps4_log("[HMBTN PS4 STUB] Called unresolved stub function (args: 0x%llx, 0x%llx, 0x%llx, 0x%llx)\n",
        (unsigned long long)a, (unsigned long long)b, (unsigned long long)c, (unsigned long long)d);
    if (g_log_fp) fflush(g_log_fp);
    return 0;
}

static void* resolve_import(const char* dll, const char* sym) {
    for (int i = 0; g_imports[i].sym != NULL; i++) {
        if (strcmp(g_imports[i].sym, sym) == 0) {
            return g_imports[i].func;
        }
    }
    ps4_log("[HMBTN PS4 LOADER WARNING] Unresolved import: %s -> %s (redirected to generic stub)\n", dll, sym);
    if (g_log_fp) fflush(g_log_fp);
    return (void*)generic_stub_func;
}

int main(int argc, char* argv[]) {
    /* Install crash signal handler with detailed context */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.__sa_handler.__sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | 0x0040;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);

    /* Initialize PS4 User Service and Pad early */
    int ret_user = sceUserServiceInitialize(NULL);
    ps4_log("[HMBTN PS4] sceUserServiceInitialize -> 0x%08X\n", ret_user);

    int ret_pad = scePadInit();
    ps4_log("[HMBTN PS4] scePadInit -> 0x%08X\n", ret_pad);

    /* Open default pad handle early */
    int32_t init_user = 0;
    if (sceUserServiceGetInitialUser(&init_user) == 0 && init_user > 0) {
        int h = scePadOpen(init_user, 0, 0, NULL);
        ps4_log("[HMBTN PS4] Early scePadOpen(user=0x%X) -> %d\n", init_user, h);
        if (h > 0) s_pad_handle = h;
    }
    /* Hide PS4 ShellCore splash screen early */
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    int ret_splash = sceSystemServiceHideSplashScreen();
    ps4_log("[HMBTN PS4] Early sceSystemServiceHideSplashScreen -> 0x%08X\n", ret_splash);
    if (g_log_fp) fflush(g_log_fp);

    /* Switch working directory to /app0 */
    chdir("/app0");

    ps4_log("========================================================\n");
    ps4_log("[HMBTN PS4] Harvest Moon: Back to Nature (PS4 Native)\n");
    ps4_log("[HMBTN PS4] Mod Penyu v1.3.5 | Target: 1080p 16:9 Native\n");
    ps4_log("========================================================\n");

    const char* exe_path = "/app0/HarvestMoonBTN.exe";
    int fd = open(exe_path, O_RDONLY);
    if (fd < 0) {
        ps4_log("[HMBTN PS4 ERROR] Cannot open %s: %s\n", exe_path, strerror(errno));
        return 1;
    }

    off_t exe_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    ps4_log("[HMBTN PS4] Found executable (%lld bytes).\n", (long long)exe_size);

    if (exe_size <= 0) {
        ps4_log("[HMBTN PS4 ERROR] Invalid executable size: %lld\n", (long long)exe_size);
        close(fd);
        return 1;
    }

    uint8_t* exe_data = malloc(exe_size);
    if (!exe_data) {
        ps4_log("[HMBTN PS4 ERROR] malloc failed for %lld bytes\n", (long long)exe_size);
        close(fd);
        return 1;
    }

    size_t total_read = 0;
    while (total_read < (size_t)exe_size) {
        ssize_t n = read(fd, exe_data + total_read, (size_t)exe_size - total_read);
        if (n <= 0) break;
        total_read += (size_t)n;
    }
    close(fd);
    ps4_log("[HMBTN PS4] Successfully read %zu bytes into memory.\n", total_read);


    /* Read DOS and NT headers */
    uint32_t pe_offset = *(uint32_t*)(exe_data + 0x3C);
    uint8_t* nt_header = exe_data + pe_offset;
    uint64_t image_base = *(uint64_t*)(nt_header + 0x30);
    uint32_t size_of_image = *(uint32_t*)(nt_header + 0x50);
    uint32_t entry_point_rva = *(uint32_t*)(nt_header + 0x28);
    uint16_t num_sections = *(uint16_t*)(nt_header + 0x06);
    uint16_t opt_hdr_size = *(uint16_t*)(nt_header + 0x14);

    ps4_log("[HMBTN PS4] Preferred ImageBase: 0x%llX, Size: 0x%X, Entry: 0x%X\n",
           (unsigned long long)image_base, size_of_image, entry_point_rva);

    /* Allocate memory at preferred ImageBase 0x140000000 */
    uint8_t* mapped_base = mmap((void*)image_base, size_of_image,
                                PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    if (mapped_base == MAP_FAILED) {
        ps4_log("[HMBTN PS4] MAP_FIXED failed, allocating dynamically...\n");
        mapped_base = mmap(NULL, size_of_image,
                           PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }

    if (mapped_base == MAP_FAILED) {
        ps4_log("[HMBTN PS4 ERROR] mmap failed: %s\n", strerror(errno));
        return 1;
    }

    ps4_log("[HMBTN PS4] Mapped game image at: %p\n", mapped_base);

    /* Copy sections */
    uint8_t* sec_headers = nt_header + 0x18 + opt_hdr_size;
    for (int i = 0; i < num_sections; i++) {
        uint8_t* sec = sec_headers + i * 40;
        char name[9] = {0};
        memcpy(name, sec, 8);
        uint32_t vaddr = *(uint32_t*)(sec + 12);
        uint32_t vsize = *(uint32_t*)(sec + 8);
        uint32_t roff  = *(uint32_t*)(sec + 20);
        uint32_t rsize = *(uint32_t*)(sec + 16);

        if (rsize > 0 && roff < (size_t)exe_size) {
            size_t copy_len = ((roff + rsize) <= (size_t)exe_size) ? rsize : ((size_t)exe_size - roff);
            memcpy(mapped_base + vaddr, exe_data + roff, copy_len);
            ps4_log("[HMBTN PS4] Section %-8s: VA +0x%07X, Size 0x%07X (copied %zu bytes)\n", name, vaddr, vsize, copy_len);
        }
    }

    /* Apply base relocations if mapped base != preferred base */
    intptr_t delta = (intptr_t)mapped_base - (intptr_t)image_base;
    if (delta != 0) {
        ps4_log("[HMBTN PS4] Applying PE relocations with delta: %lld...\n", (long long)delta);
        uint32_t reloc_rva = *(uint32_t*)(nt_header + 0x88 + 8 * 5); // BaseRelocationTable (DataDirectory[5])
        uint32_t reloc_size = *(uint32_t*)(nt_header + 0x88 + 8 * 5 + 4);
        if (reloc_rva && reloc_size) {
            uint8_t* cur_reloc = mapped_base + reloc_rva;
            uint8_t* end_reloc = cur_reloc + reloc_size;
            while (cur_reloc < end_reloc) {
                uint32_t page_rva = *(uint32_t*)cur_reloc;
                uint32_t block_size = *(uint32_t*)(cur_reloc + 4);
                if (block_size == 0) break;
                int count = (block_size - 8) / 2;
                uint16_t* entries = (uint16_t*)(cur_reloc + 8);
                for (int j = 0; j < count; j++) {
                    int type = entries[j] >> 12;
                    int offset = entries[j] & 0xFFF;
                    if (type == 10) { // IMAGE_REL_BASED_DIR64
                        *(uint64_t*)(mapped_base + page_rva + offset) += delta;
                    }
                }
                cur_reloc += block_size;
            }
        }
    }

    /* Resolve Imports via IAT */
    uint32_t import_rva = *(uint32_t*)(nt_header + 0x88 + 8 * 1); // ImportTable (DataDirectory[1])
    ps4_log("[HMBTN PS4] Resolving imports from RVA 0x%X...\n", import_rva);
    if (import_rva) {
        uint8_t* desc = mapped_base + import_rva;
        while (*(uint32_t*)(desc + 12) != 0) { // Name RVA != 0
            const char* dll_name = (const char*)(mapped_base + *(uint32_t*)(desc + 12));
            ps4_log("[HMBTN PS4]  Binding DLL: %s\n", dll_name);
            uint32_t thunk_rva = *(uint32_t*)desc; // OriginalFirstThunk
            if (!thunk_rva) thunk_rva = *(uint32_t*)(desc + 16); // FirstThunk
            uint32_t iat_rva = *(uint32_t*)(desc + 16);

            uint64_t* thunk = (uint64_t*)(mapped_base + thunk_rva);
            uint64_t* iat = (uint64_t*)(mapped_base + iat_rva);

            while (*thunk) {
                if (!(*thunk & 0x8000000000000000ULL)) { // Not ordinal
                    uint8_t* by_name = mapped_base + (*thunk & 0x7FFFFFFFULL);
                    const char* func_name = (const char*)(by_name + 2);
                    void* resolved = resolve_import(dll_name, func_name);
                    if (resolved) {
                        *iat = (uint64_t)(uintptr_t)resolved;
                    }
                }
                thunk++;
                iat++;
            }
            desc += 20;
        }
    }

    ps4_log("[HMBTN PS4] Imports resolved successfully! Launching SDL_main...\n");

    /* Free temporary exe file copy */
    free(exe_data);

    /* ---------------------------------------------------------------
     * PRIORITY: Call CRT global C++ constructors from .CRT section.
     * Windows PE stores pointers to constructor functions in .CRT section
     * between symbols __xc_a (start) and __xc_z (end).
     * On PS4/Linux these are NOT called automatically, so we must call them
     * manually before SDL_main().
     * ---------------------------------------------------------------*/
    {
        /* Section .CRT is mapped at: mapped_base + CRT_VA */
        /* We detect it by walking section headers again */
        uint8_t* dos = mapped_base;
        uint32_t pe_off2 = *(uint32_t*)(dos + 0x3C);
        uint8_t* nt2 = dos + pe_off2;
        uint16_t num_sects2 = *(uint16_t*)(nt2 + 6);
        uint16_t opt_size2 = *(uint16_t*)(nt2 + 20);
        uint8_t* sect_hdr2 = nt2 + 24 + opt_size2;
        for (int si = 0; si < num_sects2; si++) {
            uint8_t* sh = sect_hdr2 + si * 40;
            char sname[9]; memcpy(sname, sh, 8); sname[8] = 0;
            if (strcmp(sname, ".CRT") == 0) {
                uint32_t va2  = *(uint32_t*)(sh + 12);
                uint32_t vsz2 = *(uint32_t*)(sh + 16);
                /* .CRT section contains an array of function pointers (8 bytes each on x64) */
                uint8_t* crt_start = mapped_base + va2;
                uint8_t* crt_end   = crt_start + vsz2;
                ps4_log("[HMBTN PS4] Processing .CRT constructors: VA=0x%X size=%u\n", va2, vsz2);
                typedef void (*crt_fn_t)(void);
                for (uint8_t* p = crt_start; p + 8 <= crt_end; p += 8) {
                    uint64_t fn_ptr = *(uint64_t*)p;
                    if (fn_ptr != 0 && fn_ptr != 0xFFFFFFFFFFFFFFFFULL) {
                        crt_fn_t ctor = (crt_fn_t)(uintptr_t)fn_ptr;
                        ps4_log("[HMBTN PS4]   Calling CRT ctor at 0x%016llx\n", (unsigned long long)fn_ptr);
                        if (g_log_fp) fflush(g_log_fp);
                        ctor();
                    }
                }
                ps4_log("[HMBTN PS4] .CRT constructors done.\n");
                break;
            }
        }
    }

#ifdef ENABLE_HD_TEXTURES
    hd_textures_init("/app0/hd_textures.pak", mapped_base);
#endif

    /* Apply the PS4 software-renderer profile once, preserving all mappings. */
    migrate_ps4_performance_settings();

    /* Respect the language selected in the cheat menu on the next launch. */
    const char* selected_language = load_language_preference();
    ps4_log("[HMBTN PS4] Language preference from cheat menu: %s\n", selected_language);

    /* Bypass PC welcome/overlay routines in memory by patching function entries with RET (0xC3) */
    mapped_base[0x4420] = 0xC3;
    mapped_base[0x20f0] = 0xC3;
    ps4_log("[HMBTN PS4] Patched welcome screen render routines (0x4420 & 0x20f0) with RET (0xC3)\n");

    /* The PE performs an unconditional SDL_Delay(16) immediately after each
     * present, while the PS4 video backend already waits for flip/vsync. Remove
     * only this exact call site so unrelated UI delays retain their behavior. */
    {
        static const uint8_t post_present_delay_call[5] = { 0xE8, 0x83, 0xFF, 0x2A, 0x00 };
        if (memcmp(mapped_base + 0x8660, post_present_delay_call,
                   sizeof(post_present_delay_call)) == 0) {
            memset(mapped_base + 0x8660, 0x90, 5);
            ps4_log("[HMBTN PS4 PERF] Removed redundant post-present SDL_Delay(16) at RVA 0x8660\n");
        } else {
            ps4_log("[HMBTN PS4 PERF] Delay patch skipped: signature mismatch at RVA 0x8660\n");
        }
    }

    /* Call SDL_main at offset 0xa050 */
    typedef __attribute__((ms_abi)) int (*SDL_Main_t)(int argc, char* argv[]);
    SDL_Main_t pSDL_main = (SDL_Main_t)(mapped_base + 0xa050);

    char* game_argv[] = {
        "/app0/HarvestMoonBTN.exe",
        "--lang",
        (char*)selected_language,
        HM_DISC_CUE,
        NULL
    };
    ps4_log("[HMBTN PS4] Calling pSDL_main(4, game_argv), lang=%s...\n", selected_language);
    if (g_log_fp) fflush(g_log_fp);
    int ret = pSDL_main(4, game_argv);

#ifdef ENABLE_HD_TEXTURES
    hd_textures_shutdown();
#endif

    ps4_log("[HMBTN PS4] Game terminated with code: %d\n", ret);
    return ret;
}
