# Dynamic Island for Windows 11

A native Qt 6/QML + C++ live-activity surface for Windows 11. It sits flush with the top-center of the active display, defaults to a compact clock, and morphs into a low, wide media-and-system panel modeled on the supplied Dynamic Island reference.

The app does not use demo state. Everything shown in the island comes from Windows:

- Clock and date use the local system time.
- Media title, artist, artwork, timeline, capabilities, and transport controls use Global System Media Transport Controls (GSMTC).
- Volume and mute use the Windows Core Audio endpoint.
- Network name and connectivity come from the active Windows network profile.
- Battery percentage and charging state come from `GetSystemPowerStatus`.
- Dropped files remain local and can be revealed in File Explorer.

## Interaction

- Compact state: `150 × 39` logical pixels with a centered Segoe UI Variable clock.
- Expanded state: `584 × 128` logical pixels with real media controls on the left and a live week calendar on the right.
- Hover for 280 ms or click the compact surface to expand.
- Leave for 560 ms to collapse, unless the island is pinned.
- Use the real previous, play/pause, and next media controls.
- Mute/unmute the active audio endpoint, pin the island, or collapse it from the bottom-right controls.
- Drag a local file over the island to reveal the local drop target.
- Enable native Dwindle workspace tiling from the right utility cluster or with `Win+Alt+T`.
- Reduced-motion preferences are respected.

The shell uses an interruptible, frame-time-driven spring for width and height morphing, staged content transitions, and small press/hover responses. Its geometry and timing were measured across all 48,446 frames of the supplied 30 fps reference video: the stable source states are approximately `50 × 13` and `195 × 43`, with a roughly 300 ms overshooting settle. Height leads width while opening, width leads height while closing, and velocity carries through a mid-flight reversal instead of restarting. The Qt geometry preserves the source ratios at Windows desktop scale. Its top-left and top-right junctions use reverse cubic curves, while the lower corners maintain a continuous rounded silhouette.

Animations are not capped at 60 Hz. Qt Quick's synchronized render loop presents at the active display's vertical refresh rate, and the shell spring advances from `FrameAnimation.frameTime`, so 120 Hz, 144 Hz, and higher-refresh monitors receive genuinely denser motion samples. Vsync remains enabled, animation duration is time-based rather than frame-count based, and scene-graph resources stay resident to avoid avoidable setup work during an interaction.

## Native window behavior

The frameless Qt window is created with `WS_EX_TOPMOST`, `WS_EX_TOOLWINDOW`, and `WS_EX_NOACTIVATE`. A native `QWindow` input mask follows the animated silhouette, so Windows never routes pointer input from the transparent canvas to the island. Topmost status is reaffirmed without activation, allowing the island to remain above ordinary and borderless-fullscreen windows without stealing focus.

## Dwindle workspace tiling

Workspace tiling is off by default. When enabled, a native Win32 engine arranges eligible application windows independently on each monitor using a recursive Dwindle split. It respects each monitor's working area, preserves the taskbar, leaves compact-island clearance on the island display, and maintains consistent outer and inner gaps. New windows enter the layout automatically, minimized windows remain untouched, and tool, owned, cloaked, topmost, fullscreen-style, or unresponsive surfaces are excluded.

Resize a tiled window from an internal edge to change the adjacent split; the chosen proportion is preserved across automatic retiles. Drag a tiled window by its title bar and release it over another tile to swap their locations without changing the slot sizes. Minimum-size constraints still take priority when an application cannot accept the requested geometry.

Tiling and restoration use a short smoothstep transition, atomic multi-window placement, and Desktop Window Manager frame synchronization. Geometry updates follow the active display cadence and duplicate frames are discarded before they can trigger unnecessary application reflow.

Every window placement is captured before the engine moves it. Disabling tiling, pressing `Win+Alt+T` again, or closing Dynamic Island restores surviving windows to their original normal, maximized, or minimized placement.

The global shortcut uses the standard Windows hotkey service with a debounced low-level keyboard fallback for systems that register the chord but fail to deliver `WM_HOTKEY` to the island window.

Exclusive DirectX fullscreen can bypass the normal Desktop Window Manager composition path, so no conventional desktop overlay can guarantee visibility above every exclusive-mode game.

## Build on Windows

Requirements:

- Windows 11 and a current Windows SDK with C++/WinRT headers
- Qt 6.5+ with `Core`, `Gui`, `Qml`, `Quick`, and `QuickControls2`
- CMake 3.21+
- Visual Studio 2022/MSVC or another Qt-supported C++ toolchain

From a Qt-enabled developer shell:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\DynamicIsland.exe
```

## Verification switches

The executable supports deterministic native renders and motion telemetry:

```powershell
.\build\Release\DynamicIsland.exe --screenshot compact.png
.\build\Release\DynamicIsland.exe --expanded --screenshot expanded.png
.\build\Release\DynamicIsland.exe --pinned
.\build\Release\DynamicIsland.exe --tiling
.\build\Release\DynamicIsland.exe --motion-report motion-report.csv
```

Screenshot mode adds a gray QA backdrop so the transparent edge and reverse curves can be inspected. Normal execution remains transparent.

## Project layout

- `src/islandcontroller.h/.cpp` — real Windows media, audio, network, power, clock, and file-drop integration.
- `src/main.cpp` — application startup, CLI verification modes, topmost behavior, and native silhouette hit-testing.
- `src/windowtilingmanager.h/.cpp` — opt-in Win32 Dwindle layout engine, global shortcut, window filtering, and restoration.
- `qml/Main.qml` — compact state, morphing layout, input, drop target, and window geometry.
- `qml/ExpandedPanel.qml` — expanded media, time, status, and action layout.
- `qml/NotchSurface.qml` — cubic reverse-curve ears and continuous lower corners.
- `qml/IslandButton.qml` — shared button visuals, accessibility, and microinteractions.
- `assets/icons/` — selected Microsoft Fluent UI System Icons, recolored for the dark surface under the upstream MIT license.
- `CMakeLists.txt` — Qt executable, QML module, and Windows libraries.
