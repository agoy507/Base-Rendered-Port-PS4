# Runbook GPU Probe Native PS4

## Tujuan

Runbook ini menjelaskan cara membangun, memasang, menjalankan, dan mengevaluasi benchmark GPU native 1920×1080. Paket ini bukan game lengkap.

## Artefak aktif

- Paket: `HarvestMoonBTN-Native-GPU-Probe.pkg`
- Title ID: `HMBT00001`
- Content ID: `UP9000-HMBT00001_00-HMBTNNATIVEGPU01`
- Ukuran: `12.386.304` byte
- Versi: `01.04` clear-only
- SHA-256: `15E7D4C897F7D18BBA2250D27A427FAA7AC6A76820028F1D9F026CEDD01443B9`
- Lokasi FTP: `/data/pkg/HarvestMoonBTN-Native-GPU-Probe.pkg`
- Log: `/data/HarvestMoonBTNNative/perf.log`

Status distribusi 17 November 2026: paket diagnostik `01.04` telah diunggah ke FTP. Unduhan balik menghasilkan SHA-256 yang sama dengan lokal, `15E7D4C897F7D18BBA2250D27A427FAA7AC6A76820028F1D9F026CEDD01443B9`, dan ukuran `12.386.304` byte. Build ini hanya menjalankan clear biru dan swap pada frame; shader, texture, blend, dan batch draw dilewati. Marker pertama harus `[NATIVE BUILD] GPU probe 01.04 clear-only diagnostics`.

Paket pembanding `OpenOrbis-Piglet-Upstream.pkg` juga tersedia di FTP. Paket ini dibangun dari sumber sampel Piglet OpenOrbis yang tidak dimodifikasi, memakai Title ID `BREW00093`, berukuran `6.619.136` byte, dan SHA-256 `4B583BB29B4A1D226C810CEDFD77BBE46E910F1863CEB0F44C94EB9E1A31A169`. Gunakan paket ini hanya untuk membedakan masalah proyek dari masalah compositor/firmware/GoldHEN.

Backend aktif telah berpindah ke `HMBT00002`. Paket telemetry `1.01` berada di `/data/pkg/HarvestMoonBTN-Native-V2-Telemetry.pkg` dengan SHA-256 `B1A53D3F94DCCEAAF2C3B8EF6E9AFD151ACE96340C1E8CB7747ED06089B1CEF8`. Log V2 berada di `/data/HarvestMoonBTNNativeV2/perf.log`.

Hash dapat berubah setiap rebuild karena isi/timestamp packaging. Hitung hash sebelum distribusi:

```powershell
Get-FileHash releases\HarvestMoonBTN-Native-GPU-Probe.pkg -Algorithm SHA256
```

## Prasyarat

- PS4 dengan GoldHEN dan Package Installer;
- FTP PS4 pada `10.0.0.90:2121`;
- OpenOrbis pada `E:\PS4\OpenOrbis\OpenOrbis\PS4Toolchain`;
- LLVM pada `C:\Program Files\LLVM\bin`;
- Fake PKG Tools, Node.js, PowerShell, dan .NET runtime;
- `libc.prx`, `libSceFios2.prx`, dan Piglet `right.sprx` tersedia.

## Build

```powershell
cmd /c build_native_ps4.bat
```

Build menjalankan unit test dan architecture gate sebelum compile, link, FSELF, serta packaging.

## Upload

```powershell
curl.exe --fail --show-error --ftp-create-dirs `
  -T "E:\PS4\hmbtn\releases\HarvestMoonBTN-Native-GPU-Probe.pkg" `
  "ftp://10.0.0.90:2121/data/pkg/HarvestMoonBTN-Native-GPU-Probe.pkg"
```

Verifikasi ukuran remote:

```powershell
curl.exe --fail --show-error "ftp://10.0.0.90:2121/data/pkg/"
```

## Pengujian

1. Instal paket melalui Package Installer.
2. Jalankan aplikasi.
3. Tampilan yang diharapkan adalah grid sprite transparan pada render target 1920×1080.
4. Tekan L3 saja dan R3 saja; indikator tidak boleh berubah.
5. Tekan L3+R3; warna/opasitas diagnostik berubah satu kali.
6. Jalankan minimal 10 menit setelah warm-up dua menit.
7. Probe tidak mempunyai tombol exit; tutup melalui tombol PS/Home.
8. Ambil log melalui FTP.

Log bersifat append. Catat waktu pengujian agar sampel build lama tidak tercampur saat dianalisis.

Sampel upstream telah menampilkan tekstur kucing berputar pada perangkat ini, sehingga firmware/GoldHEN/compositor terbukti berfungsi. Jalankan build `01.04`: biru berarti jalur batch draw lama merusak output; tetap hitam berarti backend produksi harus dipindahkan ke scaffold/build upstream yang sudah terbukti.

## Mengambil log

```powershell
curl.exe --fail --show-error `
  -o "E:\PS4\hmbtn\ps4_native_perf.log" `
  "ftp://10.0.0.90:2121/data/HarvestMoonBTNNative/perf.log"
```

## Keputusan pass atau fail

Gate throughput lulus bila:

- boot tanpa CE-34878;
- log mencapai `sprite batch ready` dan mencetak baris `[NATIVE PERF]`;
- `fps >= 59`;
- `p95 <= 16667 us`;
- `p99 <= 20000 us`;
- `1%low >= 50`;
- visual 16:9, warna, alpha, dan input benar.

Probe memakai `eglSwapInterval(0)`. Hasil ini mengukur throughput uncapped atau perilaku default compositor, bukan pacing vblank eksplisit. Gate paced 60 Hz diuji terpisah setelah sinkronisasi vblank tersedia.

Gagal bila salah satu syarat throughput tersebut tidak terpenuhi. Simpan log gagal; jangan menurunkan resolusi sebagai perbaikan pertama.

## Troubleshooting CE-34878

Insiden pertama pada 16 November 2026 menghasilkan:

```text
[NATIVE] GPU probe initialization failed, EGL=0x00003000 GL=0x00000000
```

`0x3000` adalah `EGL_SUCCESS`. Koreksi pertama mengubah swap interval menjadi nonfatal, tetapi perangkat tetap menampilkan CE-34878. Log yang lebih rinci kemudian berhenti setelah `modules ready` dengan `configs=0`. Artinya kegagalan nyata berada pada tahap pemilihan konfigurasi EGL, bukan pada loop render atau beban 1080p.

Build koreksi kedua yang aktif:

- menyamakan flag Piglet dengan sampel resmi: composite, flexible memory, dan nilai vendor `0x60`;
- menyamakan pool memori sampel: system 250 MiB, video 512 MiB, flexible 170 MiB, serta command/resource buffer masing-masing 1 MiB;
- memanggil `eglSwapInterval(0)` setelah bind API dan sebelum memilih config;
- kegagalan swap interval hanya menjadi warning;
- membedakan kegagalan panggilan `eglChooseConfig` dari panggilan yang sukses tetapi menghasilkan nol config;
- mencatat tahap konfigurasi Piglet, display, initialize, bind API, config, context, shader, atlas, dan sprite batch;
- memeriksa `GL_LINK_STATUS`, mencatat shader info log, dan mempertahankan kode error atlas/render sebelum dibaca ulang;
- mengembalikan exit normal jika inisialisasi gagal agar tidak memicu CE generik.

Build aktif belum terverifikasi pada perangkat. Root cause baru dianggap terkonfirmasi bila build ini boot dan mencapai checkpoint `sprite batch ready`. Jika masih gagal, ambil log baru; baris `[NATIVE INIT]` terakhir menentukan tahap berikutnya yang harus diperbaiki.

## Template hasil uji

```text
Tanggal dan waktu:
Model PS4 dan firmware:
Hash PKG:
Boot tanpa CE-34878: ya/tidak
Tampilan 16:9: ya/tidak
L3 saja: normal/tidak
R3 saja: normal/tidak
L3+R3 toggle: ya/tidak
Durasi:
FPS average:
p95:
p99:
1% low:
over20ms:
Keputusan: PASS/FAIL
Catatan:
```
