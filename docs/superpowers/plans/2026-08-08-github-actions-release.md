# GitHub Actions CI/CD Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two GitHub Actions workflows to Ava — a compile-check CI workflow and a tag-triggered release workflow that produces a portable zip and a WiX MSI installer.

**Architecture:** Two independent workflow files under `.github/workflows/`. Both share the same Qt 6.5 + CMake + MSVC build sequence. The release workflow extends the CI sequence with `windeployqt`, CPack WiX packaging, and `softprops/action-gh-release`. A CPack config block is appended to `CMakeLists.txt` to enable MSI generation.

**Tech Stack:** GitHub Actions, Qt 6.5 (MSVC), CMake 3.21+, WiX Toolset v3 (pre-installed on `windows-latest`), `jurplel/install-qt-action@v3`, `softprops/action-gh-release@v2`, PowerShell.

## Global Constraints

- Target platform: `windows-latest` only — Ava uses Win32/WinRT APIs
- Qt version: `6.5.*` — matches `CMakeLists.txt` minimum requirement
- Qt arch: `win64_msvc2019_64`
- CMake minimum: 3.21
- C++ standard: 20
- WiX Upgrade GUID: `72EF1729-90D1-4CF5-B127-05799A228AE0` — must never change after first release
- All workflow files must pass `actionlint` (no syntax errors)
- No code signing — out of scope

---

### Task 1: Append CPack configuration to CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt` (append after the existing `install()` call at the bottom)

**Interfaces:**
- Produces: `cpack -C Release -G WiX` produces `Ava-0.1.0-win64.msi` in the build directory

- [ ] **Step 1: Open `CMakeLists.txt` and locate the bottom**

The last lines currently are:
```cmake
install(TARGETS Ava
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
```

- [ ] **Step 2: Append the CPack block after `install()`**

Add exactly this at the end of `CMakeLists.txt`:
```cmake
# CPack / WiX MSI installer
set(CPACK_PACKAGE_NAME "Ava")
set(CPACK_PACKAGE_VENDOR "yappologistic")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native Qt 6/QML live-activity island for Windows 11")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Ava")
set(CPACK_WIX_UPGRADE_GUID "72EF1729-90D1-4CF5-B127-05799A228AE0")
set(CPACK_GENERATOR "WIX")
set(CPACK_WIX_ARCHITECTURE "x64")
include(CPack)
```

- [ ] **Step 3: Verify the file ends correctly**

The file should end with `include(CPack)`. There should be no other `include(CPack)` anywhere in the file.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add CPack/WiX MSI configuration"
```

---

### Task 2: Create the CI workflow

**Files:**
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Produces: A workflow that runs on every push to `main` and every PR targeting `main`, building Ava with MSVC and failing the check if the build fails.

- [ ] **Step 1: Create the `.github/workflows/` directory**

```bash
mkdir -p .github/workflows
```

- [ ] **Step 2: Create `.github/workflows/ci.yml` with this exact content**

```yaml
name: CI

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build:
    runs-on: windows-latest

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Install Qt
        uses: jurplel/install-qt-action@v3
        with:
          version: '6.5.*'
          arch: win64_msvc2019_64
          modules: qtquickcontrols2

      - name: Configure CMake
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build --config Release
```

- [ ] **Step 3: Verify the file is valid YAML**

On any machine with Python available:
```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))"
```
Expected: no output, exit code 0.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add compile-check workflow on push and PR"
```

---

### Task 3: Create the release workflow

**Files:**
- Create: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: CPack WiX config from Task 1 (`cpack -C Release -G WiX`)
- Produces: A workflow triggered by `v*.*.*` tags that publishes a GitHub Release with `Ava-<tag>-portable.zip` and `Ava-<version>-win64.msi`

- [ ] **Step 1: Create `.github/workflows/release.yml` with this exact content**

```yaml
name: Release

on:
  push:
    tags:
      - 'v*.*.*'

permissions:
  contents: write

jobs:
  build-and-release:
    runs-on: windows-latest

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Install Qt
        uses: jurplel/install-qt-action@v3
        with:
          version: '6.5.*'
          arch: win64_msvc2019_64
          modules: qtquickcontrols2

      - name: Configure CMake
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build --config Release

      - name: Deploy Qt dependencies
        run: |
          $qtBin = "$env:Qt6_DIR\bin"
          & "$qtBin\windeployqt.exe" --qmldir qml --release build\Release\Ava.exe

      - name: Create portable zip
        run: |
          $tag = "${{ github.ref_name }}"
          Compress-Archive -Path build\Release\* -DestinationPath "Ava-$tag-portable.zip"

      - name: Build MSI installer
        run: |
          cd build
          cpack -C Release -G WIX

      - name: Find MSI artifact
        id: find_msi
        run: |
          $msi = Get-ChildItem -Path build -Filter "*.msi" | Select-Object -First 1
          echo "msi_path=build\$($msi.Name)" >> $env:GITHUB_OUTPUT
          echo "msi_name=$($msi.Name)" >> $env:GITHUB_OUTPUT

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          generate_release_notes: true
          files: |
            Ava-${{ github.ref_name }}-portable.zip
            ${{ steps.find_msi.outputs.msi_path }}
```

- [ ] **Step 2: Verify the file is valid YAML**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/release.yml'))"
```
Expected: no output, exit code 0.

- [ ] **Step 3: Confirm `permissions: contents: write` is present**

The `softprops/action-gh-release` action needs write access to repository contents to create releases. The `permissions` block at the job level grants this. Verify it is present in the file exactly as written above.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: add tag-triggered release workflow with portable zip and MSI"
```

---

### Task 4: Push branch and open pull request

**Files:**
- No file changes — git operations only.

**Interfaces:**
- Produces: A pull request on `yappologistic/Ava` from `erfannf/Ava:main` containing all three commits from Tasks 1–3.

- [ ] **Step 1: Push the branch to the fork**

```bash
git push origin main
```

- [ ] **Step 2: Open the pull request**

```bash
gh pr create \
  --repo yappologistic/Ava \
  --head erfannf:main \
  --title "ci: add GitHub Actions CI and release workflows" \
  --body "$(cat <<'EOF'
## Summary

- Adds `.github/workflows/ci.yml` — compile-check on every push to `main` and every PR targeting `main`
- Adds `.github/workflows/release.yml` — builds EXE, runs `windeployqt`, packages a portable zip and WiX MSI, and publishes a GitHub Release when a `v*.*.*` tag is pushed
- Appends CPack/WiX configuration to `CMakeLists.txt` to enable MSI generation

## How to cut a release

1. Tag a commit: `git tag v1.0.0 && git push origin v1.0.0`
2. The release workflow runs automatically on `windows-latest`
3. A GitHub Release is created with two artifacts:
   - `Ava-v1.0.0-portable.zip` — run `Ava.exe` directly, no install needed
   - `Ava-0.1.0-win64.msi` — installs to Program Files, adds Start Menu entry

## Test plan

- [ ] Merge this PR to a fork and push a `v0.1.0` tag — verify the release workflow creates a GitHub Release with both artifacts attached
- [ ] Open a test PR against main — verify the CI workflow runs and shows a green check

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 3: Record the PR URL**

Copy the PR URL from the output of the previous command and share it with the repo owner.
