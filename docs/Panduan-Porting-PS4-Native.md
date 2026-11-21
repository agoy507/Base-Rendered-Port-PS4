# Panduan Porting Harvest Moon: Back to Nature ke PS4 Native (OpenOrbis)

> **ARSIP LEGACY — BUKAN JALUR PRODUKSI.** Gunakan dokumen ini hanya sebagai riwayat loader. Petunjuk aktif berada pada `Native-Rebuild-1080p60.md` dan `Native-GPU-Probe-Runbook.md`.

**Versi:** 1.0  
**Target:** PS4 FW 5.05–11.00+ (GoldHEN / Mira)  
**Toolchain:** OpenOrbis v0.5.4, Clang LLVM-23, musl libc FreeBSD  
**Game Source:** Windows PE (`HarvestMoonBTN.exe`, base VA `0x140000000`, SDL2-based)

---

## 1. Arsitektur & Strategi Umum

| Lapisan | Deskripsi |
|---------|-----------|
| **Game Logic** | Tetap berjalan sebagai x86-64 machine code asli (tidak di-recompile). PE Windows di-load di memori PS4 pada `ImageBase` preferensi `0x140000000`. |
| **Loader C** | `src/ps4_loader.c` (digen dari `build_ps4/gen_loader.js`) — memetakan PE, resolve imports, patch in-memory, hook SDL2, redirect I/O ke `/data/HarvestMoonBTN/`. |
| **SDL2 PS4** | `build_ps4/libSDL2_optimized.a` — renderer software `-O3`, ScePad (input), SceAudioOut (audio), SceVideoOut (vsync). Driver renderer GNM pada library lama hanya stub. |
| **System Services** | `sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE)` + `sceSystemServiceHideSplashScreen()` untuk sembunyikan splash OS. |
| **Packaging** | `orbis-pub-cmd` → `eboot.bin` (signed ELF) + `param.sfo` → PKG (CUSA01117). |

---

## 2. Tahapan Porting

### 2.1 Analisis PE Windows (Reverse Engineering)

**Tools:** `pefile`, `capstone`, `objdump`, `nm`, IDA/Ghidra (manual)

| Temuan | Detail |
|--------|--------|
| **ImageBase** | `0x140000000` (64-bit PE) |
| **Entry Point** | RVA `0x13e0` → CRT startup → `SDL_main` di RVA `0xA050` |
| **Imports** | 220 fungsi: `msvcrt.dll` (CRT), `SDL2.dll`, `kernel32.dll`, `user32.dll`, dll |
| **SDL_main** | Offset `0xA050` dari `ImageBase` — titik masuk logika game |
| **Button Hook** | `SDL_GameControllerGetButton` di VA `0x1400068C0`, pakai array SDL Button ID (14 entry) di `.data` `0x1412941A0` |
| **PS1 Button Masks** | Array 14 entry di `.rdata` `0x1402DAE80`: Cross/Circle/Square/Triangle/L1/R1/L2/R2/Select/Start/L3/R3/DPAD×2 |
| **Audio Init** | `SDL_OpenAudioDevice` dipanggil di VA `0x14000A242` (spec: 44.1kHz, stereo, 2048 samples) |
| **Language System** | `getenv("HMBTN_LANG")` di VA `0x14000A17C` → format `patch_%s.hmp` (`patch_id.hmp` untuk Indonesia) di VA `0x1402D6E98` |
| **Cheat Menu** | Handler di `0x1400077EC` (7 opsi), bahasa di index 3 → `0x140007DEA` → `0x1402CCFE0` set flag `0x1402D448F=1` + pesan restart |
| **Welcome Overlay** | Fungsi di `0x140004420` (entry) & `0x1400020F0` (sub-rutin gambar) — perlu di-skip |

### 2.2 Generator Loader (`build_ps4/gen_loader.js`)

File JS menghasilkan `src/ps4_loader.c` lengkap. **Kunci desain:**

```javascript
// Konfigurasi konstan
const PS4_SCREEN_WIDTH  = 1920;
const PS4_SCREEN_HEIGHT = 1080;
const SDL_MAIN_OFFSET   = 0xA050;           // dari analisis PE
const IMAGE_BASE        = 0x140000000n;
```

**Fungsi utama generator:**
- `generatePELoader()` — mmap PE, copy sections, relocations, IAT resolve
- `generateSDLHooks()` — hook SDL_CreateWindow, SDL_CreateRenderer, SDL_RenderPresent, SDL_OpenAudioDevice, SDL_Delay, SDL_GameControllerGetButton
- `generateVideoAudioInit()` — init SceVideoOut/SceAudioOut, vsync, flip queue
- `generateMain()` — entry point, load sysmodule, hide splash, setenv, call SDL_main

### 2.3 Build System (`update_rebuild.bat`)

```bat
set LIBS=--start-group -lSDL2 -lc -lkernel -lSceUserService -lSceSysmodule -lSceSystemService -lSceVideoOut -lSceAudioOut -lScePad --end-group
```

**Link flags kritis:**
- `-lSceSysmodule -lSceSystemService` → resolve `sceSystemServiceHideSplashScreen`
- `-lSceVideoOut -lSceAudioOut -lScePad` → backend SDL2 PS4

---

## 3. Perbaikan Kritis (Chronological)

### 3.1 Sembunyikan Splash Screen OS + Welcome Overlay PE
| Layer | Metode |
|-------|--------|
| **OS** | `sceSysmoduleLoadModuleInternal(0x80000010)` + `sceSystemServiceHideSplashScreen()` di `hook_SDL_RenderPresent` frame pertama |
| **PE (in-memory)** | Byte patch `0xC3` (RET) di `mapped_base[0x4420]` & `mapped_base[0x20F0]` sebelum `SDL_main` dipanggil |

### 3.2 Button Mapping DS4 → SDL (Final)
| PS4 Button | Hardware Mask | SDL Button | Game Action |
|------------|---------------|------------|-------------|
| ✕ Cross | 0x8000 | 0 (A) | Confirm / Interact |
| ○ Circle | 0x10000 | 1 (B) | Cancel / Back |
| □ Square | 0x20000 | 2 (X) | Tool / Action |
| △ Triangle | 0x40000 | 3 (Y) | Menu / Status |
| L1 | 0x100 | 4 (LEFT_SHOULDER) | Shoulder L |
| R1 | 0x200 | 5 (RIGHT_SHOULDER) | Shoulder R |
| L2 | 0x400 | 6 (LEFT_TRIGGER) | Trigger L |
| R2 | 0x800 | 7 (RIGHT_TRIGGER) | Trigger R |
| **Options** | **0x80000** | **9 (START)** | **Pause Menu** |
| **Touchpad** | **0x4000000** | **8 (BACK)** | **Mod Menu** |
| **L3 / R3** | **0x100000 / 0x200000** | **7/8 (LEFTSTICK/RIGHTSTICK) + F1; joystick fallback 11/12** | **Cheat Menu** |
| D-Pad | 0x1000–0x8000 | 14–17 | Quick Items |

**Implementasi:** `hook_SDL_GameControllerGetButton` di `gen_loader.js` mengecek hardware mask `ScePadData.buttons` dan mengembalikan enum SDL yang sesuai. `hook_SDL_PollEvent` menyintesis F1 hanya untuk L3/R3 dari event controller 7/8 atau joystick 11/12. Circle tetap B tanpa Escape/F1 dan tombol lain tetap menggunakan fungsi PlayStation/game yang sudah ada.

### 3.3 Audio: Fix Crackling/Stutter
**Masalah:** PE request 44.1kHz/2048 samples → buffer underrun di PS4.

**Solusi (`hook_SDL_OpenAudioDevice`):**
```c
desired->freq = 48000;           // PS4 native rate
desired->channels = 2;
desired->samples = 1024;         // low latency
desired->format = AUDIO_S16SYS;
SDL_PauseAudioDevice(dev, 0);    // auto-unpause
```

### 3.4 Renderer Software OpenOrbis: Profil Ringan
**Masalah:** Backend renderer OpenOrbis yang tersedia pada build ini hanya renderer software. Render internal 2556×1440 dengan bilinear scaling menyebabkan beban CPU sangat tinggi dan terukur sekitar 2–5 FPS.

**Mitigasi aktif (`hook_SDL_CreateRenderer` dan migrasi settings):**
```c
SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
// /data/HarvestMoonBTN/settings.ini dimigrasikan satu kali:
// Profil v2: resolution=1, smooth=0; opsi lain dan mapping tombol dipertahankan.
```

### 3.5 Frame Pacing dan Pengukuran FPS
**Masalah:** Backend video sudah menunggu flip/vblank. Selain itu, PE memanggil `SDL_Delay(16)` tepat setelah present pada RVA `0x8660`, sehingga frame menunggu lagi.

**Solusi (`hook_SDL_RenderPresent`):**
```c
SDL_RenderPresent(renderer);
// Tidak ada SDL_Delay tambahan. FPS terukur dicatat setiap 5 detik.
// Call pasca-present di RVA 0x8660 di-NOP hanya jika signature 5 byte cocok.
```

**Status:** build dan compile terverifikasi; peningkatan FPS nyata belum boleh diklaim sebelum membaca sampel FPS dari `loader.log` di PS4.

Hot path `SDL_render_sw.c` dan `SDL_ps4video.c` dibangun ulang dengan `-O3` ke `build_ps4/libSDL2_optimized.a`. Backend video tidak lagi mengosongkan seluruh framebuffer 1080p sebelum surface fullscreen menimpa area yang sama.

### 3.6 SDL_Delay Clamp
**Masalah:** Game panggil `SDL_Delay(100+)` → kill FPS.

**Solusi (`hook_SDL_Delay`):**
```c
if (ms > 16) ms = 16;
SDL_Delay(ms);
```

### 3.7 Window Flags: Hapus `SDL_WINDOW_ALLOW_HIGHDPI`
**Masalah:** Flag ini minta 4K backbuffer (2556×1440) → allokasi tekstur 16 MB → `os_mmap failed` → `abort()` → **CE-34878-0 crash**.

**Fix:** Hanya `SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN` (1080p native).

### 3.8 Pilihan Bahasa dari Cheat Menu
Patch Inggris dan Indonesia tersimpan di bundle `HMBTNHPS` di dalam `HarvestMoonBTN.exe`; patch bukan file eksternal yang hilang.

Loader membaca `id` atau `en` dari `/data/HarvestMoonBTN/data/lang.txt`, menerima path lama `/data/HarvestMoonBTN/lang.txt` untuk migrasi, lalu memasok nilai yang sama melalui hook `HMBTN_LANG`, argumen `--lang`, dan `__getmainargs`. Perubahan bahasa dari menu berlaku setelah game dimulai ulang.

**Status:** pilihan bahasa host/engine sudah aktif dan persisten. Konsistensi seluruh dialog/aset masih perlu validasi di PS4 karena paket saat ini membawa disc USA mentah.

### 3.9 Save Data Redirect
**Loader:** Hook `fopen`/`fwrite`/`fclose` redirect path `C:\...` → `/data/HarvestMoonBTN/`.

---

## 4. File Struktur Penting

```
E:\PS4\hmbtn\
├── build_ps4/
│   └── gen_loader.js          # Generator loader C (UTAMA)
├── src/
│   └── ps4_loader.c           # Generated (106 KB)
├── pkg_staging/
│   ├── HarvestMoonBTN.exe     # PE Windows (19.9 MB)
│   ├── settings.ini           # Config game
│   └── disc.cue               # CUE sheet
├── HarvestMoonBTN-ID-Windows/ # Source versi Indonesia (Windows)
│   ├── HarvestMoonBTN.exe
│   └── settings.ini (Language=id)
├── releases/
│   └── HarvestMoonBTN-PS4-Native.pkg  # Output PKG (250 MB)
├── update_rebuild.bat         # Build script otomatis
└── docs/
    ├── Dokumentasi-Perbaikan-PS4-Native.md
    └── Panduan-Porting-PS4-Native.md  # (file ini)
```

---

## 5. Build & Deploy Workflow

```bash
# 1. Regenerate loader (jika gen_loader.js diubah)
cd E:\PS4\hmbtn
node build_ps4/gen_loader.js

# 2. Full rebuild PKG
cmd /c update_rebuild.bat

# 3. Upload ke PS4 via FTP (10.0.0.90:2121)
curl.exe --fail --show-error --disable-epsv --ftp-create-dirs \
  --upload-file "releases/HarvestMoonBTN-PS4-Native.pkg" \
  "ftp://10.0.0.90:2121/data/pkg/HarvestMoonBTN-PS4-Native.pkg"

# Verifikasi nama dan ukuran remote
curl.exe --fail --show-error --disable-epsv \
  "ftp://10.0.0.90:2121/data/pkg/"

# 4. Di PS4: GoldHEN Package Installer → Install PKG
# 5. Jalankan game, cek loader.log di /data/HarvestMoonBTN/loader.log
```

---

## 6. Debugging & Logging

**Loader Log (`/data/HarvestMoonBTN/loader.log`):**
```
[HMBTN PS4] Mapped game image at: 0x140000000
[HMBTN PS4] Resolving imports from RVA 0x182A000...
[HMBTN PS4] Binding DLL: SDL2.dll
[HMBTN PS4] Binding DLL: msvcrt.dll
[HMBTN PS4] Binding DLL: kernel32.dll
[HMBTN PS4] In-memory patch: welcome overlay @ 0x4420 -> RET
[HMBTN PS4] In-memory patch: welcome sub @ 0x20F0 -> RET
[HMBTN PS4] Video init: 1920x1080 @60Hz, flip queue ready
[HMBTN PS4] Audio init: 48000Hz stereo 1024 samples
[HMBTN PS4] sceSystemServiceHideSplashScreen on first present -> 0x00000000
[HMBTN PS4] SDL_RenderPresent #1
...
```

**Key Checks:**
- `Mapped game image at: 0x140000000` → MAP_FIXED berhasil
- `sceSystemServiceHideSplashScreen -> 0x00000000` → splash OS tersembunyi
- `Audio init: 48000Hz stereo 1024 samples` → hook audio aktif
- `SDL_RenderPresent` count meningkat → main loop jalan

---

## 7. Known Issues & TODO

| Issue | Status | Solusi Direncanakan |
|-------|--------|---------------------|
| **Bahasa gameplay** | ⚠️ Perlu validasi perangkat | Uji beberapa dialog setelah memilih `id`; paket masih memakai disc USA mentah |
| **Cheat Menu Language Toggle** | ✅ Aktif/persisten | Pilih `id/en`, restart, lalu cek log `Language preference from cheat menu` |
| **FPS software renderer** | ⚠️ Perlu pengukuran PS4 | Cek FPS dan fase `update/copy/present/other` di `loader.log`; jangan mengasumsikan 30/60 FPS |
| **Save Data** | ✅ Redirect jalan | Verifikasi persistensi cross-session |
| **Fullscreen Toggle** | ✅ Hook no-op | Game sudah fullscreen native |
| **Controller Disconnect** | ❓ Belum test | Handle hotplug di hook Pad |

---

## 8. Referensi Teknis

### Konstanta OpenOrbis
```c
#define ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE 0x80000010
#define ORBIS_PAD_BUTTON_CROSS       0x8000
#define ORBIS_PAD_BUTTON_CIRCLE      0x10000
#define ORBIS_PAD_BUTTON_SQUARE      0x20000
#define ORBIS_PAD_BUTTON_TRIANGLE    0x40000
#define ORBIS_PAD_BUTTON_L1          0x100
#define ORBIS_PAD_BUTTON_R1          0x200
#define ORBIS_PAD_BUTTON_L2          0x400
#define ORBIS_PAD_BUTTON_R2          0x800
#define ORBIS_PAD_BUTTON_OPTIONS     0x80000
#define ORBIS_PAD_BUTTON_TOUCH_PAD   0x4000000
#define ORBIS_PAD_BUTTON_L3          0x100000
#define ORBIS_PAD_BUTTON_R3          0x200000
#define ORBIS_PAD_BUTTON_DPAD_UP     0x1000
#define ORBIS_PAD_BUTTON_DPAD_DOWN   0x2000
#define ORBIS_PAD_BUTTON_DPAD_LEFT   0x4000
#define ORBIS_PAD_BUTTON_DPAD_RIGHT  0x8000
```

### SDL_GameControllerButton Enum (Subset)
```c
SDL_CONTROLLER_BUTTON_A           = 0  // Cross
SDL_CONTROLLER_BUTTON_B           = 1  // Circle
SDL_CONTROLLER_BUTTON_X           = 2  // Square
SDL_CONTROLLER_BUTTON_Y           = 3  // Triangle
SDL_CONTROLLER_BUTTON_BACK        = 4  // Touchpad → Mod Menu
SDL_CONTROLLER_BUTTON_GUIDE       = 5
SDL_CONTROLLER_BUTTON_START       = 6  // Options → Pause Menu
SDL_CONTROLLER_BUTTON_LEFTSTICK   = 7  // L3 + synthetic F1
SDL_CONTROLLER_BUTTON_RIGHTSTICK  = 8  // R3 + synthetic F1
SDL_CONTROLLER_BUTTON_LEFT_SHOULDER  = 4
SDL_CONTROLLER_BUTTON_RIGHT_SHOULDER = 5
SDL_CONTROLLER_BUTTON_LEFT_TRIGGER   = 6
SDL_CONTROLLER_BUTTON_RIGHT_TRIGGER  = 7
SDL_CONTROLLER_BUTTON_DPAD_UP        = 14
SDL_CONTROLLER_BUTTON_DPAD_DOWN      = 15
SDL_CONTROLLER_BUTTON_DPAD_LEFT      = 16
SDL_CONTROLLER_BUTTON_DPAD_RIGHT     = 17
```

### PE Offsets Kunci (VA = ImageBase + RVA)
| Fungsi / Data | VA | RVA | Catatan |
|---------------|-----|-----|---------|
| `SDL_main` | `0x14000A050` | `0xA050` | Entry point game |
| Welcome overlay entry | `0x140004420` | `0x4420` | Patch RET |
| Welcome sub-rutin | `0x1400020F0` | `0x20F0` | Patch RET |
| Button hook | `0x1400068C0` | `0x68C0` | `SDL_GameControllerGetButton` |
| SDL Button ID Array | `0x1412941A0` | `.data` | 14 entry, mapping salah |
| PS1 Button Masks | `0x1402DAE80` | `.rdata` | 14 entry |
| `getenv("HMBTN_LANG")` | `0x14000A17C` | `0xA17C` | Language detect |
| `patch_%s` format | `0x1402D6E98` | `.rdata` | `patch_id.hmp` |
| Cheat menu handler | `0x1400077EC` | `0x77EC` | 7 opsi |
| Language change fn | `0x1402CCFE0` | — | Set flag + restart msg |
| Indonesian flag | `0x1402D448F` | `.data` | `0xFF` = enabled |
| `SDL_OpenAudioDevice` call | `0x14000A242` | `0xA242` | Audio init site |

---

## 9. Lessons Learned

1. **MAP_FIXED di `0x140000000`** — kritis untuk PE Windows yang pakai absolute address. Fallback dynamic mapping + relocations wajib.
2. **SDL2 PS4 backend beda dari PC** — backend renderer build ini software; jangan dokumentasikan akselerasi GNM tanpa bukti runtime.
3. **Present PS4 sudah menunggu flip** — hindari `SDL_Delay` kedua di `SDL_RenderPresent`; ukur FPS dari log.
4. **HighDPI flag = mati** — PS4 output 1080p fixed. Jangan minta 4K backbuffer.
5. **Audio spec harus native** — 48kHz/1024 samples. Rate conversion di SDL2 PS4 mahal.
6. **In-memory patch > binary patch** — Tidak perlu modify `HarvestMoonBTN.exe` di disk. Loader patch saat runtime.
7. **Patch bahasa tertanam di EXE** — pilihan menu disimpan sebagai `id/en` di `data/lang.txt` dan diterapkan saat restart.
8. **Crash CE-34878-0** — Sering karena OOM (texture terlalu besar) atau div-by-zero di timing. Log `loader.log` adalah diagnostic utama.

---

## 10. Checklist Release

- [x] Splash screen OS + PE tersembunyi
- [x] Button mapping DS4 final (Circle tidak mengirim Escape; L3/R3 mengirim F1 untuk Cheat)
- [x] Audio 48kHz/1024 stereo, no crackling
- [x] Profil v2 software-renderer `resolution=1`, `smooth=0`, filter `nearest`, integer scaling
- [x] Delay manual di `SDL_RenderPresent` dihapus; logging FPS 5 detik aktif
- [x] SDL_Delay clamp 16ms
- [x] Window 1080p native (no HighDPI)
- [x] Save redirect `/data/HarvestMoonBTN/`
- [x] Bahasa `id/en` persisten melalui `data/lang.txt`, `HMBTN_LANG`, dan `--lang`
- [ ] Validasi FPS aktual, Circle/L3/R3, restart bahasa, serta konsistensi dialog di PS4
- [ ] Test persistensi save data
- [ ] Test controller hotplug
- [ ] Test FW 5.05, 6.72, 9.00, 11.00
- [ ] Update `docs/Dokumentasi-Perbaikan-PS4-Native.md`

---

*Dokumen ini di-generate dari sesi porting real-time. Update saat ada perubahan baru.*
