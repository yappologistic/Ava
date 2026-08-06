# Design QA

## Sources

- Primary motion/visual source: <https://youtu.be/nKomstQedmE>
- Structural reference: <https://github.com/Ebullioscopic/Atoll>
- User-supplied video: `C:\Users\LENOVO\Downloads\g1aRy_BeCMhKsdF4.mp4`
- Preserved primary-video frames: `.reference/frame_analysis/reference-compact-frame-00049.png`, `.reference/frame_analysis/reference-expanded-frame-00061.png`, and `.reference/frame_analysis/reference-control-center-frame-15258.png`
- Full frame metrics: `.reference/frame_analysis/video-frame-metrics.csv`
- Same-scale visual comparison: `design-comparison-final.png`

## Frame-by-frame source audit

- Decoded and inspected all 48,446 native 640×360 frames of the 26:54.87, 30 fps primary video.
- Stable compact source geometry: approximately 50×13 px.
- Stable media-expanded source geometry: approximately 195×43 px.
- Source ratio compact → expanded: 0.256 width and 0.302 height.
- Implementation geometry: 150×39 → 584×128 logical px, preserving those ratios after the approved reverse-curve ears are included.
- Source expansion begins accelerating after the first frame, crosses its target around 170–200 ms, overshoots slightly, and visually settles around 300–330 ms.
- Final implementation trace: 7 ms median / 14 ms maximum active frame gap, 4 px width overshoot, 1 px height overshoot, and about a 300 ms visual settle.

## Visual comparison

- Viewport/state: source frame 61 and the deterministic `--expanded` native render, normalized to identical rendered bounds.
- Outer aspect ratio, lower-corner position, artwork slot, title origin, transport-control baseline, clock center, calendar center, and negative-space band align in the combined comparison.
- The implementation intentionally retains the user's approved concave top junction curves, which differ from the video's plain cropped screen edge.
- Displayed album artwork, media metadata, local time, and calendar dates differ because the implementation uses current Windows state rather than copied or invented source content.

## Issue log

- P0 — Dense Windows diagnostics column did not match the sparse source composition. Resolved by removing the divider and visible network/battery rows from the resting state.
- P0 — Compact island was proportionally too large. Resolved from the measured 50×13 / 195×43 source-state ratio.
- P1 — Previous icon-font controls looked thin and generic. Resolved with official Microsoft Fluent UI System Icons and source-scale optical sizing.
- P1 — Media controls used prominent circular plates absent from the source. Resolved with bare, filled transport icons and hover-only feedback.
- P1 — Motion was initially slow and non-overshooting. Resolved from frame-derived transition timings and verified with native frame telemetry.
- P2 — Permanently visible utility buttons made the right side read like a dashboard. Resolved with a real current-week calendar and a hover-revealed mute/pin/collapse tray.
- P2 — Media and clock content sat too high. Resolved by matching the native frame-61 baselines at identical rendered bounds.

## Functional verification

- Release build succeeds with Qt 6.8.3/MSVC.
- `qmllint` exits 0; remaining warnings are context-property qualification warnings rather than runtime errors.
- Compact and expanded deterministic screenshots succeed.
- Motion-report mode succeeds and stays inside the 60 Hz frame budget on the development machine.
- Media, artwork, timeline state, previous/play/next, audio mute/volume, network, battery, clock, file drop/reveal, pinning, and collapse are backed by real Windows APIs or local filesystem state.
- The native window is topmost, non-activating, and click-through outside the animated silhouette.
- Live Computer Use verification was unavailable because its Windows host returned `EnumWindows failed: 0x80070003` after the prescribed recovery cycle. Native screenshots and telemetry were used for final visual and motion verification.

## Result

passed
