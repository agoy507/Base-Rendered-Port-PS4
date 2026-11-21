# Harvest Moon: Back to Nature — Dokumentasi Port PS4

> **ARSIP LEGACY — BUKAN JALUR PRODUKSI.** Klaim performa dan arsitektur PE loader di dokumen ini tidak berlaku untuk rebuild native baru dan tidak menjadi bukti 1080p 60 FPS.

> Dibuat: 2026-11-03 | Bahasa: Bahasa Indonesia

---

## 🎮 Ringkasan Proyek

| Item | Detail |
|------|--------|
| Game | Harvest Moon: Back to Nature (SLUS01115) |
| Target | Port ke PS4 (homebrew) |
| Platform Asal | PS1 (NTSC-U) |
| Mod Dasar | Penyu Mod v1.3.0 (terjemahan Indonesia) |
| Working Dir | `E:\PS4\hmbtn\` |

---

## ✅ Tahap 1 — fPKG PS4 (PS1 Emulator) — SELESAI

### File yang Dihasilkan

| File | Path | Ukuran |
|------|------|--------|
| PKG utama | `E:\PS4\hmbtn\Harvest Moon - Back to Nature (Mod Penyu v1.3.0) [SLUS01115].pkg` | 137.7 MB |
| PKG alternatif | `E:\PS4\UP9000-SLUS01115_00-SLUS01115PS1FPKG-A0100-V0100.pkg` | sama |

### Konfigurasi PKG

```
Content ID : UP9000-SLUS01115_00-SLUS01115PS1FPKG
Title      : Harvest Moon: Back to Nature
Version    : 01.00
Analog fix : --sim-analog-pad=0x2020 (stick kiri+kanan diaktifkan)
Emulator   : PS Classics (bawaan PS4)
```

### Tools yang Dipakai
- PS Classics fPKG Builder v1 — `E:\PS4\PS Classics fPKG Builder v1\`
- `orbis-pub-cmd.exe` untuk generate PKG

> [!NOTE]
> Fix volume_ts: format awal `2026-11-03 00.00.00` (titik) → diubah ke `2026-11-03 00:00:00` (titik dua) agar diterima orbis-pub-cmd.

---

## 🔬 Tahap 2 — Analisis HarvestMoonBTN.exe (Windows Native Port)

### Target Binary

| Property | Value |
|----------|-------|
| Path | `E:\PS4\hmbtn\HarvestMoonBTN-ID-Windows\HarvestMoonBTN.exe` |
| Ukuran | 57,860,957 bytes (~55.2 MB) |
| SHA256 | `18577c36b79b6e02d184a79e49499b3c90b7ecf4b296a92c3deb1032b803e369` |
| Format | PE32+ (x86-64) |
| Compiler | MinGW-w64 / GCC |

### Arsitektur Internal (Hasil Analisis Manual)

```
┌─────────────────────────────────────────────────────────────┐
│  HarvestMoonBTN.exe                                         │
│                                                             │
│  [0x000000 – 0x1FF60D]  ← Kode Executable (~2.09 MB)       │
│    • SDL2 host (window, renderer, input, audio)             │
│    • wasm2c runtime (wasm-rt-impl.c)                        │
│    • VFS loader (baca blob HMBTNPZ1)                        │
│    • game.c (hasil transpilasi PS1 MIPS → WASM → C)         │
│                                                             │
│  [0x1FF60E – 0x372E46D]  ← Embedded Blob (~55.7 MB)        │
│    • Magic: HMBTNPZ1                                        │
│    • Footer: HMBTNEMB                                       │
│    • 2,373 file, semua zlib-compressed (78 DA)              │
│    • Header size: 19,016 bytes (0x4A48)                     │
│    • Isi: DISC.bin, DISC.cue, asset game, settings.ini      │
└─────────────────────────────────────────────────────────────┘
```

### Pipeline Konversi Game

```
PS1 MIPS Binary
      ↓  (wasm-opt / emscripten)
   WebAssembly
      ↓  (wasm2c)
   game.c (C transpilasi)
      ↓  (GCC MinGW-w64)
   HarvestMoonBTN.exe
```

### Build Paths (dari DWARF Debug Info)

| File | Path di Build Server |
|------|---------------------|
| Game logic | `/home/claude/native/build/game.c` |
| WASM runtime | `/home/claude/native/host/w2c/wasm-rt-impl.c` |
| PS1 disc impl | `DISC.c` |
| Host impl | `impl.c` |

---

## 🔧 Tahap 3 — Setup REA + Ghidra

### Tools yang Diinstall

| Tool | Versi | Path |
|------|-------|------|
| Ghidra | 12.2 PUBLIC | `E:\PS4\ghidra_12.2_PUBLIC\` |
| Java (JDK) | OpenJDK 25.0.3 | `C:\Program Files\Android\Android Studio\jbr\` |
| REA (rea-agents) | 6.7.0 / 6.8.0 | via `npx rea-agents` |
| 7-Zip | — | `C:\Program Files\7-Zip\7z.exe` |

### Patch yang Diterapkan pada REA

REA 6.7.0 hanya support Ghidra 12.1.x — perlu patch manual karena kita punya 12.2:

| File | Perubahan |
|------|-----------|
| `rea-agents/dist/ghidra/GhidraReleasePolicy.js` | `"12.1.4"` → `"12.2.0"` |
| `rea-agents/dist/mcp/main-Cx-Z7m_n.js` | `SUPPORTED_GHIDRA_VERSION = "12.1.4"` → `"12.2.0"` |
| `rea-agents/dist/cli-runtime/KunaInstallation-P0j3x851.js` | sama |

> [!IMPORTANT]
> Jalankan REA selalu dengan env vars berikut:
> ```powershell
> $env:GHIDRA_INSTALL_DIR = "E:\PS4\ghidra_12.2_PUBLIC"
> $env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
> $env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
> $env:REA_GHIDRA_STARTUP_TIMEOUT_MS = "900000"
> powershell -ExecutionPolicy Bypass -Command "npx rea-agents <cmd>"
> ```

### Error & Fix

| Error | Penyebab | Fix |
|-------|----------|-----|
| `npx` diblokir | PowerShell Execution Policy | `powershell -ExecutionPolicy Bypass -Command "npx ..."` |
| `provider_unavailable` | Ghidra 12.2 tidak didukung REA | Patch 3 file JS di node_modules |
| `provider_timeout` | Binary 57MB butuh >330s | `REA_GHIDRA_STARTUP_TIMEOUT_MS=900000` |
| `IllegalArgumentException: Unknown analyzer` | Nama analyzer salah di Ghidra 12.2 | Jangan pakai `REA_GHIDRA_DISABLED_ANALYZERS` |
| Java tidak di PATH | Android Studio JBR tidak di PATH | Set `JAVA_HOME` & `PATH` secara eksplisit |

---

## 📊 Tahap 4 — Hasil REA Analyze (✅ SUKSES)

**Command yang berhasil:**
```powershell
$env:REA_GHIDRA_STARTUP_TIMEOUT_MS = "900000"
npx rea-agents analyze 'E:\PS4\hmbtn\HarvestMoonBTN-ID-Windows\HarvestMoonBTN.exe' --provider ghidra --format json
```

### Binary Overview

| Field | Value |
|-------|-------|
| Format | PE x86_64 |
| Image Base | `0x140000000` |
| Segments | 20 |
| **Fungsi teridentifikasi** | **2,176** |
| **String teridentifikasi** | **4,087** |

### Memory Map Lengkap

| Segment | Start | End | Ukuran | Keterangan |
|---------|-------|-----|--------|------------|
| Headers | `0x140000000` | `0x140000600` | 1.5 KB | PE header |
| `.text` | `0x140001000` | `0x14012d000` | **1.2 MB** | Kode (game + SDL + runtime) |
| `.data` | `0x14012d000` | `0x14012e000` | 4 KB | Data inisialisasi |
| `.rdata` | `0x14012e000` | `0x140176000` | 288 KB | Read-only data, string literals |
| `.pdata` | `0x140176000` | `0x14017c000` | 24 KB | Exception handling |
| `.xdata` | `0x14017c000` | `0x140183000` | 28 KB | Exception unwind |
| `.bss` | `0x140183000` | `0x1408d9000` | **7.5 MB** | ⚠️ WASM linear memory |
| `.idata` | `0x1408d9000` | `0x1408db000` | 8 KB | Import table |
| `.CRT` | `0x1408db000` | `0x1408dc000` | 4 KB | C runtime init |
| `.tls` | `0x1408dc000` | `0x1408dd000` | 4 KB | Thread local storage |
| `.reloc` | `0x1408dd000` | `0x1408de000` | 4 KB | Relokasi |
| `.debug_aranges` | `0x1408de000` | `0x1408df000` | 4 KB | DWARF ranges |
| `.debug_info` | `0x1408df000` | `0x140905000` | **152 KB** | DWARF type info |
| `.debug_abbrev` | `0x140905000` | `0x14090c000` | 28 KB | DWARF abbrev |
| `.debug_line` | `0x14090c000` | `0x14091a000` | 56 KB | DWARF line numbers |
| `.debug_frame` | `0x14091a000` | `0x14091f000` | 20 KB | DWARF frame info |
| `.debug_str` | `0x14091f000` | `0x140920000` | 4 KB | DWARF strings |
| `.debug_line_str` | `0x140920000` | `0x140924000` | 16 KB | DWARF line strings |
| `.debug_loclists` | `0x140924000` | `0x140932000` | 56 KB | DWARF loc lists |
| `.debug_rnglists` | `0x140932000` | `0x140933000` | 4 KB | DWARF range lists |

> [!NOTE]
> `.bss` sebesar 7.5 MB adalah ciri khas wasm2c — ini adalah WASM linear memory yang dialokasikan secara statis. Seluruh state game PS1 (RAM, VRAM, SPU) hidup di sini.

### Import DLL

| DLL | Status saat Analisis |
|-----|---------------------|
| `SDL2.DLL` | Not found (perlu disertakan) |
| `KERNEL32.DLL` | ✅ Windows system |
| `MSVCRT.DLL` | ✅ Windows system |
| `COMDLG32.DLL` | ✅ Windows system |
| `SHELL32.DLL` | ✅ Windows system |

---

## 🔍 Tahap 5 — REA Search Results

### SDL Procedures (✅ Selesai — task-461)

**Entry point game:** `SDL_main` @ `0x140006310`

#### 39 SDL2 Functions yang Digunakan

| Kategori | Fungsi SDL2 | Keterangan |
|----------|-------------|------------|
| **Init** | `SDL_Init`, `SDL_InitSubSystem`, `SDL_SetMainReady` | Inisialisasi SDL |
| **Window** | `SDL_CreateWindow`, `SDL_SetWindowFullscreen`, `SDL_SetHint` | Manajemen window |
| **Renderer** | `SDL_CreateRenderer`, `SDL_RenderClear`, `SDL_RenderCopy`, `SDL_RenderPresent`, `SDL_RenderSetLogicalSize` | Render pipeline |
| **Texture** | `SDL_CreateTexture`, `SDL_UpdateTexture`, `SDL_DestroyTexture` | Framebuffer PS1 |
| **Audio** | `SDL_OpenAudioDevice`, `SDL_QueueAudio`, `SDL_PauseAudioDevice`, `SDL_ClearQueuedAudio`, `SDL_GetQueuedAudioSize` | Audio SPU |
| **Input** | `SDL_PollEvent`, `SDL_GameControllerOpen`, `SDL_GameControllerGetAxis`, `SDL_GameControllerGetButton`, `SDL_IsGameController`, `SDL_NumJoysticks` | Input PS1 |
| **Keyboard** | `SDL_GetKeyFromName`, `SDL_GetKeyName` | Keyboard mapping |
| **Timing** | `SDL_GetPerformanceCounter`, `SDL_GetPerformanceFrequency`, `SDL_Delay` | Frame timing |
| **Util** | `SDL_GetError`, `SDL_GetBasePath`, `SDL_GetCPUCount`, `SDL_ShowSimpleMessageBox` | Utilitas |
| **String** | `SDL_strlen`, `SDL_wcslen`, `SDL_memcpy`, `SDL_iconv_string`, `SDL_free` | String/mem helper |

> [!IMPORTANT]
> **PS4 Mapping Plan** — setiap SDL2 call perlu diganti:
> - `SDL_CreateWindow` + `SDL_CreateRenderer` → `sceVideoOutOpen` + `sceVideoOutSubmitFlip`
> - `SDL_UpdateTexture` + `SDL_RenderPresent` → write ke framebuffer PS4 + `sceVideoOutSubmitFlip`
> - `SDL_OpenAudioDevice` + `SDL_QueueAudio` → `sceAudioOutOpen` + `sceAudioOutOutput`
> - `SDL_PollEvent` + Controller → `scePadReadState` + `sceUserServiceGetLoginUserIdList`
> - `SDL_GetPerformanceCounter` → `sceKernelGetProcessTime`
> - `SDL_GetBasePath` → `/hostapp/` atau `/data/`

### wasm2c Procedures (✅ Selesai — task-462)

> Ditemukan ratusan fungsi `w2c_*` dengan nama lengkap! Ini karena DWARF debug info tersimpan di binary.

#### 🔌 Host Interface Functions (perlu diganti untuk PS4)

| Fungsi | Alamat | Kategori | Pengganti PS4 |
|--------|--------|----------|---------------|
| `w2c_host_gpu_draw_ot` | `0x140002380` | GPU | `sceGnmSubmitCommandBuffers` |
| `w2c_host_gpu_command` | `0x1400024d0` | GPU | GNM command processing |
| `w2c_host_gpu_drawenv` | `0x1400024f0` | GPU | Draw environment setup |
| `w2c_host_gpu_dispenv` | `0x140002500` | GPU | Display environment |
| `w2c_host_gpu_display` | `0x140002510` | GPU | `sceVideoOutSubmitFlip` |
| `w2c_host_gpu_load_image` | `0x140002530` | GPU/VRAM | Texture upload |
| `w2c_host_gpu_store_image` | `0x140002590` | GPU/VRAM | Texture readback |
| `w2c_host_gpu_move_image` | `0x1400025f0` | GPU/VRAM | VRAM copy |
| `w2c_host_gpu_clear_image` | `0x140002640` | GPU/VRAM | Clear |
| `w2c_host_cd_search` | `0x140002690` | CD/VFS | `open()` pada `/hostapp/` |
| `w2c_host_cd_read` | `0x140002710` | CD/VFS | `read()` |
| `w2c_host_cd_xa_play` | `0x140002740` | Audio/XA | `sceAudioOutOutput` |
| `w2c_host_cd_xa_stop` | `0x140002750` | Audio/XA | stop audio |
| `w2c_host_cd_xa_pos` | `0x140002760` | Audio/XA | audio position |
| `w2c_host_vsync_pending` | `0x140005110` | Timing | `sceVideoOutIsFlipPending` |
| `w2c_host_vsync_wait` | `0x140005260` | Timing | `sceVideoOutWaitVblank` |
| `w2c_host_config` | `0x140005380` | Config | settings.ini → PS4 param |
| `w2c_host_pad_read` | `0x140005ec0` | Input | `scePadReadState` |
| `w2c_host_mc_exists` | `0x140006020` | Save | `sceSaveDataMount` |
| `w2c_host_mc_dir` | `0x140006030` | Save | `sceSaveDataDirNameSearch` |
| `w2c_host_mc_read` | `0x140006050` | Save | `sceSaveDataMount` + `read` |
| `w2c_host_mc_write` | `0x140006080` | Save | `sceSaveDataMount` + `write` |
| `w2c_host_mc_create` | `0x1400060b0` | Save | `sceSaveDataCreate` |
| `w2c_host_mc_format` | `0x1400060d0` | Save | format save |
| `w2c_host_snd_call` | `0x1400060e0` | Audio/SPU | `sceAudioOutOutput` |
| `w2c_host_math1` | `0x1400060f0` | Math/GTE | PS1 GTE emulation |
| `w2c_host_math2` | `0x140006150` | Math/GTE | PS1 GTE emulation |
| `w2c_host_print` | `0x140006160` | Debug | `sceKernelDebugOutText` |
| `w2c_host_abort` | `0x140006190` | Error | `exit()` |

#### 🎮 Game Logic Functions (tetap dipakai di PS4)

**Entry Points Kunci:**
| Fungsi | Alamat | Keterangan |
|--------|--------|------------|
| `SDL_main` | `0x140006310` | **C entry point** |
| `w2c_game_main` | `0x14010cc40` | **WASM game main** |
| `w2c_game_memory` | `0x14010cc30` | WASM memory init |
| `w2c_game_0x5F_stack_pointer` | `0x14010cc30` | WASM stack pointer |
| `w2c_game_native_overlay_init` | `0x14010ef40` | Overlay/mod init |

**Game Loop:**
| Fungsi | Alamat | Keterangan |
|--------|--------|------------|
| `w2c_game_player_frame_update` | `0x1400ffdd0` | Update player per frame |
| `w2c_game_player_input_step` | `0x1400fae30` | Proses input player |
| `w2c_game_player_run_action` | `0x1400fcb90` | Aksi player |
| `w2c_game_player_apply_tool_target` | `0x1400fab10` | Gunakan tool |
| `w2c_game_farm_mode_tick` | `0x140108960` | Tick mode farm |
| `w2c_game_farm_run_frame_subsystems` | `0x140106130` | Frame subsystems |
| `w2c_game_clock_frame_update` | `0x1400f7340` | Update jam game |
| `w2c_game_farm_startup_step` | `0x1400f4650` | Load farm |
| `w2c_game_farm_overnight_finish` | `0x1400f70e0` | Selesai tidur |
| `w2c_game_scene_overnight_step` | `0x1400f8640` | Scene tidur |
| `w2c_game_event_cmd_dispatch` | `0x1400efe50` | Event system |
| `w2c_game_title_sequence_step` | `0x14010b180` | Title screen |

**Save/Data:**
| Fungsi | Alamat | Keterangan |
|--------|--------|------------|
| `w2c_game_save_count_page_entries` | `0x140014810` | Hitung save entries |
| `w2c_game_menu_run_state` | `0x140106010` | Menu state |

**Minigame:**
| Fungsi | Alamat | Keterangan |
|--------|--------|------------|
| `w2c_game_mg1_run_screen` | `0x140109e20` | Minigame 1 |
| `w2c_game_mg2_state_run` | `0x14010a170` | Minigame 2 |
| `w2c_game_mg3_round_dispatch` | `0x14010a520` | Minigame 3 |
| `w2c_game_mg4_state_tick` | `0x14010a8d0` | Minigame 4 |
| `w2c_game_mg5_frame_update` | `0x14010a9f0` | Minigame 5 |

**NPC & Event:**
| Fungsi | Alamat | Keterangan |
|--------|--------|------------|
| `w2c_game_thomas_arrived` | `0x140014de0` | NPC Thomas |
| `w2c_game_claire_open_door` | `0x140014e50` | NPC Claire |
| `w2c_game_dog_script_follow_motion` | `0x140105c10` | Anjing mengikuti |
| `w2c_game_shop_stock_step` | `0x140105030` | Stok toko |
| `w2c_game_shop_greenhouse_step` | `0x1401048c0` | Greenhouse |

**Mod (Penyu Mod v1.3.0):**
| Fungsi | Alamat | Keterangan |
|--------|--------|------------|
| `w2c_game_mod_picture_id` | `0x140014f40` | Picture ID mod |
| `w2c_game_mod_gift_note` | `0x140014fa0` | Gift note mod |
| `w2c_game_mod_chicken_shop_price_impl` | `0x140015000` | Harga ayam mod |
| `w2c_game_mod_glyph` | `0x140015060` | Glyph/font mod |
| `w2c_game_mod_horse` | `0x140015...` | Horse mod |
| `w2c_game_mod_v3_tick` | `0x1400ee070` | Mod v3 tick |
| `w2c_game_mod_frame_tick_impl` | `0x1400efe50` | Mod frame tick |
| `w2c_game_mod_quest_progress` | `0x14012cd3e` | Quest mod |

---

## 💡 Tahap 6 — Decompile SDL_main (✅ Selesai)

**Alamat:** `0x140006310` | **Fungsi:** entry point game (host layer)

### Kode C Decompiled (Ghidra pseudocode — disederhanakan)

```c
undefined4 SDL_main(ulonglong argc, longlong argv, char *envp)
{
    // ── 1. Argument parsing ──────────────────────────────────────
    // --headless  → skip window/audio (benchmark mode)
    // --frames N  → batas jumlah frame

    // ── 2. Load disc image ───────────────────────────────────────
    GetModuleFileNameA(NULL, exe_path, 0x1000);
    iVar7 = cd_open_embedded(exe_path);   // baca blob HMBTNPZ1 dari .exe sendiri
    if (iVar7 == 0) {
        // Fallback: cari .cue / .bin di SDL_GetBasePath()
        szPath = SDL_GetBasePath();
        dirp = opendir(szPath);
        // scan for .cue/.CUE → .bin/.BIN
    }

    // ── 3. Audio init ────────────────────────────────────────────
    // Format: 44100 Hz, S16LE, stereo, 1024 samples
    SDL_OpenAudioDevice(0, 0, &audio_spec, obtained, 0);
    // audio_spec = { freq=0xAC44=44100, format=0x8010=AUDIO_S16LSB,
    //                channels=2, samples=0x400=1024 }
    SDL_PauseAudioDevice(audio_dev, 0);   // unmute

    // ── 4. Timing (VBlank NTSC) ──────────────────────────────────
    perf_freq = SDL_GetPerformanceFrequency();
    vblank_period = (longlong)(perf_freq / 59.94);   // NTSC PS1!
    next_vblank = SDL_GetPerformanceCounter() + vblank_period;

    // ── 5. Window & Renderer ─────────────────────────────────────
    window = SDL_CreateWindow(
        "Harvest Moon: Back to Nature - Mod by Penyu",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (gpu_wide + 0xa0) * scale * 2,   // lebar (skala x2)
        scale * 0xf0,                     // tinggi (240px * scale)
        SDL_WINDOW_SHOWN
    );
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (fullscreen) SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

    // ── 6. Gamepad init ──────────────────────────────────────────
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            break;
        }
    }

    // ── 7. WASM runtime init ─────────────────────────────────────
    gpu_reset();
    wasm_rt_init();
    _host = &game;
    wasm2c_game_instantiate(&game, &host);   // ← instantiate WASM module
    M      = DAT_1405bff10;                  // WASM linear memory ptr (di .bss)
    M_size = DAT_1405bff28;                  // WASM linear memory size
    state_build = crc32(0) ^ 0x8407921b;     // build checksum

    // ── 8. Game start ────────────────────────────────────────────
    w2c_game_native_overlay_init(&game);     // init Penyu mod
    w2c_game_main(&game, 0, 0);              // ← game loop dimulai di sini

    // ── 9. Cleanup ───────────────────────────────────────────────
    fprintf(stderr, "[host] the game's main returned\n");
    wasm2c_game_free(&game);
    wasm_rt_free();
    return 0;
}
```

### Temuan Kritis dari Decompile

| Temuan | Detail |
|--------|--------|
| **Window title** | `"Harvest Moon: Back to Nature - Mod by Penyu"` |
| **Audio format** | 44100 Hz, S16LE, stereo, 1024 samples buffer |
| **VBlank rate** | `59.94 Hz` (NTSC PS1) |
| **Resolusi** | `(gpu_wide + 160) × scale × 2` wide, `scale × 240` tall |
| **WASM memory** | Pointer di `0x1405bff10` (dalam `.bss` 7.5MB) |
| **Disc loading** | `cd_open_embedded()` → baca HMBTNPZ1 blob dari .exe |
| **Mod init** | `w2c_game_native_overlay_init()` sebelum `w2c_game_main()` |
| **Game entry** | `w2c_game_main(&game, 0, 0)` — blocking loop |

### PS4 Host Layer — Template Port

Berdasarkan decompile ini, berikut skeleton host layer untuk PS4:

```c
// ps4_host.c — Host layer pengganti SDL2 untuk PS4
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/Pad.h>
#include <orbis/AudioOut.h>
#include <orbis/SaveData.h>
#include "wasm-rt.h"
#include "game.h"

static w2c_game game;
static struct w2c_game_host host;  // host interface callbacks

int main(void) {
    // 1. Video Out
    int video_handle = sceVideoOutOpen(
        SCE_USER_SERVICE_USER_ID_SYSTEM,
        SCE_VIDEO_OUT_BUS_TYPE_MAIN, 0, NULL
    );
    // setup framebuffer 640×480 atau sesuai gpu_wide

    // 2. Audio (44100Hz S16LE stereo 1024-sample)
    int audio_handle = sceAudioOutOpen(
        SCE_USER_SERVICE_USER_ID_SYSTEM,
        SCE_AUDIO_OUT_PORT_TYPE_MAIN,
        0, 1024, 44100,
        SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO
    );

    // 3. Pad
    scePadInit();
    int pad_handle = scePadOpen(
        SCE_USER_SERVICE_USER_ID_SYSTEM,
        SCE_PAD_PORT_TYPE_STANDARD, 0, NULL
    );

    // 4. Load disc dari /hostapp/ atau embedded pkg data
    cd_open_ps4("/hostapp/DISC.bin");  // atau VFS dari pkg

    // 5. WASM init — SAMA dengan Windows
    gpu_reset();
    wasm_rt_init();
    _host = &game;
    wasm2c_game_instantiate(&game, &host);

    // 6. Run game
    w2c_game_native_overlay_init(&game);
    w2c_game_main(&game, 0, 0);

    // 7. Cleanup
    wasm2c_game_free(&game);
    wasm_rt_free();
    return 0;
}
```

> [!TIP]
> Fungsi `w2c_game_main`, `wasm2c_game_instantiate`, `wasm_rt_init`, dan seluruh `w2c_game_*` **tidak perlu diubah** — hanya perlu recompile dengan OpenOrbis toolchain. Yang perlu ditulis ulang hanya 29 fungsi `w2c_host_*`.

---

## 📋 Ringkasan: Apa yang Perlu Dilakukan untuk Port Native

### Fase 1 — Siapkan Source (Extract dari .exe)
- [ ] Extract `game.c` logic (via REA decompile semua `w2c_game_*`)
- [ ] Extract `wasm-rt-impl.c` (runtime WASM)
- [ ] Extract/rebuild `host` struct & callback table

### Fase 2 — Tulis PS4 Host Layer (29 fungsi)
- [ ] GPU: 9 fungsi `w2c_host_gpu_*` → PS4 GNM/framebuffer
- [ ] CD/VFS: 5 fungsi `w2c_host_cd_*` → baca dari `/hostapp/`
- [ ] Pad: 1 fungsi `w2c_host_pad_read` → `scePadReadState`
- [ ] VSync: 2 fungsi `w2c_host_vsync_*` → `sceVideoOutWaitVblank`
- [ ] Save: 5 fungsi `w2c_host_mc_*` → `sceSaveData*`
- [ ] Audio: 2 fungsi `w2c_host_snd_call`, `cd_xa_*` → `sceAudioOutOutput`
- [ ] Math/GTE: 2 fungsi `w2c_host_math*` → bisa tetap software

### Fase 3 — Build & Package
- [x] Download OpenOrbis toolchain v0.5.4 & Clang LLVM
- [x] Compile native loader dengan target FreeBSD 12 ELF x86-64
- [x] Package ke `.pkg` dengan `orbis-pub-cmd`
- [x] Sukses build versi 1.3.0 (283 MB) & versi 1.3.5 (241 MB)

---

## 🏆 Tahap 3 — Rebuild Native Sukses & Evolusi ke Mod Penyu v1.3.5 (SELESAI)

### 1. Transformasi Arsitektur: Native PE Image Loader
Alih-alih melanjutkan dekompilasi Ghidra yang terhambat pada resolusi ratusan simbol internal, proyek beralih ke arsitektur **Direct PE x86-64 Loader**:
- CPU PlayStation 4 adalah **AMD Jaguar x86-64**.
- Biner `HarvestMoonBTN.exe` versi PC MinGW adalah biner x86-64 murni.
- `ps4_loader.c` memetakan EXE langsung ke `0x140000000`, mengaitkan IAT ke libc FreeBSD & OpenOrbis SDL2, lalu mengeksekusi langsung `SDL_main` pada kecepatan 60 FPS 1080p native tanpa overhead emulasi.

### 2. Pembaruan ke Format MOD v1.3.5 (November 2026)
Format v1.3.5 membawa perubahan besar:
- **Pemisahan Data Game & Patch:** Biner game mengecil dari 58.4 MB ke 16.4 MB. Data game diambil langsung dari disc resmi SLUS-01115 (`assets/original_disc/disc.bin` & `disc.cue`) yang dibundel di dalam fPKG.
- **218 Imports Hooked:** 43 fungsi baru ditambahkan, termasuk emulasi thread Win32 (`CreateThread`), stubs aman WinHTTP, dan implementasi naked assembly `_setjmp`/`longjmp`.
- **Kontroler:** Khusus native DualShock 4 (mapping pihak ketiga dinonaktifkan).
- **Auto-Updater 1-Klik:** Disediakan `check_update.bat` yang otomatis memeriksa server Si Penyu, mengunduh rilis baru, memverifikasi hash SHA256, dan me-rebuild PKG secara mandiri.
- **Ukuran Paket Akhir:** Menurun drastis dari 283 MB menjadi **241.1 MB** di `releases/HarvestMoonBTN-PS4-Native.pkg`.

---

## 📁 File Tree Proyek Terkini

```text
E:\PS4\hmbtn\
├── archive/                     # Arsip versi lama (exe_v1.3.0, dekompilasi Ghidra & WASM)
├── assets/                      # Artwork + original_disc/ (disc ASLI SLUS-01115)
├── build_ps4/                   # Generator loader, analyzer PE, SFO/GP4, & updater
├── docs/                        # Dokumentasi teknis lengkap & bug reports
├── HarvestMoonBTN-ID-Windows/   # Game Windows PC asli (HarvestMoonBTN.exe v1.3.5)
├── pkg_staging/                 # Staging folder perakitan fPKG eboot.bin
├── releases/                    # Output fPKG siap install (HarvestMoonBTN-PS4-Native.pkg)
├── src/                         # Sumber aktif loader native (ps4_loader.c)
├── check_update.bat             # Skrip pembaruan otomatis 1-klik
├── clean.bat                    # Skrip pembersih cache build
├── update_rebuild.bat           # Skrip kompilasi & packaging master
└── README.md                    # Dokumentasi utama proyek
```
