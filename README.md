# BCDScribe

BCDScribe is a Linux desktop application for inspecting and editing offline Windows Boot Configuration Data (BCD) stores. It is built with C++17 and Qt 6, and uses libhivex to read and write registry hive files.

## Features

- Browse BCD objects and inspect their settings with friendly labels.
- Create a fresh BCD store with a Boot Manager object and configurable boot-menu timeout.
- Edit supported Boolean, policy, reference, device, and timeout values.
- Create, copy, and delete boot objects and BCD elements.
- Save edits to a separate file or overwrite the open BCD store after confirmation.
- Build a standalone x86_64 AppImage with bundled Qt, X11, and native Wayland support.

BCDScribe edits offline store files. It does not run Windows `bcdedit.exe` or directly modify the active Windows boot configuration.

## Install build dependencies

On Debian/Ubuntu, Fedora/RHEL, Arch, and openSUSE, the bootstrap script installs native build packages and the AppImage packaging tools:

```sh
./install-dependencies.sh
```

Preview package and download commands with `./install-dependencies.sh --dry-run`. Add `--build` to create an AppImage after installing dependencies. Package installation may require `sudo`.

## Build and run

```sh
cmake -S . -B build
cmake --build build
./build/BCDScribe
```

**Git publishing:** the default CMake build includes an automatic Git update target. When an `origin` remote is configured, it stages all non-ignored repository changes, commits them, and pushes the current branch. Review your changes before building if you do not intend to publish them. Build output directories and AppImages are ignored by Git. If no `origin` is configured, publishing is skipped.

**GitHub releases:** after the AppImage is built and the commit is pushed, CMake creates a versioned GitHub release when `HEAD` is newer than the latest release. Releases start at `v0.1.0` and increment the patch version; repeat builds of an already released commit do not create duplicates. This requires GitHub CLI (`gh`) to be authenticated for the repository (`gh auth login`).

## Build an AppImage

```sh
./build-appimage.sh
```

The output is `dist/BCDScribe-x86_64.AppImage`. The AppImage bundles Qt's Wayland and X11 platform plugins; it uses native Wayland where available and can use XWayland as a fallback. See [packaging/README.md](packaging/README.md) for tool configuration and compatibility notes. AppImages do not bundle the Linux kernel or glibc; building on an older Linux baseline improves compatibility with older distributions.
