# GitHub Actions CI/CD for Ava — Design Spec

**Date:** 2026-08-08
**Status:** Approved

## Overview

Add two GitHub Actions workflows to the Ava repository so that:

1. Every push to `main` and every pull request targeting `main` runs a compile check.
2. Pushing a `v*.*.*` tag builds the full release artifacts — a self-contained portable zip and a WiX MSI installer — and publishes them as a GitHub Release.

No existing build logic in `CMakeLists.txt` changes except appending a CPack configuration block at the bottom.

## Trigger Rules

| Workflow | File | Trigger |
|---|---|---|
| CI | `.github/workflows/ci.yml` | `push` to `main`; `pull_request` targeting `main` |
| Release | `.github/workflows/release.yml` | `push` of tags matching `v*.*.*` |

Both workflows run exclusively on `windows-latest` because Ava depends on Win32 and WinRT APIs.

## Shared Build Sequence

Both workflows run steps 1–4. The release workflow adds steps 5–8.

1. **Checkout** — `actions/checkout@v4`
2. **Install Qt 6.5** — `jurplel/install-qt-action@v3` with `version: '6.5.*'`, `arch: win64_msvc2019_64`, `modules: qtquickcontrols2`
3. **Configure CMake** — `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
4. **Build** — `cmake --build build --config Release`

## Release-Only Steps

5. **windeployqt** — run `windeployqt --qmldir qml --release build\Release\Ava.exe` to copy all required Qt DLLs, QML plugins, and runtime files next to the executable, making the output folder self-contained.
6. **Zip portable artifact** — use PowerShell `Compress-Archive` to zip the `build\Release\` folder into `Ava-<tag>-portable.zip`.
7. **CPack WiX MSI** — run `cpack -C Release -G WiX` from the build directory to produce `Ava-<version>-win64.msi`. WiX Toolset v3 is pre-installed on `windows-latest` runners; no separate install step is needed.
8. **Create GitHub Release** — `softprops/action-gh-release@v2` with auto-generated release notes (`generate_release_notes: true`) and both artifacts attached:
   - `Ava-<tag>-portable.zip`
   - `Ava-<version>-win64.msi`

The tag name is read from `${{ github.ref_name }}` for consistent artifact naming.

## CMakeLists.txt Changes

Append a CPack configuration block at the bottom of `CMakeLists.txt` (after the existing `install()` call):

```cmake
include(CPack)

set(CPACK_PACKAGE_NAME "Ava")
set(CPACK_PACKAGE_VENDOR "yappologistic")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native Qt 6/QML live-activity island for Windows 11")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Ava")
set(CPACK_WIX_UPGRADE_GUID "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX")  # replace with a real UUID at implementation time; must be stable across all future releases
set(CPACK_GENERATOR "WIX")
set(CPACK_WIX_ARCHITECTURE "x64")
```

A stable `CPACK_WIX_UPGRADE_GUID` must be generated once (any UUID generator works) and committed permanently. Changing it between releases breaks in-place upgrades in the MSI.

## Artifacts per Release

| Artifact | Description |
|---|---|
| `Ava-v1.0.0-portable.zip` | windeployqt output folder zipped — run `Ava.exe` directly, no install needed |
| `Ava-v1.0.0-win64.msi` | WiX installer — installs to Program Files, adds Start Menu entry |

## Tooling Versions

| Tool | Version | Reason |
|---|---|---|
| `actions/checkout` | v4 | current stable |
| `jurplel/install-qt-action` | v3 | supports Qt 6.5 MSVC binaries |
| `softprops/action-gh-release` | v2 | current stable, supports auto release notes |
| WiX Toolset | v3 (pre-installed) | bundled on `windows-latest` |

## What Is Not in Scope

- Code signing (requires a purchased certificate — out of scope for an open-source project at this stage)
- Tests (Ava has no automated test suite; CI only verifies compile success)
- Multi-Qt-version matrix (CMakeLists.txt pins 6.5 minimum; single build is sufficient)
- Linux/macOS builds (Ava is Windows-only by design)
