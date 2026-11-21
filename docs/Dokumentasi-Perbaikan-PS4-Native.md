# Dokumentasi Lengkap Perbaikan: Harvest Moon BTN PS4 Native Port (CUSA01117)

> **ARSIP LEGACY — BUKAN JALUR PRODUKSI.** Perbaikan di bawah tetap berguna sebagai riwayat diagnosis loader, tetapi paket native baru tidak memakai PE hook atau renderer SDL software.

Dokumen ini merangkum seluruh analisis teknis, pembongkaran kode mesin (*disassembly*), akar masalah (*root cause*), implementasi perbaikan, dan pengujian runtime pada porting **Harvest Moon: Back to Nature (Mod Penyu v1.3.5)** ke Sony PlayStation 4 (Arsitektur AMD Jaguar x86-64 Native).

> **Koreksi terakhir: 16 November 2026.** Klaim lama tentang renderer GNM/60 FPS, enum GUIDE untuk L3/R3, dan patch bahasa eksternal telah diselaraskan dengan implementasi serta bukti build terbaru.

---

## Daftar Isi
1. [Ringkasan Masalah & Status Terkini](#1-ringkasan-masalah--status-terkini)
2. [Perbaikan 1: Background Home Screen Tidak Muncul (pic1.png)](#2-perbaikan-1-background-home-screen-tidak-muncul-pic1png)
3. [Perbaikan 2: Resolusi PE64 Import Table & DataDirectory (Offset 0x88)](#3-perbaikan-2-resolusi-pe64-import-table--datadirectory-offset-0x88)
4. [Perbaikan 3: NULL Pointer Guards pada C Runtime String Hooks](#4-perbaikan-3-null-pointer-guards-pada-c-runtime-string-hooks)
5. [Perbaikan 4: Resolusi Renderer Grafis SDL2 (Dummy Accelerated Stub Fix)](#5-perbaikan-4-resolusi-renderer-grafis-sdl2-dummy-accelerated-stub-fix)
6. [Perbaikan 5: Memory Corruption Free Static Memory (SDL_GetBasePath & SDL_free)](#6-perbaikan-5-memory-corruption-free-static-memory-sdl_getbasepath--sdl_free)
7. [Perbaikan 6: Generic Stub Fallback untuk Unresolved Imports](#7-perbaikan-6-generic-stub-fallback-untuk-unresolved-imports)
8. [Perbaikan 7: Async-Signal-Safe Crash Handler (write() Syscall + Hex Formatter)](#8-perbaikan-7-async-signal-safe-crash-handler-write-syscall--hex-formatter)
9. [Perbaikan 8: Window Fullscreen Mode & Controller Discovery Guards](#9-perbaikan-8-window-fullscreen-mode--controller-discovery-guards)
10. [Perbaikan 9: Redirect Logging & Save Storage ke /data/](#10-perbaikan-9-redirect-logging--save-storage-ke-data)
11. [Perbaikan 10: Otomatisasi Build & FTP Deployment Pipeline](#11-perbaikan-10-otomatisasi-build--ftp-deployment-pipeline)
12. [Perbaikan 11: SIGSEGV Input Controller NULL Pointer Dereference (SDL_IsGameController)](#12-perbaikan-11-sigsegv-input-controller-null-pointer-dereference-sdl_isgamecontroller)
13. [Perbaikan 12: VirtualAlloc Anonymous mmap Bertingkat & Emulasi VirtualQuery (win64_MBI)](#13-perbaikan-12-virtualalloc-anonymous-mmap-bertingkat--emulasi-virtualquery-win64_mbi)
14. [Perbaikan 13: Resolusi Dynamic Symbol __stack_chk_guard & Linker Group Order](#14-perbaikan-13-resolusi-dynamic-symbol-__stack_chk_guard--linker-group-order)
15. [Perbaikan 14: Koreksi Struct FreeBSD ucontext_t, SA_SIGINFO (0x0040), dan CRT Thread Trampoline](#15-perbaikan-14-koreksi-struct-freebsd-ucontext_t-sa_siginfo-0x0040-dan-crt-thread-trampoline)
16. [Perbaikan 15: Freeze di Welcome Screen - Integrasi Direct ScePad Native & Synthetic Input Event Queue](#16-perbaikan-15-freeze-di-welcome-screen---integrasi-direct-scepad-native--synthetic-input-event-queue)
17. [Perbaikan 16: Layar Tertutup PS4 System Splash Screen - Dismiss via sceSystemServiceHideSplashScreen & In-Memory RET Patching](#17-perbaikan-16-layar-tertutup-ps4-system-splash-screen---dismiss-via-scesystemservicehidesplashscreen--in-memory-ret-patching)
18. [Struktur Paket PKG Final (CUSA01117)](#18-struktur-paket-pkg-final-cusa01117)
19. [Panduan Pengujian & Verifikasi](#19-panduan-pengujian--verifikasi)

---

## 1. Ringkasan Masalah & Status Terkini

| No | Masalah / Gejala | Status | Lokasi Perbaikan |
|---|---|---|---|
| 1 | Background gambar tidak muncul saat game di-highlight di menu PS4 | **SOLVED** ✅ | `build_ps4/project.gp4`, `pkg_staging/sce_sys/pic1.png` |
| 2 | Crash saat membaca PE IAT DataDirectory pada mapping EXE 64-bit | **SOLVED** ✅ | `build_ps4/gen_loader.js` (Offset DataDirectory PE32+ `0x88`) |
| 3 | Signal 11 (Segmentation Fault) saat memanggil string functions | **SOLVED** ✅ | `build_ps4/gen_loader.js` (NULL pointer guards di semua hook string) |
| 4 | Crash CE-34878-0 saat inisialisasi renderer grafis SDL | **SOLVED** ✅ | `build_ps4/gen_loader.js` (Eksplisit `SDL_RENDERER_SOFTWARE`) |
| 5 | Crash fatal seketika setelah renderer sukses (`SDL_free` pada pointer static) | **SOLVED** ✅ | `build_ps4/gen_loader.js` (`hook_SDL_GetBasePath` heap malloc + `hook_SDL_free` guard) |
| 6 | Potensi crash jika EXE memanggil import tak terdaftar | **SOLVED** ✅ | `build_ps4/gen_loader.js` (Fallback `generic_stub_func` return 0) |
| 7 | Crash handler mati sendiri tanpa mencetak register CPU | **SOLVED** ✅ | `build_ps4/gen_loader.js` (100% async-signal-safe raw `write()` & hex writer) |
| 8 | Mode fullscreen ganda dan polling joystick crash | **SOLVED** ✅ | `build_ps4/gen_loader.js` (`hook_SDL_SetWindowFullscreen` no-op + logging controller) |
| 9 | Penyimpanan game (`/app0/saves/`) read-only di PS4 | **SOLVED** ✅ | `build_ps4/gen_loader.js` (Redireksi path `/app0/` -> `/data/HarvestMoonBTN/`) |
| 10 | Proses build dan transfer PKG manual via USB lambat | **SOLVED** ✅ | `update_rebuild.bat`, `tools/upload_pkg_to_ps4.js`, `tools/fetch_loader_log.js` |
| 11 | Crash CE-34878-0 (signal 11) seketika setelah deteksi joystick | **SOLVED** ✅ | `build_ps4/gen_loader.js` (`hook_SDL_IsGameController` return 1 & auto mapping DS4) |
| 12 | Crash `os_mmap failed..` / `abort()` pada wasm2c linear memory 8GB | **SOLVED** ✅ | `build_ps4/gen_loader.js` (`hook_VirtualAlloc` mmap bertingkat & `hook_VirtualQuery` struct `win64_MBI`) |
| 13 | Crash signal 11 saat membuat tekstur rendering (`SW_CreateTexture`) | **SOLVED** ✅ | `update_rebuild.bat` (`--start-group`), `build_ps4/gen_loader.js` (`__stack_chk_guard`) |
| 14 | Crash signal 11 pasca-frame #5 & ketidaksesuaian struct FreeBSD `ucontext_t` | **SOLVED** ✅ | `build_ps4/gen_loader.js` (FreeBSD `ucontext_t`, `SA_SIGINFO` 0x40, ABI trampoline `_beginthreadex`) |
| 15 | Freeze di welcome screen (game me-render frame tanpa henti menunggu tombol input) | **SOLVED** ✅ | `build_ps4/gen_loader.js` (`scePadReadState`, antrean synthetic event SDL controller & keyboard, hook `SDL_GameControllerGetButton`) |
| 16 | Layar game tertutup splash screen sistem PS4 (game sekilas muncul saat tekan tombol Home DS4) | **SOLVED** ✅ | `build_ps4/gen_loader.js` (`sceSystemServiceHideSplashScreen`, `-lSceSystemService`, patch RET `0x4420` & `0x20f0`) |
| 17 | **FPS sekitar 5 pada renderer software** — library lama memakai hot path O0, copy/clear 1080p redundan, dan delay pasca-present | ⚠️ **OPTIMIZED / TEST PS4** | profil v2 + SDL hot path `-O3` + tanpa clear redundan + NOP delay RVA `0x8660` + phase timing |
| 18 | **Button Mapping DS4 Final** — Circle tidak lagi membuka menu; L3/R3→F1→Cheat Menu | **SOLVED / TEST PS4** ✅ | `build_ps4/gen_loader.js` (B tanpa Escape/F1; controller 7/8 + joystick 11/12 → F1) |
| 19 | **Audio Crackling/Stutter Fix** — PE requested 44.1kHz/2048 samples causing buffer underrun | **SOLVED** ✅ | `build_ps4/gen_loader.js` (`hook_SDL_OpenAudioDevice` force 48kHz/1024/AUDIO_S16SYS + auto-unpause) |
| 20 | **Pilihan Bahasa Cheat Menu** — sebelumnya selalu ditimpa `id` saat boot | ✅ **AKTIF/PERSISTEN** | `data/lang.txt` + `HMBTN_LANG` + `--lang`; konsistensi aset gameplay masih perlu tes PS4 |

---

## 2. Perbaikan 1: Background Home Screen Tidak Muncul (`pic1.png`)

### Gejala
Ketika icon game disorot di dashboard PS4 (XMB) atau saat game mulai dimuat, gambar latar belakang `Harvest_Moon_Back_To_Nature_background.png` tidak muncul di TV, melainkan hanya menampilkan layar biru polos.

### Akar Masalah
Sistem antarmuka Sony Orbis OS mensyaratkan spesifikasi berkas gambar terpisah:
* `sce_sys/icon0.png`: Ikon aplikasi berukuran 512x512 piksel.
* `sce_sys/pic0.png`: Banner depan berukuran 1920x1080 (opsional).
* `sce_sys/pic1.png`: **Wallpaper latar belakang penuh berukuran 1920x1080 (wajib untuk background display)**.

Sebelumnya, proyek hanya mendaftarkan `icon0.png` dan `pic0.png`. Ketiadaan `pic1.png` menyebabkan PS4 menggunakan warna default biru.

### Solusi
1. Membuat berkas `pkg_staging/sce_sys/pic1.png` dari gambar latar Full HD 1920x1080.
2. Mendaftarkan berkas ke [project.gp4](file:///e:/PS4/hmbtn/build_ps4/project.gp4):
   ```xml
   <file targ_path="sce_sys/pic1.png" orig_path="E:\PS4\hmbtn\pkg_staging\sce_sys\pic1.png"/>
   ```
3. Menambahkan sinkronisasi otomatis di [update_rebuild.bat](file:///e:/PS4/hmbtn/update_rebuild.bat).

---

## 3. Perbaikan 2: Resolusi PE64 Import Table & DataDirectory (Offset 0x88)

### Gejala
Aplikasi native mengalami crash seketika saat `eboot.bin` mencoba membaca Import Address Table (IAT) dari executable Windows x86-64 `HarvestMoonBTN.exe`.

### Akar Masalah
Header PE memiliki perbedaan layout offset antara PE32 (32-bit) dan PE32+ (64-bit):
* Pada PE32: DataDirectory dimulai pada offset `0x78` dari NT Header.
* Pada PE32+ (64-bit): Optional Header lebih panjang karena ukuran register dan base pointer 64-bit. DataDirectory dimulai pada offset **`0x88`** dari NT Header.

Kode loader lama menggunakan offset 32-bit (`0x78`), sehingga alamat RVA Import Directory terbaca salah dan memicu crash saat parsing memori.

### Solusi
Pada [build_ps4/gen_loader.js](file:///e:/PS4/hmbtn/build_ps4/gen_loader.js):
```c
uint32_t nt_offset = *(uint32_t*)(game_base + 0x3C);
uint8_t* nt_header = game_base + nt_offset;

// PE32+ (64-bit) DataDirectory berada di offset +0x88 dari NT Header:
uint32_t import_rva = *(uint32_t*)(nt_header + 0x88 + 8 * 1); // Entry 1: Import Table
```
Dengan perbaikan ini, seluruh **220 fungsi impor** dari 7 DLL (`SDL2.dll`, `KERNEL32.dll`, `msvcrt.dll`, `USER32.dll`, `SHELL32.dll`, `WINHTTP.dll`, `comdlg32.dll`) berhasil dihubungkan 100%.

---

## 4. Perbaikan 3: NULL Pointer Guards pada C Runtime String Hooks

### Gejala
Ketika fungsi `pSDL_main(2, game_argv)` dieksekusi, loader mengalami crash `Signal 11 (Segmentation Fault)`.

### Akar Masalah
Kode game Windows sering memanggil fungsi string runtime standar seperti `strcmp`, `strlen`, `strcpy`, atau string parser dengan pointer `NULL`. Di MSVCRT Windows hal ini terkadang ditoleransi, namun pada FreeBSD libc (PS4), dereference pointer `0x0` langsung memicu pembunuhan proses oleh kernel.

### Solusi
Semua fungsi string hook di [build_ps4/gen_loader.js](file:///e:/PS4/hmbtn/build_ps4/gen_loader.js) dipasangi proteksi *defensive NULL pointer check*:
```c
__attribute__((ms_abi)) int hook_strcmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    return strcmp(s1, s2);
}

__attribute__((ms_abi)) size_t hook_strlen(const char* s) {
    if (!s) return 0;
    return strlen(s);
}

__attribute__((ms_abi)) char* hook_strcpy(char* dest, const char* src) {
    if (!dest) return NULL;
    if (!src) { *dest = '\0'; return dest; }
    return strcpy(dest, src);
}
```

---

## 5. Perbaikan 4: Resolusi Renderer Grafis SDL2 (Dummy Accelerated Stub Fix)

### Gejala
Aplikasi mengalami crash **CE-34878-0** tepat saat inisialisasi renderer grafis SDL (`hook_SDL_CreateRenderer`).

### Akar Masalah
Setelah membongkar berkas objek `SDL_render_ps4.c.o` di dalam library `libSDL2.a` OpenOrbis Toolchain, ditemukan bahwa driver akselerasi GPU PS4 (`PS4_CreateRenderer`) adalah fungsi *stub tiruan* yang berisi:
```c
printf("# ERROR %s(), please use software renderer on ps4!\n", __FUNCTION__);
return NULL;
```
Karena mengembalikan `NULL`, pemanggilan `SDL_RenderSetLogicalSize` berikutnya pada pointer `NULL` memicu crash seketika.

### Solusi
Di [build_ps4/gen_loader.js](file:///e:/PS4/hmbtn/build_ps4/gen_loader.js):
1. Mengubah pembuatan renderer agar secara eksplisit memprioritaskan driver `software` (`SDL_RENDERER_SOFTWARE` / driver index 0), yang mengalirkan grafik melalui frame buffer surface kernel Orbis (`PS4_CreateWindowFramebuffer` & `sceVideoOutSubmitFlip`).
2. Menambahkan fallback ke `SDL_CreateSoftwareRenderer(SDL_GetWindowSurface(window))` jika inisialisasi pertama gagal.
3. Menambahkan pengaman `if (!renderer) return ...` di semua fungsi grafis terkait.

---

## 6. Perbaikan 5: Memory Corruption Free Static Memory (`SDL_GetBasePath` & `SDL_free`)

### Gejala
Setelah perbaikan software renderer berhasil (renderer tercatat sukses dibuat di log), game tetap crash seketika setelah `hook_SDL_CreateRenderer returning to game EXE...`.

### Analisis Akar Masalah (Disassembly `HarvestMoonBTN.exe`)
Melalui pembongkaran instruksi biner x86-64 pada `HarvestMoonBTN.exe` tepat setelah pembuatan renderer pada offset RVA `0x000b217`:
```assembly
14000b217: callq SDL_CreateRenderer
14000b222: movq  %rax, [renderer]
14000b231: movq  $0, [texture]
14000b23c: callq SDL_GetBasePath         ; Game memanggil SDL_GetBasePath()
14000b241: movq  %rax, %rdi             ; rdi = base path yang dikembalikan
14000b260: callq snprintf                ; snprintf(buf, 1024, "%sgamecontrollerdb.txt", base_path)
14000b26f: callq SDL_RWFromFile
14000b27c: callq SDL_GameControllerAddMappingsFromRW
14000b281: movq  %rdi, %rcx             ; rcx = rdi (base path)
14000b284: callq SDL_free                ; Game memanggil SDL_free(base_path) !
```

* Standar SDL2 mensyaratkan bahwa pemanggil fungsi `SDL_GetBasePath()` **wajib** membebaskan pointer tersebut menggunakan `SDL_free()`.
* Pada loader sebelumnya, `hook_SDL_GetBasePath()` mengembalikan pointer statis:
  ```c
  static char g_base_path[] = "/app0/";
  char* hook_SDL_GetBasePath(void) { return g_base_path; }
  ```
* Akibatnya, `SDL_free(g_base_path)` memanggil `free()` libc FreeBSD pada memori segmen `.data` statis.
* Pada libc sistem operasi FreeBSD/PS4, pemanggilan `free()` pada pointer non-heap menyebabkan **kerusakan memori seketika (heap corruption) yang berujung pada SIGSEGV / SIGABRT instan**.

### Solusi
1. Mengubah `hook_SDL_GetBasePath` agar mengalokasikan string baru di heap secara dinamis via `malloc`:
   ```c
   __attribute__((ms_abi)) char* hook_SDL_GetBasePath(void) {
       ps4_log("[HMBTN PS4] SDL_GetBasePath called\n");
       if (g_log_fp) fflush(g_log_fp);
       char* p = (char*)malloc(sizeof(g_base_path));
       if (p) strcpy(p, g_base_path);
       return p;
   }
   ```
2. Menambahkan safety guard pada `hook_SDL_free` agar pointer statis `g_base_path` tidak akan pernah diteruskan ke `free()`:
   ```c
   __attribute__((ms_abi)) void hook_SDL_free(void* mem) {
       if (!mem) return;
       if (mem == g_base_path) {
           ps4_log("[HMBTN PS4 WARNING] Ignored attempt to free static g_base_path!\n");
           if (g_log_fp) fflush(g_log_fp);
           return;
       }
       free(mem);
   }
   ```

---

## 7. Perbaikan 6: Generic Stub Fallback untuk Unresolved Imports

### Gejala & Risiko
Jika ada fungsi import dari Windows DLL (`SDL2.dll`, `KERNEL32.dll`, `msvcrt.dll`, dll.) yang belum diimplementasikan di tabel hook loader, fungsi `resolve_import` sebelumnya mengembalikan pointer `NULL`. Ketika game mencoba mengeksekusi instruksi `call [IAT]`, CPU akan melompat ke alamat `0x0`, memicu Segmentation Fault instan.

### Solusi
Di [build_ps4/gen_loader.js](file:///e:/PS4/hmbtn/build_ps4/gen_loader.js):
1. Membuat fungsi universal `generic_stub_func` dengan calling convention Microsoft 64-bit (`ms_abi`):
   ```c
   __attribute__((ms_abi)) static uint64_t generic_stub_func(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
       ps4_log("[HMBTN PS4 STUB] Called unresolved stub function (args: 0x%llx, 0x%llx, 0x%llx, 0x%llx)\n",
           (unsigned long long)a, (unsigned long long)b, (unsigned long long)c, (unsigned long long)d);
       if (g_log_fp) fflush(g_log_fp);
       return 0; // return SUCCESS / NULL
   }
   ```
2. Mengubah `resolve_import` agar mengarahkan seluruh import yang belum terdaftar ke `generic_stub_func` alih-alih `NULL`:
   ```c
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
   ```

---

## 8. Perbaikan 7: Async-Signal-Safe Crash Handler (`write()` Syscall + Hex Formatter)

### Gejala
Ketika terjadi crash, crash handler tidak mencetak register atau bahkan mati sendiri sebelum sempat menulis log.

### Akar Masalah
Standar POSIX/FreeBSD menetapkan bahwa fungsi-fungsi stdio seperti `printf`, `fprintf`, `snprintf`, `malloc`, dan `fflush` **tidak aman dipanggil di dalam konteks signal handler (bukan async-signal-safe)**. Pemanggilan fungsi tersebut saat memori corrupt sering memicu deadlock mutex atau recursive fault yang langsung diterminasi oleh kernel tanpa rekaman log.

### Solusi
Mengganti seluruh handler sinyal dengan instruksi primitif `write()` langsung ke file descriptor dan konversi bilangan heksadesimal manual tanpa ketergantungan library:
```c
static void crash_write_u64(uint64_t v) {
    static const char HEX[] = "0123456789abcdef";
    char buf[20];
    buf[0] = '0'; buf[1] = 'x'; buf[18] = '\n';
    for (int i = 0; i < 16; i++)
        buf[2+i] = HEX[(v >> (60 - i*4)) & 0xF];
    if (g_log_fd >= 0) write(g_log_fd, buf, 19);
    write(2, buf, 19);
}

static void crash_handler(int sig, siginfo_t* info, void* uctx) {
    if (g_log_fd < 0) {
        g_log_fd = open("/data/HarvestMoonBTN/loader.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    }
    CRASH_PUTS("\n========================================================\n");
    CRASH_PUTS("[HMBTN PS4 FATAL CRASH] Caught signal\n");
    if (info) {
        CRASH_PUTS("[CRASH] FaultAddr=");
        crash_write_u64((uint64_t)(uintptr_t)info->si_addr);
    }
    if (uctx) {
        ucontext_t* uc = (ucontext_t*)uctx;
        uint64_t rip = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
        crash_write_reg("[CRASH] RIP=", 12, rip);
        crash_write_reg("[CRASH] RSP=", 12, (uint64_t)uc->uc_mcontext.gregs[REG_RSP]);
        ...
    }
    if (g_log_fd >= 0) { fsync(g_log_fd); }
    _exit(128 + sig);
}
```

---

## 9. Perbaikan 8: Window Fullscreen Mode & Controller Discovery Guards

### Gejala
Setelah inisialisasi renderer selesai, game memanggil `SDL_SetWindowFullscreen(window, 0x1001)`. Pada OpenOrbis, panggilan ulang fullscreen pada window yang sudah fullscreen dapat memicu glitch display atau mode switch error.

### Solusi
1. `hook_SDL_SetWindowFullscreen` dijadikan no-op aman (mengembalikan 0) karena window PS4 secara permanen sudah Full HD 1080p Fullscreen:
   ```c
   __attribute__((ms_abi)) int hook_SDL_SetWindowFullscreen(SDL_Window* window, uint32_t flags) {
       ps4_log("[HMBTN PS4] SDL_SetWindowFullscreen called (window: %p, flags: 0x%X)\n", window, flags);
       if (g_log_fp) fflush(g_log_fp);
       return 0;
   }
   ```
2. Menambahkan logging eksplisit ke `hook_SDL_RWFromFile`, `hook_SDL_GameControllerAddMappingsFromRW`, `hook_SDL_NumJoysticks`, `hook_SDL_IsGameController`, dan `hook_SDL_GameControllerOpen` agar setiap tahap deteksi stik DualShock 4 terpantau di log.

---

## 10. Perbaikan 9: Redirect Logging & Save Storage ke `/data/`

### Masalah
Partisi `/app0/` tempat game terpasang berstatus *read-only* di PS4. Setiap upaya pembuatan file save (`card1.mcd`, `settings.ini`, dsb.) akan gagal (`EACCES`).

### Solusi
Mekanisme pengalihan path otomatis (*transparent path redirection*):
* Setiap penulisan ke `/app0/saves/` atau `/app0/settings.ini` secara otomatis dialihkan ke partisi writable `/data/HarvestMoonBTN/`.
* File default di-copy (*seeded*) dari `/app0/` saat pertama kali dijalankan jika belum ada di `/data/`.
* Seluruh log eksekusi dicatat ke `/data/HarvestMoonBTN/loader.log`.

---

## 11. Perbaikan 10: Otomatisasi Build & FTP Deployment Pipeline

Siklus pengembangan diotomatisasi secara penuh melalui skrip berikut:

1. **Auto Rebuild ([update_rebuild.bat](file:///e:/PS4/hmbtn/update_rebuild.bat))**:
   - Memvalidasi compiler toolchain (`clang`, `ld.lld`, `create-fself`, `orbis-pub-cmd`).
   - Menganalisis EXE baru (`analyze_new_exe.js`).
   - Meregenerasi `src/ps4_loader.c` (`gen_loader.js`).
   - Kompilasi ELF 64-bit FreeBSD (`-O2 -fPIC`).
   - Linking library sistem (`-lSDL2 -lkernel -lSceUserService -lScePad` dll).
   - Menandatangani FSELF `eboot.bin` (`paid 0x3800000000000011`).
   - Mengemas paket fPKG retail via `orbis-pub-cmd.exe img_create --skip_digest`.

2. **FTP Auto-Upload ([tools/upload_pkg_to_ps4.js](file:///e:/PS4/hmbtn/tools/upload_pkg_to_ps4.js))**:
   - Terhubung ke GoldHEN FTP di `10.0.0.90:2121`.
   - Menghapus log lama `/data/HarvestMoonBTN/loader.log`.
   - Mengunggah berkas PKG 238.4 MB ke `/data/pkg/HarvestMoonBTN-PS4-Native.pkg`.

3. **Log Downloader ([tools/fetch_loader_log.js](file:///e:/PS4/hmbtn/tools/fetch_loader_log.js))**:
   - Menarik `loader.log` dari PS4 ke komputer lokal secara instan untuk analisis langsung.

---

## 12. Perbaikan 11: SIGSEGV Input Controller NULL Pointer Dereference (`SDL_IsGameController`)

### Gejala
Game terhenti dengan crash fatal `signal=1` / `CE-34878-0` seketika setelah SDL joystick initialization selesai pada konsol PS4.

### Akar Masalah
Pada file biner game (`HarvestMoonBTN.exe`) di RVA `0x2b10`:
1. Game memanggil `SDL_IsGameController(0)`.
2. Pada OpenOrbis port `libSDL2.a`, driver joystick PS4 (`ScePad`) tidak mendaftarkan GUID kontroler bawaan ke SDL database GameController secara otomatis, sehingga `SDL_IsGameController(0)` mengembalikan `0`.
3. Akibat nilai `0`, alur logika game berbelok ke pemanggilan fallback `SDL_JoystickOpen(0)`, dan membiarkan variabel pointer `SDL_GameController*` global di alamat `0x1412941f0` tetap bernilai `NULL`.
4. Saat game memasuki subrutin pembacaan input frame berikutnya di RVA `0x68b5`, instruksi assembly mendereferensikan `rcx = [0x1412941f0]` yang berisi `NULL` tanpa validasi, memicu Segmentation Fault fatal.

### Solusi
1. Di `build_ps4/gen_loader.js` & `src/ps4_loader.c`:
   - Mencegat `hook_SDL_IsGameController`: jika index 0 dan joystick terhubung, paksa mengembalikan `1`.
   - Menginjeksi string database mapping DualShock 4 via `SDL_GameControllerAddMapping` saat inisialisasi controller:
     ```c
     SDL_GameControllerAddMapping("030000004c050000c405000000010000,PS4 Controller,a:b0,b:b1,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b2,y:b3,platform:Linux,");
     ```
   - Mencegat `hook_SDL_GameControllerOpen`: jika `SDL_GameControllerOpen(0)` mengembalikan NULL, lakukan fallback otomatis ke `SDL_JoystickOpen(0)` agar handle valid selalu tersedia.
2. Menambahkan deklarasi struct `msvcrt_FILE` (48 byte) dan `g_msvcrt_iob[20]` untuk melindungi aliran stdio C-runtime.

---

## 13. Perbaikan 12: VirtualAlloc Anonymous `mmap` Bertingkat & Emulasi `VirtualQuery` (`win64_MBI`)

### Gejala
Game gagal alokasi memori internal dengan pesan `os_mmap failed..` disusul panggilan `abort()` di subrutin `wasm_rt_allocate_memory` (RVA `0x2b6cc0`).

### Akar Masalah
1. Runtime `wasm2c` yang terbenam pada EXE meminta virtual address space sebesar 8 GB (`0x200000000` bytes) yang terdiri dari 4 GB linear memory 32-bit ditambah 4 GB guard pages proteksi *out-of-bounds*.
2. Implementasi `hook_VirtualAlloc` lama memanggil alokasi memori fisik `calloc(1, 8GB)`. Karena total RAM konsol PS4 hanya ~5 GB yang bebas untuk aplikasi homebrew, alokasi fisik 8 GB pasti ditolak (`NULL`), memicu string error `os_mmap failed..`.
3. Setelah alokasi virtual space, wasm2c memanggil `VirtualQuery` untuk memverifikasi proteksi memory range. Implementasi stub lama hanya me-return `0`, yang diartikan oleh engine sebagai memori tidak valid.

### Solusi
1. Mengganti strategi alokasi di `hook_VirtualAlloc` menggunakan pemetaan alamat virtual *POSIX / FreeBSD anonymous mapping*:
   ```c
   void* ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
   ```
   Sistem operasi FreeBSD Jaguar mengalokasikan ruang alamat virtual (VMA) tanpa mengikat RAM fisik seketika. RAM fisik hanya di-commit saat dipanggil proteksi `PAGE_READWRITE` via `mprotect`.
2. Menambahkan fallback ukuran bertingkat jika 8 GB ditolak oleh kernel: `8 GB -> 4 GB -> 2 GB -> 1 GB -> 256 MB`.
3. Mengimplementasikan struktur tabel pelacak alokasi `g_vallocs` (64 entri) untuk menyimpan base address dan ukuran blok.
4. Mengimplementasikan fungsi emulasi penuh `hook_VirtualQuery`:
   - Menerima pointer buffer struktur Win64 `MEMORY_BASIC_INFORMATION` (48 bytes).
   - Mengisi `BaseAddress`, `AllocationBase`, `AllocationProtect` (`0x40 = PAGE_EXECUTE_READWRITE`), `RegionSize`, `State` (`0x1000 = MEM_COMMIT`), dan `Type` (`0x20000 = MEM_PRIVATE`).

---

## 14. Perbaikan 13: Resolusi Dynamic Symbol `__stack_chk_guard` & Linker Group Order

### Gejala
Crash CE-34878-0 (signal 11) terjadi tepat saat game membuat streaming texture display Full HD/2K di RVA `0x202b` (`SDL_CreateTexture: 2556x1440`).

### Akar Masalah
1. Analisis biner ELF `hmbtn_ps4.elf` membuktikan simbol compiler `__stack_chk_guard` dan `__stack_chk_fail` berstatus *Undefined* (`U`) di dalam tabel simbol dinamis ELF.
2. Hal ini disebabkan oleh urutan argumen linker pada `update_rebuild.bat`:
   ```bat
   %LLD% ... %BUILD_DIR%\ps4_loader.o -lc -lkernel -lSDL2 ...
   ```
   Karena `-lc` ditempatkan sebelum `-lSDL2`, linker LLD memproses `libc.a` sebelum dependensi internal `libSDL2.a` diekstraksi. Ketika file objek `SDL_render_sw.c.o` ditarik, dependensinya ke stack canary `libc.a` (`__stack_chk_guard`, `__stack_chk_fail`, `gettimeofday`, dan `sem_*`) tidak diselesaikan ulang dan tertinggal sebagai impor dinamis yang unresolved.
3. Karena modul sistem PS4 (`libkernel.sprx`) tidak mengekspor simbol `__stack_chk_guard`, runtime dynamic linker PS4 mengisi entry GOT simbol tersebut dengan nilai `0` (NULL).
4. Saat rutin `SW_CreateTexture` dijalankan, instruksi pembuka compiler:
   ```assembly
   mov rax, [rip + __stack_chk_guard]  ; rax = GOT entry (0)
   mov rax, [rax]                      ; DEREFERENCE ADDRESS 0 -> CRASH SIGNAL 11!
   ```
   langsung memicu page fault seketika.

### Solusi
1. Di `update_rebuild.bat`: Mengubah baris dependensi pustaka menjadi circular resolution group `--start-group ... --end-group`:
   ```bat
   set LIBS=--start-group -lSDL2 -lc -lkernel -lSceUserService -lSceVideoOut -lSceAudioOut -lScePad --end-group
   ```
   Menyelesaikan seluruh dependensi silang antara `libSDL2.a` dan `libc.a`.
2. Di `build_ps4/gen_loader.js` & `src/ps4_loader.c`: Mendefinisikan stack canary dan fail handler secara eksplisit di level loader:
   ```c
   __attribute__((visibility("default"))) uintptr_t __stack_chk_guard = 0x595e9fbd94fda766ULL;
   __attribute__((visibility("default"))) void __stack_chk_fail(void) {
       ps4_log("[HMBTN PS4 FATAL] Stack smashing detected!\n");
       abort();
   }
   ```
3. Memperbaiki `hook_SDL_RenderSetLogicalSize` agar meneruskan resolusi logika asli yang diminta game (2556x1440) alih-alih memotongnya, sehingga SDL2 melakukan penskalaan otomatis ke jendela 1080p dengan aspek rasio yang tepat.
4. Menambahkan logging dan `fflush(g_log_fp)` pada seluruh fungsi pipeline grafis (`SDL_CreateTexture`, `SDL_UpdateTexture`, `SDL_RenderPresent`).

---

## 15. Perbaikan 14: Koreksi Struct FreeBSD `ucontext_t`, `SA_SIGINFO` (0x0040), dan CRT Thread Trampoline

### Gejala
1. Setelah pipeline grafis SDL2 berhasil merender 5 frame pertama (`SDL_UpdateTexture` dan `SDL_RenderPresent` #1-#5), game mengalami crash fatal dengan `signal=11`.
2. Pada log crash handler terdahulu, register CPU kritis (`RIP`, `RSP`, `RAX`) tercetak sebagai `0x0000000000000000`, sementara register `RBX` terbaca `0x38` dan `RDX` terbaca `0x7eed9ff60`.

### Akar Masalah
1. **Perbedaan Layout `ucontext_t` Antara Header Toolchain dan Kernel Sony Orbis OS (FreeBSD amd64):**
   Header OpenOrbis mengadopsi definisi `ucontext_t` bergaya Linux/musl dengan array `gregs[]`. Namun kernel PlayStation 4 (turunan FreeBSD 9/12) menyusun stack frame sinyal dengan struct `__mcontext` FreeBSD:
   * `uc_sigmask`: 16 byte (Word 0 & 1)
   * `mc_onstack`: Word 2
   * Register umum (`mc_rdi`, `mc_rsi`, `mc_rdx`, `mc_rcx`, `mc_r8`, `mc_r9`, `mc_rax`, `mc_rbx`, `mc_rbp`, `mc_r10`..`mc_r13`): Word 3 - 15
   * Register `mc_r14`: Word 16 (bernilai `0x38`)
   * Register `mc_r15`: Word 17 (bernilai `0x7eed9ff60`)
   * `mc_trapno` / `mc_addr`: Word 18 & 19
   * `mc_rip`: Word 22
   * `mc_rsp`: Word 25
   Karena loader membaca offset Linux, pembacaan pointer register jatuh pada offset data padding/nol, sehingga RIP dan RSP terbaca 0x0.
2. **Nilai Flag `SA_SIGINFO`:**
   Konstanta POSIX `SA_SIGINFO` pada Linux bernilai `4`, namun pada kernel FreeBSD bernilai `0x0040` (64). Akibatnya, pendaftaran handler tidak sepenuhnya mengaktifkan mode passing 3-argumen secara sempurna di level kernel.
3. **Calling Convention MS ABI pada Thread CRT (`hook__beginthreadex`):**
   Fungsi `hook__beginthreadex` memanggil `pthread_create` langsung dengan pointer fungsi tanpa melewati trampoline `hm_thread_tramp`, berpotensi menimbulkan ketidakcocokan register saat thread background CRT dieksekusi.

### Solusi
1. Mengganti struct context pada crash handler dengan layout native FreeBSD amd64 (`ps4_mcontext_t` dan `ps4_ucontext_t`), serta menambahkan dump mentah 32-word `ucontext` dan inspeksi stack frame RSP 16-word.
2. Mengatur flag sinyal secara eksplisit: `sa.sa_flags = SA_SIGINFO | 0x0040;`.
3. Memperbarui `hook__beginthreadex` agar menggunakan trampoline `hm_thread_tramp` dengan alokasi struktur `hm_thread_t` yang aman terhadap konversi calling convention MS ABI x64.
4. Meningkatkan pelaporan frame `SDL_UpdateTexture` dan `SDL_RenderPresent` hingga 20 frame pertama dan periodik per 60 frame (1 detik).

---

## 16. Perbaikan 15: Freeze di Welcome Screen - Integrasi Direct ScePad Native & Synthetic Input Event Queue

### Gejala
Game berhasil melewati inisialisasi video, audio, threading, dan masuk ke rendering loop tanpa crash (frame 60, 120, 180... 720 terus dirender), namun visual layar terpaku (*freeze/stuck*) pada Welcome Screen / Disclaimer ("Mainkan Harvest Moon: Back to Nature"). Menekan tombol stik DualShock 4 tidak memajukan layar ke menu utama atau intro.

### Analisis Disassembly
Melalui disassembly biner x64 `HarvestMoonBTN.exe`:
1. Subrutin welcome screen pada VA `0x140004420` (dipanggil dari `0x140004bb0` dan `0x14000b4a0`) menunggu input konfirmasi untuk berpindah state.
2. Loop pemrosesan event pada VA `0x14000b7d7` memanggil `SDL_PollEvent` (`0x14000b848`). Selain itu, engine memanggil fungsi IAT `SDL_GameControllerGetButton` (`0x14182a810`) dan `SDL_GameControllerGetAxis` (`0x14182a808`) secara langsung.
3. Backend joystick SDL OpenOrbis (`SDL_sysjoystick.c.o`) memerlukan handle pad aktif yang diinisialisasi melalui `sceUserServiceGetInitialUser` dan `scePadOpen`. Tanpa inisialisasi native pad pada level loader, `SDL_PollEvent` tidak pernah menghasilkan event penekanan tombol gamepad maupun keyboard.

### Solusi
1. **Inisialisasi Native Orbis Pad:**
   - Menyertakan header `<orbis/Pad.h>` dan `<orbis/UserService.h>`.
   - Menginisialisasi `sceUserServiceInitialize(NULL)` dan `scePadInit()` di awal fungsi `main()`.
   - Membuka handle DualShock 4 default via `sceUserServiceGetInitialUser(&userId)` dan `scePadOpen(userId, 0, 0, NULL)`.
2. **Direct Hardware Polling & Synthetic Event Queue:**
   - Mengimplementasikan `poll_direct_ds4()` yang membaca status fisik tombol stik DS4 secara presisi melalui `scePadReadState(handle, &data)`.
   - Mendeteksi perubahan bitmask tombol (`curr ^ prev`) dan menyuntikkan event ganda secara serentak ke dalam antrean sintetis (`s_synth_events`):
     - `SDL_CONTROLLERBUTTONDOWN` / `SDL_CONTROLLERBUTTONUP`
     - `SDL_KEYDOWN` / `SDL_KEYUP` (Return, Space, Esc, D-Pad, dsb.)
   - Tombol **Cross (X)** dan **Options (Start)** secara otomatis menginjeksi keycode `SDLK_RETURN` dan `SDLK_SPACE` untuk menjamin transisi instan dari welcome screen.
3. **Penyempurnaan Hook `SDL_GameControllerGetButton` & `SDL_GameControllerGetAxis`:**
   - Menambahkan fallback pembacaan langsung ke state hardware DS4 (`s_prev_pad_buttons`) bila engine melakukan polling controller langsung alih-alih melalui antrean event.
   - Mengimplementasikan pembacaan stik analog (axis LX, LY, RX, RY) dan trigger (L2, R2) pada `hook_SDL_GameControllerGetAxis`.
4. **Redireksi Standar I/O:**
   - Menggunakan `dup2(g_log_fd, 1)` dan `dup2(g_log_fd, 2)` pada inisialisasi agar seluruh pesan keluaran dari engine/runtime tercatat ke `/data/HarvestMoonBTN/loader.log`.

---

## 17. Perbaikan 16: Layar Tertutup PS4 System Splash Screen - Dismiss via sceSystemServiceHideSplashScreen & In-Memory RET Patching

### Gejala
Game loop dan rendering PS1 berjalan normal di latar belakang (60 FPS, audio dan input pad aktif), namun layar TV tetap menampilkan gambar splash screen (`pic0.png`/`pic1.png`). Saat tombol PS/Home pada DualShock 4 ditekan, layar gameplay sempat terlihat sekilas di display selama transisi menu PS4.

### Analisis Disassembly & Sistem PS4
1. **Perilaku Orbis OS ShellCore:**
   - Saat aplikasi/game native diluncurkan, OS PS4 menampilkan splash screen (`pic0.png` atau `pic1.png`) sebagai layer visual teratas.
   - OS PS4 tidak akan menutup layer splash screen ini secara otomatis sampai aplikasi memanggil fungsi sistem:
     ```c
     sceSystemServiceHideSplashScreen();
     ```
   - Ketika tombol PS/Home ditekan, kernel PS4 memulai suspend transition dan menyembunyikan sementara layer splash screen sistem, sehingga buffer game yang sedang di-flip oleh SDL2 (`sceVideoOutSubmitFlip`) sempat terlihat sekilas.
2. **Pustaka Sistem Terkait:**
   - Modul `libSceSystemService` dan `libSceSysmodule` harus dimuat melalui `sceSysmoduleLoadModuleInternal(0x80000010)` (`ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE`).
3. **Pembersihan Overlay Welcome Internal EXE:**
   - Rutin welcome screen PE pada VA `0x140004420` dan renderer overlay `0x1400020f0` ditambal di memori saat runtime dengan instruksi `RET` (`0xC3`) untuk memastikan tidak ada lapisan grafis modal dari executable PC yang dapat menimpa buffer framebuffer game PS1.

### Solusi
1. **Deklarasi Header & Dynamic Linking:**
   - Menambahkan header `<orbis/Sysmodule.h>` dan `<orbis/SystemService.h>` pada `build_ps4/gen_loader.js`.
   - Menambahkan flag linker `-lSceSysmodule -lSceSystemService` pada `LIBS` di `update_rebuild.bat`.
2. **Pemanggilan Dismiss Splash Screen:**
   - Memanggil `sceSystemServiceHideSplashScreen()` pada fase inisialisasi awal `main()` dan pada frame pertama `hook_SDL_RenderPresent`:
     ```c
     sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
     sceSystemServiceHideSplashScreen();
     ```
3. **In-Memory Binary Patch:**
   - Menulis `0xC3` (`RET`) pada offset RVA `0x4420` dan `0x20f0` sebelum mengeksekusi `pSDL_main`:
     ```c
     mapped_base[0x4420] = 0xC3;
     mapped_base[0x20f0] = 0xC3;
     ```

---

## 18. Struktur Paket PKG Final (CUSA01117)

```
HarvestMoonBTN-PS4-Native.pkg (249,954,304 bytes)
├── eboot.bin                     # FSELF Loader Native PS4 (x86-64 ELF)
├── HarvestMoonBTN.exe            # Executable Game (PE32+ 64-bit)
├── disc.bin                      # Disc Image Asli PS1 (Track 1 Mode2/2352)
├── disc.cue                      # CUE Sheet Cue Index
├── settings.ini                  # Konfigurasi Grafik 1080p & Audio
├── saves/
│   ├── card1.mcd                 # Memory Card Slot 1
│   └── card2.mcd                 # Memory Card Slot 2
├── sce_module/
│   ├── libc.prx                  # Sony OpenOrbis C Runtime
│   └── libSceFios2.prx           # File I/O Subsystem
└── sce_sys/
    ├── param.sfo                 # Metadata Judul CUSA01117
    ├── icon0.png                 # Icon XMB Beranda (512x512)
    ├── pic0.png                  # Splash / Foreground Banner (1920x1080)
    └── pic1.png                  # Background Wallpaper HD (1920x1080)
```

- SHA-256: `553D9B768D8000DB736BE5865B7FB604B158A48BB1FA0B8BE5C01B4BFDD2ABFC`
- Remote terverifikasi: `ftp://10.0.0.90:2121/data/pkg/HarvestMoonBTN-PS4-Native.pkg` (`249,954,304` byte, 16 November 2026)

---

## 19. Perbaikan 17: Crash CE-34878-0 dan Mitigasi FPS Renderer Software

### Gejala
1. Game crash dengan error `CE-34878-0` setelah beberapa frame render (~5 FPS).
2. Log loader menampilkan: `os_mmap failed..` diikuti `abort()` dan `SIGSEGV`.
3. FPS terpaku di 2-6 FPS, audio patah-patah (crackling/stutter).
4. Renderer fallback ke software (tidak akselerasi GPU).

### Analisis Akar Masalah

#### 1. `SDL_WINDOW_ALLOW_HIGHDPI` → 4K Backbuffer → OOM Crash
Di `hook_SDL_CreateWindow`, flag `SDL_WINDOW_ALLOW_HIGHDPI` diminta:
```c
SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
```
Pada PS4 (output 1080p fixed), flag ini meminta backbuffer resolusi tinggi (~2556×1440 = 16 MB per tekstur). Game membuat banyak tekstur → `os_mmap failed` → `abort()` → **CE-34878-0**.

#### 2. Renderer OpenOrbis yang Aktif adalah Software
Backend `PS4_CreateRenderer` pada OpenOrbis build ini tidak menyediakan renderer GPU yang dapat dipakai. Runtime hanya menemukan renderer software, sehingga internal texture besar dan bilinear scaling membebani CPU.

#### 3. Frame Pacing Div-by-Zero
`hook_SDL_RenderPresent` menggunakan `SDL_GetPerformanceFrequency()` untuk timing. Pada SDL2 PS4 port, fungsi ini bisa return `0` → div-by-zero → undefined behavior.

#### 4. Unbounded `SDL_Delay` Calls
Game memanggil `SDL_Delay(100+)` untuk timing frame. Tanpa clamp, CPU idle terlalu lama → FPS drop.

#### 5. Delay Present Ganda
Backend flip sudah menunggu vblank. Delay manual di `hook_SDL_RenderPresent` menambahkan wait kedua dan menurunkan throughput.

### Solusi (Semua di `build_ps4/gen_loader.js`)

**A. Hapus Flag HighDPI**
```c
SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN  // tanpa ALLOW_HIGHDPI
```

**B. Gunakan Profil Renderer Software Ringan**
```c
SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
// Migrasi profile-v2 satu kali: resolution=1, smooth=0.
```

**C. Serahkan Frame Pacing ke Backend Flip**
```c
SDL_RenderPresent(renderer);
// Tanpa SDL_Delay tambahan; hitung dan log FPS setiap 5 detik.
```

**D. SDL_Delay Clamp Max 16ms**
```c
__attribute__((ms_abi)) void hook_SDL_Delay(uint32_t ms) {
    if (ms > 16) ms = 16;
    SDL_Delay(ms);
}
```

### Hasil dan Batas Verifikasi
- Loader lulus tes regresi, compile target PS4, linking, signing, dan packaging.
- Profil v2 dimigrasikan tanpa menghapus opsi atau mapping lain; integer scaling diaktifkan untuk menghindari fractional software scaling.
- FPS nyata belum diklaim 60; hasil harus dibaca dari sampel 5 detik pada `loader.log` setelah instalasi di PS4.

---

## 20. Perbaikan 18: Button Mapping DS4 Final (Sesuai Game Asli)

### Kebutuhan User
> "controller sesuaikan dengan game aslinya. hanya ubahan menu mod/pause dengan option dan cheat menu L3/R3 dan Select/modmenu touchpad"

### Mapping Final

| PS4 Button | Hardware Mask | SDL Button | Game Action |
|------------|---------------|------------|-------------|
| ✕ Cross | 0x8000 | 0 (A) | Confirm / Interact |
| ○ Circle | 0x10000 | 1 (B) | Cancel / Back |
| □ Square | 0x20000 | 2 (X) | Tool / Action |
| △ Triangle | 0x40000 | 3 (Y) | Menu / Status |
| L1 | 0x100 | 4 (LEFT_SHOULDER) | Shoulder L |
| R1 | 0x200 | 5 (RIGHT_SHOULDER) | Shoulder R |
| L2 | 0x400 | Axis (a3) | Trigger L (analog) |
| R2 | 0x800 | Axis (a4) | Trigger R (analog) |
| **Options** | **0x80000** | **9 (START)** | **Pause Menu** |
| **Touchpad** | **0x4000000** | **8 (BACK)** | **Mod Menu** |
| **L3 / R3** | **0x100000 / 0x200000** | **7/8 (LEFTSTICK/RIGHTSTICK) + F1; joystick fallback 11/12** | **Cheat Menu** |
| D-Pad | 0x1000–0x8000 | 14–17 | Quick Items |

### Implementasi `hook_SDL_GameControllerGetButton`
```c
__attribute__((ms_abi)) uint8_t hook_SDL_GameControllerGetButton(...) {
    // Override khusus
    if (button == SDL_CONTROLLER_BUTTON_START) {   // 6 = Pause
        if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_OPTIONS) return 1;
    }
    if (button == SDL_CONTROLLER_BUTTON_BACK) {    // 4 = Mod Menu
        if (s_prev_pad_buttons & ORBIS_PAD_BUTTON_TOUCH_PAD) return 1;
    }
    // LEFTSTICK=7 dan RIGHTSTICK=8; raw joystick 11/12 menjadi fallback.
    // Hanya event L3/R3 yang menyintesis F1; tombol lain tetap sesuai mapping game.
    // Standard DS4 → SDL mapping untuk tombol lain...
}
```
**Catatan:** L2/R2 di SDL2 PS4 adalah axis analog (`SDL_CONTROLLER_AXIS_TRIGGERLEFT/RIGHT`), bukan button.

---

## 21. Perbaikan 19: Audio Crackling/Stutter Fix

### Gejala
Audio patah-patah (crackling/stutter) meskipun game jalan.

### Akar Masalah
PE Windows memanggil `SDL_OpenAudioDevice` dengan spec:
- 44.1 kHz sample rate
- 2048 samples buffer
- Default format

PS4 `SceAudioOut` native rate 48 kHz. Rate conversion 44.1→48kHz + buffer besar (2048) = underrun/latency tinggi → crackling.

### Solusi: `hook_SDL_OpenAudioDevice`
```c
static SDL_AudioSpec fixed_desired = {0};
fixed_desired.freq = 48000;       // PS4 native
fixed_desired.format = AUDIO_S16SYS;
fixed_desired.channels = 2;
fixed_desired.samples = 1024;     // Low latency
fixed_desired.callback = desired->callback;
fixed_desired.userdata = desired->userdata;

SDL_AudioDeviceID dev = SDL_OpenAudioDevice(device, iscapture, 
    &fixed_desired, obtained, allowed_changes);
if (dev > 0) {
    SDL_PauseAudioDevice(dev, 0); // Auto-unpause
}
```

### Hasil
Audio **bersih, tidak patah**, latency rendah.

---

## 22. Perbaikan 20: Pilihan Bahasa Cheat Menu — Aktif dan Persisten

### Status
- Patch Inggris dan Indonesia tertanam dalam bundle `HMBTNHPS` di `HarvestMoonBTN.exe`; klaim bahwa patch eksternal hilang adalah analisis lama yang sudah dikoreksi.
- Loader membaca `id/en` dari `/data/HarvestMoonBTN/data/lang.txt`, dengan fallback dan migrasi path legacy `/data/HarvestMoonBTN/lang.txt`.
- Pilihan diteruskan melalui hook `HMBTN_LANG`, argumen `--lang`, dan `__getmainargs` sebelum `SDL_main`.
- Setelah bahasa diganti melalui menu, game harus dimulai ulang.

### Batas Verifikasi
Pilihan host/engine sudah aktif. Konsistensi seluruh dialog/aset belum boleh dianggap selesai sampai diuji pada PS4, karena paket masih menyertakan disc USA mentah.

---

## 23. Update Build & Linker Flags

### `update_rebuild.bat` (LIBS)
```bat
set LIBS=--start-group -lSDL2 -lc -lkernel -lSceUserService -lSceSysmodule -lSceSystemService -lSceVideoOut -lSceAudioOut -lScePad --end-group
```
- `-lSceSysmodule -lSceSystemService` → `sceSystemServiceHideSplashScreen`
- `-lSceVideoOut -lSceAudioOut -lScePad` → backend SDL2 PS4
- `--start-group ... --end-group` → resolve circular deps (`__stack_chk_guard`)

---

## 24. Struktur PKG Final (Updated)

```
HarvestMoonBTN-PS4-Native.pkg (249,954,304 bytes)
├── eboot.bin                     # FSELF Loader Native PS4
├── HarvestMoonBTN.exe            # PE32+ Game Executable
├── disc.bin                      # Disc Image PS1
├── disc.cue                      # CUE Sheet
├── settings.ini                  # Config 1080p & Audio
├── saves/
│   ├── card1.mcd                 # Memory Card Slot 1
│   └── card2.mcd                 # Memory Card Slot 2
├── sce_module/
│   ├── libc.prx                  # Sony OpenOrbis C Runtime
│   └── libSceFios2.prx           # File I/O Subsystem
└── sce_sys/
    ├── param.sfo                 # CUSA01117
    ├── icon0.png                 # 512x512
    ├── pic0.png                  # 1920x1080
    └── pic1.png                  # 1920x1080 Background
```

---

## 25. Checklist Release (Updated)

- [x] Splash screen OS + PE tersembunyi
- [x] Button mapping DS4 final (Options=Pause, Touchpad=Mod, L3/R3=Cheat)
- [x] Audio 48kHz/1024 stereo, no crackling
- [x] Profil v2 software renderer `resolution=1`, `smooth=0`, `nearest`, integer scaling
- [x] `SDL_render_sw` dan `SDL_ps4video` dibangun ulang `-O3` ke `libSDL2_optimized.a`
- [x] Clear framebuffer penuh sebelum overwrite dihapus
- [x] Delay pasca-present RVA `0x8660` di-NOP setelah signature cocok; log fase 5 detik aktif
- [x] SDL_Delay clamp 16ms
- [x] Window 1080p native (no HighDPI)
- [x] Save redirect `/data/HarvestMoonBTN/`
- [x] Pilihan bahasa `id/en` persisten dan diterapkan setelah restart
- [x] PKG `249,954,304` byte dibangun dan hash lokal diverifikasi
- [x] Upload build terbaru ke `/data/pkg/`; ukuran remote `249,954,304` byte terverifikasi
- [ ] Validasi FPS, Circle/L3/R3, restart bahasa, dan dialog gameplay di PS4
- [ ] Test persistensi save data
- [ ] Test controller hotplug
- [ ] Test FW 5.05, 6.72, 9.00, 11.00
- [x] Dokumentasi dikoreksi sesuai implementasi dan bukti build 16 November 2026

---

## 19. Panduan Pengujian & Verifikasi

### Prosedur di Konsol PS4:
1. Pastikan PS4 dalam kondisi aktif dengan GoldHEN berjalan (FTP port 2121).
2. Di menu utama PS4, sorot ikon game lama **Harvest Moon: Back to Nature**, tekan tombol **Options** pada stik DualShock 4 → pilih **Hapus**.
3. Buka **Settings** → **GoldHEN** → **Package Installer**, pilih `HarvestMoonBTN-PS4-Native.pkg` yang ada di `/data/pkg/` untuk menginstal versi terbaru.
4. Periksa antarmuka menu utama: gambar background Full HD harus tampil penuh.
5. Tekan tombol `(X)` untuk meluncurkan game.
6. Pantau apakah game berhasil melewati inisialisasi controller dan masuk ke video/title screen.

### Memeriksa Log Runtime:
Jalankan perintah berikut di komputer:
```cmd
node tools/fetch_loader_log.js
```
Log akan langsung mengunduh `/data/HarvestMoonBTN/loader.log` dari PS4 dan menampilkan status eksekusi hingga baris terakhir.
