# Ava development guide

This file applies to the entire repository. Ava is a native Windows productivity
application with two executables:

- `Ava`: the island, launcher, Liquid Glass, system monitor, wallpaper, timer,
  media, and tiling experience.
- `AvaChat`: the native Codex workspace and conversation experience.

Both applications are built with Qt 6, C++20, and QML.

## Working agreements

- Be concise and candid. Distinguish verified behavior from assumptions.
- Preserve the requested goal and constraints. Finish authorized work end to end
  and verify observable behavior before claiming completion.
- Ask only when a decision is materially ambiguous, risky, destructive, or
  requires new authority.
- Keep changes focused. Avoid unrelated edits, unnecessary abstractions, and
  tests that merely repeat implementation details.
- Preserve unrelated work. Never perform destructive, production, remote, or
  publishing actions unless the user explicitly authorizes them.
- Use authoritative, current sources for changing technical contracts and link
  important evidence in the handoff.
- Use specialized skills and parallel agents only when they materially improve
  the work. Parallel branches must own separate files or well-defined interfaces.
- Report meaningful blockers, outcomes, and verification evidence without noisy
  progress narration.

## Product boundary

- Keep the shipping UI native. Do not introduce Electron, embedded browser
  views, React, or another JavaScript runtime.
- Keep the island Codex surface opaque and keep the separate AvaChat window
  outside the Liquid Glass rendering path.
- Preserve the minimal visual language. Prefer spacing, typography, and
  restrained color changes over extra cards, borders, labels, or status pills.
- Performance is a product requirement. Keep user input responsive, stream into
  stable rows, and avoid resetting models or rebuilding complete views for
  incremental events.
- Do not fake, stub, or mock a user-facing production capability. Unknown
  approvals and interactive requests must fail closed.

## Architecture

- Keep Codex app-server transport in `src/codexappserverclient.*`, orchestration
  in `src/codexchatcontroller.*`, persistent presentation state in
  `src/codexmodels.*`, and rendering in `qml/chat/`.
- Keep bounded, versioned thread snapshots in `src/codexthreadsnapshotstore.*`
  and Git/worktree operations in `src/codexgitmanager.*`.
- Keep the main notch application in `src/main.cpp`, `src/islandcontroller.*`,
  and the root `qml/` surfaces. Do not couple it to AvaChat presentation state.
- Keep installed-app discovery, ranking, direct URL/path targets, hotkey
  handling, and launch history in `src/applauncher.*`. Keep CPU, memory, disk,
  and process sampling in `src/systemmonitor.*`; do not move either workload
  onto the QML or UI thread.
- Keep Liquid Glass coordination in `src/liquidglassbackdrop.*`, Windows
  capture and the optical shader in `src/liquidglasscaptureworker.*`, and the
  Qt scene-graph bridge in `src/liquidglasstextureitem.*`. Root QML owns only
  geometry, state, clipping, and interaction inputs.
- Preserve the Liquid Glass GPU pipeline: Windows Graphics Capture,
  triple-buffered shared D3D11 textures, per-resource timeline fences, one
  optical pass, and one scene-graph draw. Do not add per-frame CPU readback,
  resource allocation, keyed-mutex acquisition, explicit flushes, UI-thread
  waits, or a second QML blur/refraction path.
- Preserve the per-monitor wallpaper fallback and capture-candidate filtering
  that prevent hidden layered windows or unusable Explorer surfaces from
  turning the glass black.
- Use stable client-generated IDs for optimistic UI and reconcile authoritative
  app-server items in place. Never display both optimistic and authoritative
  copies.
- Treat app-server events as incremental state updates. Update only affected
  model roles and rows; do not reset a model during live streaming.
- Bound high-volume data such as command output, deltas, and history. Parse rich
  Markdown after completion or at deliberate chunk boundaries, not for every
  token.
- Keep blocking file, process, Git, image, and JSON work off the UI thread when
  it can exceed a frame budget. Preserve event ordering when work moves off-thread.

## Motion and layout

- Preserve the centered conversation safe lane at all window sizes.
- Animate discrete state changes, not token-by-token text growth or streaming
  row height. Honor reduced motion.
- Follow the live edge only when the user is already near it. Preserve the
  reading position when the user scrolls into history.
- Validate typography, spacing, icons, focus, keyboard navigation, and window
  resizing in the running app.

## Build and test

- Requirements: Windows, CMake 3.21+, a C++20 MSVC toolchain, and Qt 6.5+
  with Core, Concurrent, Gui, Network, Qml, Quick, Quick Controls, Quick
  Dialogs, Quick Layouts, Shader Tools, and Test components.
- Configure into a per-worktree build directory. Do not share a build directory
  between parallel worktrees.
- Release build: `cmake --build build --config Release`.
- Before running tests, ensure the active Qt `bin` directory and
  `build/Release` are on `PATH`. A repository-local Qt installation may use:

  ```powershell
  $env:PATH = "$PWD\.qt\6.8.3\msvc2022_64\bin;$PWD\build\Release;$env:PATH"
  ```

- Fast native suite:

  ```powershell
  ctest --test-dir build -C Release -R "system_monitor|app_launcher|codex_models|chat_text_styler|codex_git_manager|attachment_ui" --output-on-failure
  ```

- Live Codex E2E is opt-in because it uses the real account and workspace. Set
  `AVA_RUN_LIVE_CODEX_TEST=1`, then run
  `ctest --test-dir build -C Release -R codex_live_e2e --output-on-failure`.
- Add focused tests for observable state transitions and reconciliation. Avoid
  tests that only mirror implementation details.
- Before claiming completion, build Release, run the relevant tests, launch the
  exact Release binary, inspect the real interface, and exercise the changed
  flow.
- For user-facing changes, validate typography, spacing, icons, focus, keyboard
  navigation, reduced motion, resizing, and high-DPI behavior in the running app.

## Parallel worktrees

- Start parallel branches from the same verified commit and use the `codex/`
  prefix unless the user requests another convention.
- Give each worktree explicit file ownership. Do not modify shared integration
  files such as `CMakeLists.txt` or `src/codexchatcontroller.*` from multiple
  branches without agreeing on an interface first.
- Build and test inside each worktree before integration. After merging, run the
  combined Release build and relevant suites again; isolated green builds do not
  prove the integrated result.
- Commit generated source changes only. Never commit per-worktree build output,
  temporary screenshots, logs, caches, credentials, or local account data.

## Repository hygiene

- Preserve unrelated user changes. Keep edits scoped and avoid destructive Git
  commands.
- Ava's original source code is licensed under PolyForm Noncommercial 1.0.0.
  Do not remove or change the root `LICENSE`, or relicense the project, without
  explicit authorization from the project owner.
- Preserve all third-party licenses and notices. Bundled fonts, icons, and other
  third-party material are not covered by Ava's project license.
- Store temporary screenshots and reference captures outside the repository.
  Do not commit generated screenshots, binaries, logs, or caches.
- Do not push unless the user explicitly asks.
- Keep secrets and machine-specific absolute paths out of committed files.
