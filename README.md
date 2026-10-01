# BCDScribe

BCDScribe is a Linux desktop application for inspecting and editing offline Windows Boot Configuration Data (BCD) stores. It is built with C++17 and Qt 6, and uses libhivex to read and write registry hive files.

## Features

- Browse BCD objects and inspect their settings with friendly labels.
- Create a fresh BCD store with a Boot Manager object and configurable boot-menu timeout.
- Edit supported Boolean, policy, reference, device, and timeout values.
- Create, copy, and delete boot objects and BCD elements.
- Save edits to a separate output file; the source hive is not overwritten.
- Build a standalone x86_64 AppImage with bundled Qt and runtime libraries.

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

## Build an AppImage

```sh
./build-appimage.sh
```

The output is `dist/BCDScribe-x86_64.AppImage`. See [packaging/README.md](packaging/README.md) for tool configuration and compatibility notes. AppImages do not bundle the Linux kernel or glibc; building on an older Linux baseline improves compatibility with older distributions.
