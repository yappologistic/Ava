<p align="center">
  <img src="docs/ava-hero.png" alt="Ava expanded on the Windows 11 desktop with live media controls and calendar" width="100%">
</p>

# Ava

Ava is a native Windows 11 live-activity island built with Qt 6, QML, C++, and Win32. It sits at the top-center of the desktop as a compact clock and fluidly expands into media controls, Codex activity, a timer, a calendar, system actions, and an optional Dwindle tiling workspace.

There is no demo or placeholder state. Ava reads and controls real Windows services and applications.

## Features

- Compact always-on-top clock using the bundled Inter typeface.
- Switchable Notch and Pill shell modes; Pill Mode keeps every interaction while
  floating below the screen edge with consistent rounding on all four corners.
- Optional Monitor compact mode with live time, GPU and CPU utilization, battery
  level and charging state, plus a separate quick-action timer satellite.
- Apple-inspired timer with a snapping minute ruler, compact countdown, progress ring, pause/resume, cancel, add-one-minute, and a real Windows completion alert.
- Live media title, artist, artwork, source-app icon, playback state, seekable timeline, transport controls, directional track handoffs, and a five-bar real Windows output-level indicator colored from the current artwork.
- Windows Core Audio volume and mute controls.
- Current network, battery, date, time, and week-calendar state.
- Native Codex CLI integration through the official app-server protocol: start a real workspace task, follow live reasoning/tool/file/command activity, review command and file-change approvals, interrupt work, see completion, and hand off to the Codex desktop app.
- Local file drop shelf with File Explorer reveal.
- Optional native Dwindle tiling with adjustable splits, live resize and swap feedback, title-bar drag swapping, application minimum-size handling, animated placement, and exact window restoration.
- A shared motion system for shell morphing, activity handoffs, media seeking, timer adjustments, calendar changes, file drops, controls, fullscreen retreat, and tiling, with reduced-motion support and display synchronization on 60 Hz, 120 Hz, 144 Hz, and higher-refresh monitors.

## Interaction

- Hover over Ava for 280 ms or click it to expand.
- Move away for 560 ms to collapse, unless it is pinned.
- Open the utility carousel and choose the shell-mode item to switch between
  Notch Mode and Pill Mode. Ava remembers the selection across restarts.
- Choose **Monitor** in the same carousel to replace the idle clock with the live
  system readouts. Media and Codex alerts still take priority when they need attention.
- Open the timer from the right-side utility controls, select a duration on the ruler, and choose **Start Timer**.
- Enable or disable Dwindle tiling from the utility controls or press `Win+Alt+T`.
- Resize a tiled window along an internal edge to adjust its split.
- Drag one tiled window by its title bar and release it over another to swap their positions.
- Drop a local file over Ava to add it to the temporary local file shelf.
- Hover or drag the media timeline to preview and seek when the active Windows media session supports playback-position changes.
- Open Codex from the right-side utility controls, enter a focused task, and press **Run**. Active work temporarily replaces the compact clock with one glanceable status line.
- Review every requested command or file-change escalation with **Deny** or **Allow**. Ava starts Codex in `workspace-write` with approvals on request; it never bypasses the Codex sandbox.
- Choose **Open** to hand the current workspace to the Codex desktop app. The documented Codex launcher currently supports workspace handoff rather than an exact-thread deep link.

## Codex integration

Ava starts the installed native Codex app-server over newline-delimited JSON-RPC. Each task created from the island is a real persisted Codex thread. Ava listens to authoritative thread, turn, item, plan, approval, error, and completion events instead of inferring activity from terminal text.

The island intentionally stays glanceable: it shows the current action, one concise detail, elapsed time, changed-file count, and only the controls relevant to the current state. Full transcripts, model selection, configuration, and detailed diffs remain in Codex.

Requirements:

- Codex CLI installed and signed in
- A Codex version that provides `codex app-server` and `codex app`

By default, Codex uses Ava's current working directory or the last workspace saved by Ava. Use `--codex-workspace` when launching from another directory.

## Native window behavior

Ava is a frameless Win32 tool window using `WS_EX_TOPMOST`, `WS_EX_TOOLWINDOW`, and normally `WS_EX_NOACTIVATE`. Its native input mask follows the animated silhouette, so transparent canvas pixels do not block interaction with applications underneath. Keyboard activation is enabled only while the Codex task composer is intentionally open, then removed as soon as the task starts or the panel closes.

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
.\build\Release\Ava.exe --codex --codex-workspace D:\path\to\project
.\build\Release\Ava.exe --screenshot compact.png
.\build\Release\Ava.exe --expanded --screenshot expanded.png
.\build\Release\Ava.exe --motion-report motion-report.csv
```

Generated screenshots, videos, reports, logs, build products, and IDE files are excluded by `.gitignore`.

For local visual-regression capture, `--codex-visual-state ready|running|approval|completed|error|compact` and `--media-peek` are accepted only together with `--screenshot`. These states exercise the shipping QML layout without replacing the real runtime integration.

## Project layout

- `src/islandcontroller.h/.cpp`: timer and real Windows media, audio, network, power, CPU/GPU, clock, and file-drop integration.
- `src/windowtilingmanager.h/.cpp`: native Dwindle layout, global shortcut, resizing, swapping, filtering, animation, and restoration.
- `src/codexbridge.h/.cpp`: Codex app-server process management, JSON-RPC, real thread/turn activity, approvals, interruption, recovery, and desktop handoff.
- `src/main.cpp`: application startup, command-line options, topmost behavior, font registration, and native silhouette hit-testing.
- `qml/Main.qml`: compact state, spring morphing, input, file drop, and window geometry.
- `qml/ExpandedPanel.qml`: expanded media, calendar, status, and utility layout.
- `qml/TimerPanel.qml`: timer setup, running, paused, and completed states.
- `qml/MonitorCompact.qml` and `qml/TimerSatellite.qml`: compact system telemetry and the active-timer quick action.
- `qml/CodexPanel.qml`: focused ready, running, approval, completion, and recovery layouts.
- `qml/OpenTuiSpinner.qml`: reduced-motion-aware active-work spinner using an OpenTUI-style frame cadence.
- `qml/NotchSurface.qml`: reverse-curve top junctions and continuous lower corners.
- `qml/IslandButton.qml`: shared button visuals, accessibility, and microinteractions.
- `assets/fonts/`: bundled Inter and Geist Mono variable fonts with their SIL Open Font licenses.
- `assets/icons/`: timer artwork, selected Microsoft Fluent UI System Icons, and Ava's custom rounded Codex status symbols.

## Third-party assets

- Inter is distributed under the SIL Open Font License 1.1. See `assets/fonts/OFL-Inter.txt`.
- Geist Mono is distributed under the SIL Open Font License 1.1. See `assets/fonts/OFL-Geist.txt`.
- Microsoft Fluent UI System Icons are distributed under the MIT License. See `assets/icons/LICENSE`.
