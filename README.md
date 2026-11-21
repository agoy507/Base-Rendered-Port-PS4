# PS4 Native Render Port Base

Reusable source-only starter for a native PlayStation 4 renderer using Piglet, EGL, and OpenGL ES 2.0. It contains a 1080p demo scene, deterministic fixed-step loop, DualShock 4 input bridge, frame telemetry, and regression tests. It contains no game content, packages, SDK binaries, or artwork from a commercial title.

## Adapting it

1. Copy `config/project.example.bat` to `config/project.local.bat`.
2. Set a title, Title ID, Content ID, and writable data directory you own.
3. Replace the generic demo scene and placeholder texture with assets and gameplay you are authorized to distribute.
4. Run `powershell -File tests/run_native_tests.ps1` and the Node contract tests before building with `build_ps4_port_base.bat`.

The Windows batch wrapper is the supported build entry point and reads `project.local.bat`. For the upstream Makefile path, pass equivalent values as `make` variables (`TITLE`, `TITLE_ID`, `CONTENT_ID`, and `VERSION`).

The Piglet renderer baseline is derived from the OpenOrbis PS4 Toolchain and is distributed under GPL-3.0; see `native/piglet_renderer/LICENSE.OpenOrbis-GPL-3.0.txt`.

Repository history retains the original project-specific implementation separately from this reusable base.
