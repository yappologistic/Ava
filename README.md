# Ava

Ava is a native Windows 11 live-activity island built with Qt 6, QML, C++, and Win32. It sits at the top-center of the desktop as a compact clock and fluidly expands into media controls, a timer, a calendar, system actions, and an optional Dwindle tiling workspace.

There is no demo or placeholder state. Ava reads and controls real Windows services and applications.

## Features

- Compact always-on-top clock using the bundled Inter typeface.
- Apple-inspired timer with a snapping minute ruler, compact countdown, progress ring, pause/resume, cancel, add-one-minute, and a real Windows completion alert.
- Live media title, artist, artwork, source-app icon, playback state, seekable timeline, transport controls, directional track handoffs, and a five-bar real Windows output-level indicator colored from the current artwork.
- Windows Core Audio volume and mute controls.
- Current network, battery, date, time, and week-calendar state.
- Live Claude Code activity, permission prompts, questions, and completions, answered from the island itself.
- Local file drop shelf with File Explorer reveal.
- Optional native Dwindle tiling with adjustable splits, live resize and swap feedback, title-bar drag swapping, application minimum-size handling, animated placement, and exact window restoration.
- A shared motion system for shell morphing, activity handoffs, media seeking, timer adjustments, calendar changes, file drops, controls, fullscreen retreat, and tiling, with reduced-motion support and display synchronization on 60 Hz, 120 Hz, 144 Hz, and higher-refresh monitors.

## Interaction

- Hover over Ava for 280 ms or click it to expand.
- Move away for 560 ms to collapse, unless it is pinned.
- Open the timer from the right-side utility controls, select a duration on the ruler, and choose **Start Timer**.
- Enable or disable Dwindle tiling from the utility controls or press `Win+Alt+T`.
- Resize a tiled window along an internal edge to adjust its split.
- Drag one tiled window by its title bar and release it over another to swap their positions.
- Drop a local file over Ava to add it to the temporary local file shelf.
- Hover or drag the media timeline to preview and seek when the active Windows media session supports playback-position changes.

## Claude Code

Ava serves Claude Code's native HTTP hooks on `http://127.0.0.1:8722/hook`, so the island shows what Claude Code is doing and answers it without switching to the terminal. Claude Code POSTs each event straight to Ava. There is no helper process, no shell involved, and nothing to spawn per tool call.

There is nothing to install. On every start Ava merges its `type: "http"` hook entries into `%USERPROFILE%\.claude\settings.json`, backing the file up to `settings.json.ava-backup` first and leaving every hook that is not Ava's untouched. Because that file is shared by every Claude Code front end on the machine, one run covers PowerShell, cmd, Git Bash, the VS Code and Cursor extensions, and the Claude desktop app. Start Ava, then start a new Claude Code session.

Re-registering on each start also keeps the entries honest: if the port ever moves, or the file is edited by hand, the next launch repairs it. Ava never rewrites a `settings.json` it cannot parse, and writes nothing when the entries are already correct.

```powershell
.\build\Release\Ava.exe --uninstall-claude-hooks   # remove the entries and exit
.\build\Release\Ava.exe --no-claude-hooks          # run without touching settings.json
```

`%LOCALAPPDATA%\Ava\Ava\claude-endpoint.json` holds the port and a random 256-bit token. Every request must present that token as `Authorization: Bearer`, so no other local process — and no web page, which cannot send the header without a preflight Ava rejects — can push events into the island.

### What appears in the island

| Claude Code event | Island | Answer |
| --- | --- | --- |
| Claude Code working | The Claude mark beside the clock, with a pulsing green dot | — |
| Permission needed | Permission card | **Allow** or **Deny** |
| `AskUserQuestion` | The question and its options | One button per option |
| Response finished | Completion card with the last message | **Keep going**, **Reply**, or **Dismiss** |
| Idle or waiting for input | Transient notice | **Dismiss** |

While Claude Code is working the island stays a clock. The Claude mark appears next to the time in both the compact and expanded shells, carrying a green dot that pulses while the session is busy and turns Claude orange when something is waiting on you. The mark fades out again when the session goes idle. Cards only take over the shell when there is something to answer.

**Allow** and **Deny** return a `PreToolUse` permission decision, and carry a soft green or red bloom from their lower edge so the consequence is readable before the label is. Answering a question returns the chosen option to Claude as the user's answer. **Keep going** and **Reply** block the stop and send your text back as the next instruction.

Typing a reply is the one moment Ava takes keyboard focus. It restores focus to the window you came from as soon as the reply is sent or cancelled.

### The island and the terminal

Hooks run *before* Claude Code's own permission prompt, so the two surfaces cannot both be live for the same decision — while Ava holds a request, the terminal is still on a spinner and has nothing to answer yet. Ava therefore hands over rather than competing:

- Every card carries a countdown. Leave it alone and it releases the request, and Claude Code's own prompt appears in the terminal with the full set of options. Permission and question cards hold for twelve seconds, completion cards for eight.
- The **×** in the card header hands over immediately, without waiting out the countdown.
- Completion cards are the exception: nothing is competing for the answer there, so you can type into your terminal as usual while the card is up.

### Behavior and limits

- If Ava is not running the connection is refused and Claude Code carries on — a failed HTTP hook is a non-blocking error, so nothing hangs and nothing needs configuring.
- An unanswered request always falls back to Claude Code's normal flow rather than hanging.
- Approval cards are skipped entirely in `bypassPermissions`, `dontAsk`, `auto`, and `plan` mode, and for edits in `acceptEdits` mode.
- Ava cannot tell whether a tool call would have prompted, so with island approvals on you will be asked about watched tools even if `permissions.allow` already covers them. **Terminal** defers one request; **ASK IN TERMINAL** in the card header turns island approvals off permanently and leaves everything else — activity, questions, completions — working. There is no button to turn them back on: delete `claude\approvalEnabled` under `HKCU\Software\Ava\Ava` and restart Ava.
- Cloud sessions (Claude Code on the web, and cowork) run their hooks on a remote machine that cannot reach `127.0.0.1` here, so only local sessions reach the island. The desktop app's local sessions work.

### Configuration

| Variable | Default | Effect |
| --- | --- | --- |
| `AVA_CLAUDE_PORT` | `8722` | Loopback port. Ava scans the next nine if it is taken and re-registers on the one it got. |
| `AVA_CLAUDE_SETTINGS` | `%USERPROFILE%\.claude\settings.json` | Which Claude Code settings file Ava registers its hooks in. |
| `AVA_CLAUDE_APPROVE_TOOLS` | `Bash,PowerShell,Write,Edit,MultiEdit,NotebookEdit,WebFetch` | Tools that raise a permission card. MCP tools always do. |
| `AVA_CLAUDE_ASK_TIMEOUT_MS` | `12000` | How long a permission or question card holds before handing over to the terminal. |
| `AVA_CLAUDE_STOP_GRACE_MS` | `8000` | Reply window on completion. `0` shows the card without holding the session. |

`%TEMP%\ava-claude.log` records the endpoint Ava came up on, whether its hooks registered, and why not when either fails.

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
.\build\Release\Ava.exe --no-claude-hooks
.\build\Release\Ava.exe --uninstall-claude-hooks
```

Generated screenshots, videos, reports, logs, build products, and IDE files are excluded by `.gitignore`.

## Project layout

- `src/claudebridge.h/.cpp`: loopback HTTP hook server, token check, request queue, and hook decisions.
- `src/islandcontroller.h/.cpp`: timer and real Windows media, audio, network, power, clock, and file-drop integration.
- `src/windowtilingmanager.h/.cpp`: native Dwindle layout, global shortcut, resizing, swapping, filtering, animation, and restoration.
- `src/main.cpp`: application startup, command-line options, topmost behavior, font registration, and native silhouette hit-testing.
- `qml/Main.qml`: compact state, spring morphing, input, file drop, and window geometry.
- `qml/ExpandedPanel.qml`: expanded media, calendar, status, and utility layout.
- `qml/TimerPanel.qml`: timer setup, running, paused, and completed states.
- `qml/ClaudePanel.qml`: Claude Code permission, question, completion, and reply surfaces.
- `qml/ClaudeBadge.qml`: the Claude mark and working pulse shown beside the clock.
- `qml/NotchSurface.qml`: reverse-curve top junctions and continuous lower corners.
- `qml/IslandButton.qml`: shared button visuals, accessibility, and microinteractions.
- `assets/fonts/`: bundled Inter variable font and SIL Open Font License.
- `assets/icons/`: timer artwork and selected Microsoft Fluent UI System Icons.

## Third-party assets

- Inter is distributed under the SIL Open Font License 1.1. See `assets/fonts/OFL-Inter.txt`.
- Microsoft Fluent UI System Icons are distributed under the MIT License. See `assets/icons/LICENSE`.
- The Claude mark in `assets/icons/claude.svg` is Anthropic's trademark, taken from [svgl.app](https://svgl.app), and is used only to identify Claude Code.
