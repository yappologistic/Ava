<p align="center">
  <img
    src="docs/ava-hero.png"
    alt="Ava expanded with live media controls and calendar on Windows 11"
    width="100%">
</p>

# Ava

Ava is a native Windows 11 productivity app built with Qt 6, QML, C++20, and
Win32. It pairs a compact live-activity island with a separate native Codex
workspace.

| Application | Purpose |
| --- | --- |
| **Ava** | Island, launcher, media controls, system monitor, timer, wallpaper tools, Liquid Glass, and optional window tiling. |
| **AvaChat** | Native Codex conversations, approvals, attachments, diffs, and Git workflows. |

## Highlights

- Live Windows media, audio, battery, network, clock, calendar, and system state.
- Fast app, URL, file, and folder launching with `Ctrl+K`.
- Native optional Liquid Glass rendered through Windows Graphics Capture and
  Direct3D 11, with no embedded browser runtime.
- Cider enrichment for queue, playlists, search, history, lyrics, and audio
  pulse, with Windows media controls as the fallback.
- Reduced-motion support, native tray controls, persistent settings, and
  optional Dwindle window tiling.
- A separate native AvaChat client backed by the installed Codex app-server.

## Build

Requirements:

- Windows 11 and Visual Studio 2022 with the C++20 MSVC toolchain
- CMake 3.21+
- Qt 6.5+ with Core, Concurrent, Gui, Network, Qml, Quick, Quick Controls,
  Quick Dialogs, Quick Layouts, Shader Tools, and Test

From a Qt-enabled developer shell:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\Ava.exe
.\build\Release\AvaChat.exe
```

To run the native test suite, ensure the active Qt `bin` directory and
`build/Release` are on `PATH`, then run:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The authenticated Codex end-to-end test is opt-in through
`AVA_RUN_LIVE_CODEX_TEST=1` because it uses the current account and workspace.

## License

Ava's original source code is available under the
[PolyForm Noncommercial License 1.0.0](LICENSE). Commercial use requires
separate written permission from the project owner.

Bundled third-party fonts and icons retain their own licenses:

- Inter: `assets/fonts/OFL-Inter.txt`
- Geist Mono: `assets/fonts/OFL-Geist.txt`
- Microsoft Fluent UI System Icons: `assets/icons/LICENSE`
