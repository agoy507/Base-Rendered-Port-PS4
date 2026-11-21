# Harvest Moon: Back to Nature — Rebuild Native PS4

> **Source-only public base:** this repository intentionally excludes game
> discs, extracted files, original executables, artwork, generated packages,
> and local SDK binaries. Use only content you are entitled to use when
> adapting the renderer for another project.

> **Status:** engine baru dibangun dari nol; paket loader/emulator lama dipertahankan hanya sebagai referensi  
> **Target:** Sony PlayStation 4 (AMD Jaguar x86-64 Native)  
> **Display:** render target 1920×1080 FullHD 16:9; layout gameplay belum diimplementasikan  
> **Performa:** 60 FPS target; 50 FPS minimum pada scene berat  
> **Controls:** DualShock 4 Controller (DS4 Native)  
> **Renderer:** Piglet/EGL/OpenGL ES 2.0 GPU; software renderer tidak diterima sebagai fallback produksi

---

## ⚡ Panduan Cepat (Quick Start)

| Kebutuhan | Perintah / File | Keterangan |
| :--- | :--- | :--- |
| **Build backend V2** | `build_native_ps4_v2.bat` | Menjalankan kontrak upstream, build/link/package Piglet yang telah terbukti terlihat |
| **Build probe lama** | `build_native_ps4.bat` | Diagnostik backend C lama; tidak lagi menjadi basis produksi |
| **Jalankan unit test** | `powershell -File tests/run_native_tests.ps1` | Menguji core native: frame budget, fixed-step, 16:9, input, dan piksel |
| **Jalankan gate PS4** | `node tests/native_ps4_probe.test.js` | Memastikan backend GPU 1080p tidak kembali ke SDL software |
| **Bersihkan Cache Build** | `clean.bat` | Menghapus file intermediate `*.o`, `*.elf`, `*.tmp`, dan `*.bak` dengan aman |
| **Paket V2 asset** | `releases/HarvestMoonBTN-Native-V2-Asset.pkg` | Renderer tervalidasi + artwork proyek + telemetry |

---

## Status Rebuild Aktif — 18 November 2026

- Arah loader PE/emulator lama dihentikan karena log PS4 membuktikan beban game/emulasi `90–150 ms` per frame dan present software `22–38 ms`.
- Backend produksi aktif dipindahkan ke scaffold Piglet upstream setelah probe C `01.04` tetap hitam meski clear-only dan swap berhasil.
- Kandidat aktif V2 Playable Farm `1.03`: `releases/HarvestMoonBTN-Native-V2-Playable-Farm.pkg` (`6.619.136` byte), Title ID `HMBT00002`, SHA-256 `DA49B4DBE8D214F8E3AB5A62B77ABB49367DC9BFB5C9692832C8906DF4102F1C`.
- Paket 1.03 sudah diunggah dan diverifikasi byte-identik di `/data/pkg/HarvestMoonBTN-Native-V2-Playable-Farm.pkg`.
- Paket V2 Asset `1.02` dengan SHA-256 `25E444030518985DD239B4B2F81467EE3DA4C1700D92503CB30031F5F3DE03D1` dipertahankan sebagai rollback visual yang tervalidasi.
- Paket V2 Asset `1.02` sudah diunggah ke `ftp://10.0.0.90:2121/data/pkg/HarvestMoonBTN-Native-V2-Asset.pkg`; ukuran dan SHA-256 remote identik dengan paket lokal.
- Diagnostik shader memeriksa `GL_LINK_STATUS` dan menyimpan info log; error atlas/render dicatat sebelum status GL/EGL dikonsumsi.
- Versi `01.02` membuktikan kategori SFO `gde` menyelesaikan CE-34878 dan menjalankan probe sekitar 120 FPS pada 1080p; 1% low umumnya sekitar 58,6 FPS. Masalah tersisa adalah output visual blank.
- Versi `01.03` menambahkan diagnostik SystemService/hide-splash, hide ulang setelah swap pertama, dan clear biru terang untuk mengisolasi masalah tampilan.
- Sampel Piglet upstream resmi menampilkan kucing berputar pada perangkat yang sama; lingkungan PS4 terbukti sehat. Versi `01.04` hanya melakukan clear biru dan swap, tanpa menjalankan shader, texture, atau batch per frame.
- Probe membuat context Piglet 1920×1080, memakai atlas RGBA 1024×1024, dan mengirim 2.048 sprite ber-alpha dalam satu draw call. Frame pacing diukur dari interval antar-frame; hasil average/p95/p99/1% low ditulis setiap 300 frame ke `/data/HarvestMoonBTNNative/perf.log`.
- Render target probe adalah 1920×1080 tanpa penurunan resolusi; grid diagnostik saat ini mengisi area 1920×960.
- Milestone probe memakai chord L3+R3 untuk men-toggle indikator warna/opasitas. Circle Cancel/Back dan cheat menu nyata adalah kontrak vertical slice, belum fitur gameplay probe.
- Source renderer baseline dipertahankan byte-identik dengan sampel yang sudah menampilkan kucing berputar pada PS4; metadata proyek memakai identitas baru agar tidak tercampur instalasi lama.
- Telemetry berada di file terpisah dan menulis `/data/HarvestMoonBTNNativeV2/perf.log`; renderer upstream tetap byte-identik.
- Stage `1.01` terukur stabil sekitar 119,9 FPS, p95/p99 sekitar 17 ms, 1% low sekitar 58,7 FPS, tanpa frame di atas 20 ms.
- Stage `1.02` berhasil menampilkan artwork Harvest Moon berputar pada PS4. Log perangkat stabil sekitar 119,9 FPS, p95/p99 sekitar 17 ms, 1% low mayoritas 58,6–58,8 FPS, dan nol frame di atas 20 ms.
- Stage `1.03` memulai game: fixed-step 60 Hz presisi, input D-pad/stick DS4, movement, collision farmhouse/world bounds, kamera 16:9, dan scene kebun placeholder 1920×1080.
- Uji perangkat `1.03` mengalami `CE-34878-0` sebelum window telemetry pertama. Build diagnostik `1.04` menambahkan checkpoint langsung-flush di sekitar App init, Pad init, Logic, scene draw, dan swap; paket lokal `releases/HarvestMoonBTN-Native-V2-Crash-Diagnostic.pkg` ber-SHA-256 `4589E64461C4BD4EE7C96FB3FEF359107DDAEC87AE47321BC5FB0F0EA00917EA`.
- Log `1.04` berhenti tepat pada `logic-before-pad-init`, sehingga EGL/scene/swap tersingkir sebagai penyebab. Build `1.05` memperbaiki urutan Pad/UserService sesuai sampel OpenOrbis, memakai parameter priority valid, dan sudah diunggah sebagai `/data/pkg/HarvestMoonBTN-Native-V2-Input-Fix.pkg` dengan SHA-256 `BB567A713CDD66C0BC75FBC75CFEBB9739B74EADEB717C0EFEB02599E6AF066F`.
- Build `1.06` memuat sysmodule UserService dan Pad secara eksplisit sebelum inisialisasi. Uji PS4 lulus: scene kebun placeholder tampil, pemain dapat digerakkan, collision tembok benar, dan tidak ada crash. Telemetry stabil sekitar 119,9 FPS submission, p99 sekitar 17 ms, 1% low 58,7–58,8 FPS, serta `over20ms=0`.
- Sumber konten Penyu `1.4.0` tervalidasi dari marker internal dan dicatat di `assets/import_sources/penyu_v1.4.0/SOURCE.md`; importer offline akan menggabungkannya dengan disc asli tanpa menjalankan SDL/PE pada PS4.
- Paket ini bukan game lengkap. Rekonstruksi aset, map, script, audio, save, bahasa Indonesia/Inggris, dan gameplay dilakukan bertahap setelah gerbang GPU lulus.

---

## 📁 Struktur Direktori Proyek

```text
E:\PS4\hmbtn\
├── archive/                     # Arsip versi lama (exe_v1.3.0, dekompilasi Ghidra & WASM)
├── assets/                      # Artwork + original_disc/ (disc ASLI SLUS-01115 untuk MOD V1.3.5+)
├── build_ps4/                   # Skrip analisis PE, generator loader, config GP4/SFO, & updater
├── build_native_ps4/            # Object, ELF, OELF, dan eboot engine native baru
├── docs/                        # Dokumentasi teknis lengkap, bug report, & catatan porting
├── HarvestMoonBTN-ID-Windows/   # Sumber biner game Windows PC (HarvestMoonBTN.exe)
├── pkg_staging/                 # Direktori staging perakitan paket fPKG
├── native/                      # Core portable, platform PS4 Piglet, dan metadata paket baru
│   └── piglet_upstream/         # Backend V2 kanonik + provenance dan lisensi GPL-3.0
├── releases/                    # Output paket instalasi PlayStation 4 (*.pkg)
├── src/                         # Loader lama; hanya referensi selama rebuild native
├── .gitignore                   # Konfigurasi filter berkas Git
├── check_update.bat             # Skrip cek & update otomatis dari server Si Penyu
├── clean.bat                    # Skrip pembersih cache build 1-klik
├── build_native_ps4.bat         # Build/test/package master untuk engine baru
├── build_native_ps4_v2.bat      # Build baseline backend V2 berbasis scaffold upstream
├── update_rebuild.bat           # Build loader lama; tidak lagi menjadi jalur produksi
└── README.md                    # Dokumentasi utama proyek
```

---

## Alur Milestone Native

1. Build dan jalankan GPU probe 1080p pada Base PS4.
2. Probe aktif memakai `eglSwapInterval(0)` dan mengukur throughput GPU uncapped/default compositor. Terima gate throughput bila `fps >= 59`, `p95 <= 16,67 ms`, `p99 <= 20 ms`, dan 1% low minimal 50 FPS.
3. Validasi pacing 59,94/60 Hz menjadi gate terpisah setelah sinkronisasi vblank eksplisit tersedia.
4. Bangun importer aset offline yang tervalidasi dari disc milik pengguna, tanpa menjalankan kode MIPS saat runtime.
5. Implementasikan vertical slice: satu area kebun, karakter, kamera, collision, satu NPC, dialog, crop, audio, save, lokalisasi, dan cheat menu.
6. Migrasikan seluruh sistem serta konten memakai replay deterministik dan golden screenshot.

---

## 📚 Dokumentasi Lanjutan
Dokumentasi teknis lengkap tersedia di folder [`docs/`](docs/):
- [`docs/Native-Rebuild-1080p60.md`](docs/Native-Rebuild-1080p60.md) — Keputusan arsitektur, kontrak, anggaran frame, dan roadmap.
- [`docs/Native-GPU-Probe-Runbook.md`](docs/Native-GPU-Probe-Runbook.md) — Build, instalasi, pengujian, pengambilan log, dan troubleshooting CE-34878.
- [`docs/Native-Rebuild-Status.md`](docs/Native-Rebuild-Status.md) — Artefak terbaru, bukti build, blocker, dan tindakan berikutnya.
- [`docs/Panduan-Porting-PS4-Native.md`](docs/Panduan-Porting-PS4-Native.md) — Referensi historis jalur PE loader; bukan runtime produksi aktif.
- [`docs/Dokumentasi-Perbaikan-PS4-Native.md`](docs/Dokumentasi-Perbaikan-PS4-Native.md) — Riwayat serta koreksi perbaikan terbaru.
- [`docs/HarvestMoonBTN-PS4-Port-Dokumentasi.md`](docs/HarvestMoonBTN-PS4-Port-Dokumentasi.md) — Riwayat lengkap porting & arsitektur PE loader.
- [`docs/HarvestMoonBTN-PS4-Rebuild.md`](docs/HarvestMoonBTN-PS4-Rebuild.md) — Detail spesifikasi build, konfigurasi 1080p, dan checklist rilis.
- [`docs/BugReport-ps4_loader.md`](docs/BugReport-ps4_loader.md) — Analisis kontroler DualShock 4 dan status bug calling convention.
- [`docs/BACA-README.txt`](docs/BACA-README.txt) — Catatan rilis asli Mod Penyu v1.3.0.
