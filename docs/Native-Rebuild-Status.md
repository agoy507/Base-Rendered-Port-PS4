# Status Rebuild Native PS4

## Status 17 November 2026

Metode aktif adalah rebuild native GPU. Paket saat ini merupakan benchmark engine, bukan game lengkap.

Sumber konten Penyu `1.4.0` sudah tersedia dan tervalidasi. ZIP ber-SHA-256 `9F1EB087886BA151642252DE0C01B4E86644EDCEB1C689DB542F2F4CADF8A73F`; executable ber-SHA-256 `24FEEB3FE43FC1039276648D1D86246A00FD48C27F351E0A04215516285358A8` dan memuat marker internal `V1.4.0`. Provenance lengkap berada di `assets/import_sources/penyu_v1.4.0/SOURCE.md`.

| Milestone | Status | Blocker atau aksi berikutnya |
|---|---|---|
| Core portable | Playable Farm lulus test | Tambah dialog, i18n, dan state dunia |
| Backend V2 upstream | Playable Farm 1.06 lulus pada PS4 | Ganti placeholder dengan data hasil importer Penyu 1.4.0 |
| GPU probe C lama | Diarsipkan | Clear-only tetap hitam; bukan basis produksi |
| 1080p 16:9 render target | Dibangun | Grid probe 1920×960; layout gameplay belum ada |
| Telemetri p95/p99 | Lulus pada asset 1.02 | Pertahankan gate pada setiap tahap beban |
| Pacing 59,94/60 Hz | Belum dibuktikan | Probe aktif meminta swap interval 0 |
| Asset importer | Belum dimulai | Tunggu gate GPU |
| Vertical slice | Belum dimulai | Tunggu importer dan gate GPU |
| Full gameplay | Belum dimulai | Migrasi bertahap setelah vertical slice |

## Build aktif terbaru

- File: `releases/HarvestMoonBTN-Native-V2-Playable-Farm.pkg`
- Title ID: `HMBT00002`
- Content ID: `UP9000-HMBT00002_00-HMBTNNATIVEV2000`
- Versi: `1.03`
- Ukuran: `6.619.136` byte
- SHA-256 lokal: `DA49B4DBE8D214F8E3AB5A62B77ABB49367DC9BFB5C9692832C8906DF4102F1C`
- Target FTP: `/data/pkg/HarvestMoonBTN-Native-V2-Playable-Farm.pkg`
- Upload remote: berhasil ke `/data/pkg/HarvestMoonBTN-Native-V2-Playable-Farm.pkg`; ukuran dan SHA-256 remote identik dengan paket lokal
- Gate lokal: PASS; core, contract, compile, link, FSELF, SFO, dan package
- Gate perangkat: menunggu uji movement, collision, camera, kestabilan, dan telemetry
- Kontrak source upstream: PASS
- Telemetry contract: PASS
- Compile, link, FSELF, SFO, package: PASS
- Provenance/lisensi: OpenOrbis GPL-3.0 dicatat di `native/piglet_upstream/PROVENANCE.md`

## Insiden CE-34878

Build `01.01` membuktikan `gd` berhenti di `eglGetDisplay`. Build `01.02` dengan kategori Piglet `gde` mencapai EGL 1.4, context, shader, atlas, batch sprite, dan loop render. Telemetri sekitar 120 FPS pada 1080p dengan 1% low umumnya 58,6 FPS; CE-34878 terselesaikan. Output pengguna masih blank, sehingga build `01.03` mencatat hasil SystemService/hide-splash, mengulang hide setelah swap pertama, dan menampilkan clear biru terang.

Log `01.03` mencatat SystemService dan kedua hide-splash sukses (`0`), swap terus berjalan, tetapi layar tetap hitam. Paket pembanding upstream OpenOrbis tanpa perubahan telah dibangun dan diunggah sebagai `/data/pkg/OpenOrbis-Piglet-Upstream.pkg` (Title ID `BREW00093`, SHA-256 `4B583BB29B4A1D226C810CEDFD77BBE46E910F1863CEB0F44C94EB9E1A31A169`). Hasil visual paket ini menentukan apakah blocker berada pada proyek atau lingkungan compositor PS4.

Sampel upstream berhasil menampilkan kucing berputar, sehingga blocker terlokalisasi ke probe. Build `01.04` melewati seluruh shader/texture/batch draw pada frame dan hanya melakukan clear biru + swap. Jika biru terlihat, giant client-side vertex batch adalah kandidat utama dan backend produksi akan memakai scaffold upstream serta VBO bertahap.

Build `01.04` tetap hitam. Karena itu backend C lama dihentikan. Backend V2 kini mempertahankan renderer dan pipeline build upstream yang sudah terbukti terlihat, tetapi memakai identitas aplikasi baru `HMBT00002`. Migrasi berikutnya dilakukan satu variabel per build: telemetry, satu checker quad, atlas, lalu VBO 1 → 16 → 256 → 2.048 sprite.

Baseline V2 `1.00` berhasil menampilkan kucing berputar. Update `1.01` menambahkan telemetry 300-frame melalui `HmbtnTelemetry.cpp` dan integrasi minimal di `main.cpp`; `PigletApplication.cpp` tetap hash-identik. Log berada di `/data/HarvestMoonBTNNativeV2/perf.log`. Flush dilakukan setiap 10 window dan overhead agregasi tetap masuk ke interval berikutnya agar hitch tidak disembunyikan.

Runtime `1.01` menghasilkan 20 window telemetry yang stabil: sekitar 119,9 FPS, p95/p99 sekitar 17 ms, 1% low sekitar 58,7 FPS, dan nol frame di atas 20 ms. Stage `1.02` mengganti hanya PNG kucing dengan artwork proyek ber-hash `72dd1a89...665d3b`; renderer upstream tetap `b48489ae...cb6f87`. Uji perangkat `1.02` mengonfirmasi artwork Harvest Moon terlihat dan berputar. Dua puluh window log tetap stabil sekitar 119,9 FPS, p95/p99 sekitar 17 ms, 1% low mayoritas 58,6–58,8 FPS, serta tidak ada frame di atas 20 ms.

Logging sekarang memiliki checkpoint:

```text
[NATIVE INIT] modules ready
[NATIVE INIT] Piglet configuration accepted
[NATIVE INIT] EGL display ready
[NATIVE INIT] EGL initialized: <major>.<minor>
[NATIVE INIT] OpenGL ES API bound
[NATIVE INIT] EGL config ready: configs=<count>
[NATIVE INIT] context ready
[NATIVE INIT] shader program ready
[NATIVE INIT] atlas ready
[NATIVE INIT] sprite batch ready
```

Jika crash berulang, baris terakhir di `/data/HarvestMoonBTNNative/perf.log` menjadi bukti lokasi kegagalan.

## Aksi berikutnya

1. bekukan `1.06` sebagai baseline gameplay PS4 yang lulus untuk input, fixed-step, camera, collision, render, dan swap;
2. bangun importer offline dari disc asli + patch Penyu `1.4.0`;
3. ganti geometri kebun placeholder dengan tile/atlas nyata tanpa mengubah bootstrap Piglet dan jalur input yang tervalidasi;
4. ukur setiap kenaikan konten dengan gate p99 di bawah 20 ms, 1% low minimal 50 FPS, dan nol crash.
