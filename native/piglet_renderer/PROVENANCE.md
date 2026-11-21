# Provenance — PS4 Piglet Upstream Baseline

Baseline ini disalin pada 17 November 2026 dari sampel `samples/piglet` dalam instalasi lokal OpenOrbis PS4 Toolchain.

- Upstream: OpenOrbis PS4 Toolchain
- Upstream repository: https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain
- Lisensi upstream: GPL-3.0; salinan lengkap tersedia di `LICENSE.OpenOrbis-GPL-3.0.txt`
- Instalasi lokal tidak menyertakan metadata `.git`, sehingga commit/tag upstream tidak dapat dipulihkan. Snapshot dikunci dengan SHA-256 sumber berikut agar tetap dapat diaudit:
  - `ApplicationBase.cpp`: `a572d70e03a1784b21cac3102b9772bc310dc9ba2d54fc6202427fcee2990c94`
  - `ApplicationBase.hpp`: `7dd366b79d04ce814f09941d104c62b0ca6dec17914527023a443622cb011e49`
  - `main.cpp`: `6c57a227df28949941e6b3221e6ce07dcbfd9a339f6d38d6c43a86599edca998`
  - `PigletApplication.cpp`: `b48489ae15a03afb8ad1781c115dc5e8bd20b64095ff04792c6a43ad81cb6f87`
  - `PigletApplication.hpp`: `8fc2ee7839b4e492cd2eac70dab2679d6d17242dfffc13eefb6b0f05a14227b7`
  - `build.bat` sebelum perubahan metadata: `fea3bcb29f7a246e988a1a8d8bccea4a74bc3ddf6f772a0806e5ce738b67b41b`
- Modifikasi template berada pada metadata package yang dapat dikonfigurasi, integrasi telemetry minimal di `main.cpp`, serta file `PortBaseTelemetry.hpp/.cpp` dan bridge input.
- `assets/images/rgba_texture.png` adalah placeholder generik dari baseline upstream; downstream wajib menggantinya dengan aset yang berhak mereka distribusikan.
- Title ID, Content ID, versi, judul, dan direktori data berasal dari `config/project.local.bat` yang diabaikan Git. Nilai default aman tersedia di `config/project.example.bat`.

Baseline ini digunakan sebagai renderer PS4 generik untuk port yang menyediakan konten dan metadata miliknya sendiri.
