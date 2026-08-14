<p align="center">
  <img
    src="docs/ava-hero.png"
    alt="Ava expanded with live media controls and calendar above a metallic knight wallpaper on Windows 11"
    width="100%">
</p>

<p align="center">
  <a href="https://buymeacoffee.com/e_gurl">
    <img
      src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png"
      alt="Buy me a coffee"
      width="217">
  </a>
</p>

# Ava

Ava is a native Windows 11 productivity project built with Qt 6, QML, C++, and
Win32. It combines a compact live-activity island with a separate native Codex
workspace. Shipping features use real Windows, Cider, and Codex state;
deterministic visual states exist only for explicit screenshot QA.

## At a glance

| Application | Purpose |
| --- | --- |
| **Ava** | The island, app launcher, system monitor, media and Cider controls, timer, wallpaper controller, Liquid Glass shell, and optional Dwindle workspace. |
| **AvaChat** | A separate native Codex workspace for persistent conversations, approvals, attachments, diffs, and Git workflows. |

## Build and run

You need:

- Windows 11 and a current Windows SDK with C++/WinRT headers
- Qt 6.5+ with `Concurrent`, `Core`, `Gui`, `Network`, `Qml`, `Quick`,
  `QuickControls2`, `QuickDialogs2`, `QuickLayouts`, `ShaderTools`, and `Test`
- CMake 3.21+
- Visual Studio 2022 with the C++20 MSVC toolchain

From a Qt-enabled developer shell:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\Ava.exe
.\build\Release\AvaChat.exe
```

Before running tests, place the active Qt `bin` directory and `build/Release`
on `PATH`. A repository-local Qt installation may use:

```powershell
$env:PATH = "$PWD\.qt\6.8.3\msvc2022_64\bin;$PWD\build\Release;$env:PATH"
ctest --test-dir build -C Release -R "system_monitor|app_launcher|enhanced_tabs|window_motion|emoji_picker|codex_models|chat_text_styler|codex_git_manager|attachment_ui|cider_integration" --output-on-failure
```

The authenticated live Codex test is opt-in because it uses the current account
and workspace:

```powershell
$env:AVA_RUN_LIVE_CODEX_TEST = "1"
ctest --test-dir build -C Release -R codex_live_e2e --output-on-failure
```

## What Ava includes

### Ava island

- A compact always-on-top clock with switchable Notch and floating Pill shells.
- Optional native Liquid Glass with live refraction, light bending, chromatic
  dispersion, adaptive legibility, and a persistent on/off state.
- A `Ctrl+K` launcher for installed apps, AvaChat, valid web addresses, and
  existing files or folders. Results include ranked search and launch history.
- Optional Monitor mode with live time, GPU and CPU use, battery level, and
  charging state. Selecting CPU opens memory, disk, and the five most active
  processes without exposing process IDs.
- A timer with a snapping minute ruler, compact countdown, progress ring,
  pause/resume, cancel, add-one-minute, a Windows completion alert, and a
  separate quick-action satellite while active.
- Live media title, artist, artwork, source icon, playback state, seekable
  timeline, transport controls, directional track handoffs, and a five-bar
  output-level indicator colored from the current artwork.
- Cider enrichment for favorites, editable queue, playlists, search, listening
  history, timed lyrics, and a session-level audio pulse. Windows media controls
  remain the fallback.
- Windows Core Audio volume and mute controls, plus current network, battery,
  date, time, and week-calendar state.
- A local file-drop shelf with File Explorer reveal and a wallpaper chooser for
  the bundled runtime wallpapers.
- Optional native Dwindle tiling with adjustable splits, live resize and swap
  feedback, title-bar drag swapping, application minimum-size handling,
  animated placement, and exact window restoration.
- Reduced-motion-aware transitions synchronized with the active display rather
  than fixed to one refresh rate.

### AvaChat

- A native Codex app-server client with persistent thread history, thread
  search, incremental streaming, plans, reasoning and tool activity, and
  explicit command or file-change approvals.
- Model and reasoning-effort controls populated from the active Codex account,
  plus Fast mode when the selected model supports it.
- File and image attachments, clipboard image paste, image inspection, rich
  Markdown, syntax-aware code blocks, and a dedicated diff inspector.
- A Git change center for reviewing files, staging and unstaging, discarding
  with confirmation, committing, pushing, and creating pull requests.
- Optional isolated Git worktrees for new conversations.
- Single-instance activation and local IPC so Ava can surface live AvaChat
  activity in the compact island.

## Using Ava

- Hover over Ava for 280 ms or click it to expand.
- Move away for 560 ms to collapse, unless the island is pinned.
- Open the utility carousel to switch shells, toggle Liquid Glass or Monitor
  mode, control sound and pinning, open wallpapers or the timer, start Codex,
  and enable Dwindle. Persistent appearance choices survive restarts.
- Press `Ctrl+K` to open the launcher. Type an application name, a valid web
  address, or an existing absolute file or folder path, then press `Enter`.
- Select the CPU percentage in compact Monitor mode to open the System Monitor
  drill-down without triggering normal island expansion first.
- Open the timer, choose a duration on the ruler, and select **Start Timer**.
- Enable or disable Dwindle from the utility controls or press `Win+Alt+T`.
- Resize a tiled window along an internal edge to change its split.
- Drag one tiled window by its title bar and release it over another to swap
  their positions.
- Drop a local file over Ava to add it to the temporary local file shelf.
- Hover or drag the media timeline to preview and seek when the active Windows
  media session supports playback-position changes.
- Open Codex from the utility carousel for a focused island task, or open
  **AI Chat** from the launcher for the full AvaChat workspace.
- Review every requested command or file-change escalation with **Deny** or
  **Allow**. Island tasks start in `workspace-write` with approvals on request;
  Ava never bypasses the Codex sandbox.

## Native Liquid Glass

Liquid Glass is optional and can be toggled from Ava's utility carousel. It
applies to the island, launcher, monitor, timer, wallpaper, and related shell
surfaces. Codex surfaces remain opaque, and the separate AvaChat window stays
outside the glass rendering path.

This is a native effect, not a web or blur layer. Windows Graphics Capture feeds
live desktop content into a Direct3D 11 optical shader for refraction, edge
bending, dispersion, Fresnel highlights, and caustics. Shared triple-buffered
GPU textures and timeline fences keep frames off the CPU readback path. When
Explorer cannot provide usable desktop pixels, Ava resolves the current
per-monitor wallpaper instead of showing a black surface.

## Performance and fluidity

Ava keeps rendering and input work bounded without reducing feature fidelity.
Windows system-state queries and monitor sampling run away from the UI thread,
repeated refresh requests are coalesced, inactive audio metering sleeps, and the
native silhouette hit region is reused until its geometry changes. Media state
follows Windows session events with a bounded recovery poll instead of a
permanent one-second full query. Window-style updates run only when the requested
state changes, which avoids unnecessary Desktop Window Manager frame
recalculation.

Liquid Glass remains a live GPU effect: capture, optical processing, and display
stay on D3D11 resources with no per-frame CPU readback. Performance work must
preserve shader output, capture continuity, and interaction latency. Capture
target scans also reuse validated DWM bounds instead of asking the compositor
for the same rectangle twice per candidate.

The measurement process, invariants, confirmed findings, and official sources
are documented in [docs/performance.md](docs/performance.md).

## Codex integration

Ava and AvaChat use the installed Codex app-server over newline-delimited
JSON-RPC. Tasks are real persisted Codex threads. Both clients consume
authoritative thread, turn, item, plan, approval, error, and completion events
instead of inferring activity from terminal output.

The island stays deliberately glanceable. It can start a focused task, show the
current action and elapsed time, request approvals, interrupt work, and display
completion. **Open** launches the bundled AvaChat executable for the current
workspace when available. AvaChat provides the full conversation, attachment,
model, diff, and Git experience. Local IPC mirrors active AvaChat status back to
the compact island without coupling the two presentation models.

Codex features require:

- Codex CLI installed and signed in
- A Codex version that provides `codex app-server`
- Git on `PATH` for worktrees and the Git change center
- An authenticated GitHub CLI (`gh`) only when creating pull requests from
  AvaChat

Island tasks use Ava's current working directory or the last saved workspace by
default. Use `--codex-workspace` for Ava or `--workspace` for AvaChat when
launching from another directory.

## Native window behavior

Ava is a frameless Win32 tool window using `WS_EX_TOPMOST`, `WS_EX_TOOLWINDOW`,
and normally `WS_EX_NOACTIVATE`. Its native input mask follows the animated
silhouette, so transparent canvas pixels do not block applications underneath.
Keyboard activation is enabled only while the launcher or Codex task composer
needs text input, then removed when that focused surface closes.

The island remains above ordinary and borderless-fullscreen windows without
stealing focus. Exclusive DirectX fullscreen can bypass desktop composition, so
no conventional desktop overlay can guarantee visibility above every
exclusive-mode game.

## Dwindle workspace tiling

Tiling is disabled by default. When enabled, Ava arranges eligible application
windows independently on each monitor using recursive Dwindle splits. It
preserves the taskbar and island clearance while excluding minimized, tool,
owned, cloaked, topmost, fullscreen-style, and unresponsive windows.

Window placement uses short smoothstep transitions, atomic multi-window updates,
and Desktop Window Manager synchronization. Ava records every original placement
before a window moves. Disabling tiling, pressing `Win+Alt+T` again, or closing
Ava restores surviving windows to their original normal, maximized, or minimized
state.

## Launch and QA options

```powershell
.\build\Release\Ava.exe --pinned
.\build\Release\Ava.exe --timer
.\build\Release\Ava.exe --start-timer 900
.\build\Release\Ava.exe --wallpapers
.\build\Release\Ava.exe --launcher --launcher-query "example.com"
.\build\Release\Ava.exe --monitor-details
.\build\Release\Ava.exe --tiling
.\build\Release\Ava.exe --codex --codex-workspace D:\path\to\project
.\build\Release\Ava.exe --screenshot compact.png
.\build\Release\Ava.exe --expanded --screenshot expanded.png
.\build\Release\Ava.exe --expanded --cider-visual-state playlists --cider-open playlists --screenshot cider-playlists.png
.\build\Release\Ava.exe --expanded --cider-open search --cider-search-query "afterglow" --screenshot cider-search.png
.\build\Release\Ava.exe --expanded --cider-open lyrics --screenshot cider-live-lyrics.png
.\build\Release\Ava.exe --motion-report motion-report.csv
.\build\Release\AvaChat.exe --workspace D:\path\to\project
.\build\Release\AvaChat.exe --workspace D:\path\to\project --new-chat --worktree
```

Generated screenshots, videos, reports, logs, build products, and IDE files are
excluded by `.gitignore`.

For local visual-regression capture, the following options are accepted only
when used with `--screenshot`:

- `--codex-visual-state ready|running|approval|completed|error|compact`
- `--media-peek`
- `--cider-visual-state connect|queue|lyrics|playlists|search|recent|audio`
- `--cider-open queue|lyrics|playlists|search|recent`

These deterministic states exercise the shipping QML layout without replacing
the real runtime integration. Authenticated `--cider-open` captures use the real
Cider connection.

## Project layout

- `src/main.cpp` and `src/islandcontroller.*`: Ava startup, command-line
  options, native window behavior, timers, media, audio, power, clock, file
  drops, and persisted appearance state.
- `src/ciderintegration.*` and `src/cideraudiometer.*`: scoped local Cider
  enrichment for favorites, editable queue, playlists, search, listening
  history, timed lyrics, and a session-level audio pulse, with Windows media
  controls as the fallback.
- `src/applauncher.*`: installed-app discovery, `Ctrl+K`, ranked search, icons,
  launch history, and direct URL or filesystem targets.
- `src/systemmonitor.*`: asynchronous CPU, memory, disk, and process sampling.
- `src/liquidglassbackdrop.*`, `src/liquidglasscaptureworker.*`, and
  `src/liquidglasstextureitem.*`: Windows capture, native D3D11 optics,
  shared-texture synchronization, and Qt scene-graph rendering.
- `src/windowtilingmanager.*`: native Dwindle layout, global shortcut,
  resizing, swapping, filtering, animation, and restoration.
- `src/codexbridge.*`: island-side Codex tasks, AvaChat launch, and compact
  activity IPC.
- `src/codexappserverclient.*`, `src/codexchatcontroller.*`,
  `src/codexmodels.*`, `src/codexthreadsnapshotstore.*`, and
  `src/codexgitmanager.*`: AvaChat transport, orchestration, incremental models,
  bounded persistence, and Git operations.
- Root `qml/`: the island shell, launcher, expanded utilities, Monitor views,
  timer, wallpaper chooser, and compact Codex activity.
- `qml/chat/`: the separate AvaChat window, composer, conversation timeline,
  attachments, code, images, prompts, work disclosures, and diff inspector.
- `tests/`: focused native tests plus the opt-in authenticated Codex E2E.
- `docs/performance.md`: performance invariants, audit evidence, validation
  workflow, and authoritative Qt and Windows references.
- `assets/fonts/` and `assets/icons/`: bundled fonts, licenses, Fluent icons,
  and Ava-specific artwork.

## License

Ava's original source code is available under the
[PolyForm Noncommercial License 1.0.0](LICENSE). You may use, study, test,
modify, and share Ava for personal and other noncommercial purposes. Commercial
use is not licensed: you may not sell Ava, include it in a paid product or
service, or otherwise use it for commercial advantage without separate written
permission from the project owner.

Because it restricts commercial use, Ava is source-available software rather
than OSI-approved open-source software. Third-party assets remain under their
own licenses and are not relicensed by Ava's project license.

## Third-party assets

- Inter is distributed under the SIL Open Font License 1.1. See
  `assets/fonts/OFL-Inter.txt`.
- Geist Mono is distributed under the SIL Open Font License 1.1. See
  `assets/fonts/OFL-Geist.txt`.
- Microsoft Fluent UI System Icons are distributed under the MIT License. See
  `assets/icons/LICENSE`.
