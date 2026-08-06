# Ava

Ava is a native Windows 11 live-activity island built with Qt 6, QML, C++, and Win32. It sits at the top-center of the desktop as a compact clock and fluidly expands into media controls, a timer, a calendar, system actions, and an optional Dwindle tiling workspace.

There is no demo or placeholder state. Ava reads and controls real Windows services and applications.

## Features

- Compact always-on-top clock using the bundled Geist typeface.
- Apple-inspired timer with a snapping minute ruler, compact countdown, progress ring, pause/resume, cancel, add-one-minute, and a real Windows completion alert.
- Live media title, artist, artwork, playback state, timeline, and transport controls through Global System Media Transport Controls.
- Windows Core Audio volume and mute controls.
- Current network, battery, date, time, and week-calendar state.
- Local file drop shelf with File Explorer reveal.
- Optional native Dwindle tiling with adjustable splits, title-bar drag swapping, application minimum-size handling, animated placement, and exact window restoration.
- Reduced-motion support and display-synchronized animation on 60 Hz, 120 Hz, 144 Hz, and higher-refresh monitors.

## Interaction

- Hover over Ava for 280 ms or click it to expand.
- Move away for 560 ms to collapse, unless it is pinned.
- Open the timer from the right-side utility controls, select a duration on the ruler, and choose **Start Timer**.
- Enable or disable Dwindle tiling from the utility controls or press `Win+Alt+T`.
- Resize a tiled window along an internal edge to adjust its split.
- Drag one tiled window by its title bar and release it over another to swap their positions.
- Drop a local file over Ava to add it to the temporary local file shelf.

## Native window behavior

Ava is a frameless, non-activating Win32 tool window using `WS_EX_TOPMOST`, `WS_EX_TOOLWINDOW`, and `WS_EX_NOACTIVATE`. Its native input mask follows the animated silhouette, so transparent canvas pixels do not block interaction with applications underneath.

The island remains above ordinary and borderless-fullscreen windows without stealing focus. Exclusive DirectX fullscreen can bypass desktop composition, so no conventional desktop overlay can guarantee visibility above every exclusive-mode game.

## Dwindle workspace tiling

Tiling is disabled by default. When enabled, Ava arranges eligible application windows independently on each monitor using recursive Dwindle splits. It preserves the taskbar and island clearance while excluding minimized, tool, owned, cloaked, topmost, fullscreen-style, and unresponsive windows.

Window placement uses short smoothstep transitions, atomic multi-window updates, and Desktop Window Manager synchronization. Every original placement is captured before a window moves. Disabling tiling, pressing `Win+Alt+T` again, or closing Ava restores surviving windows to their original normal, maximized, or minimized state.

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
.\build\Release\Ava.exe
```

## Useful launch options

```powershell
.\build\Release\Ava.exe --pinned
.\build\Release\Ava.exe --timer
.\build\Release\Ava.exe --start-timer 900
.\build\Release\Ava.exe --tiling
.\build\Release\Ava.exe --screenshot compact.png
.\build\Release\Ava.exe --expanded --screenshot expanded.png
.\build\Release\Ava.exe --motion-report motion-report.csv
```

Generated screenshots, videos, reports, logs, build products, and IDE files are excluded by `.gitignore`.

## Project layout

- `src/islandcontroller.h/.cpp`: timer and real Windows media, audio, network, power, clock, and file-drop integration.
- `src/windowtilingmanager.h/.cpp`: native Dwindle layout, global shortcut, resizing, swapping, filtering, animation, and restoration.
- `src/main.cpp`: application startup, command-line options, topmost behavior, font registration, and native silhouette hit-testing.
- `qml/Main.qml`: compact state, spring morphing, input, file drop, and window geometry.
- `qml/ExpandedPanel.qml`: expanded media, calendar, status, and utility layout.
- `qml/TimerPanel.qml`: timer setup, running, paused, and completed states.
- `qml/NotchSurface.qml`: reverse-curve top junctions and continuous lower corners.
- `qml/IslandButton.qml`: shared button visuals, accessibility, and microinteractions.
- `assets/fonts/`: bundled Geist variable font and SIL Open Font License.
- `assets/icons/`: timer artwork and selected Microsoft Fluent UI System Icons.

## Third-party assets

- Geist is distributed under the SIL Open Font License 1.1. See `assets/fonts/OFL-Geist.txt`.
- Microsoft Fluent UI System Icons are distributed under the MIT License. See `assets/icons/LICENSE`.
