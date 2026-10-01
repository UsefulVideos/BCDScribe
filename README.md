# BCDScribe

BCDScribe is a Linux desktop application for inspecting and editing offline Windows Boot Configuration Data (BCD) stores. It is built with C++17 and Qt 6, and uses libhivex to read and write registry hive files.

## Features

- Browse BCD objects, select and highlight the configured default entry, and inspect settings with mapped BCDEdit option names.
- Create a fresh BCD store with a Boot Manager object and configurable boot-menu timeout.
- Create NTLDR, GRUB4DOS, Windows Memory Diagnostic, WIM/Ramdisk, and VHD/VHDX boot-entry templates.
- Edit supported Boolean, policy, reference, device, and timeout values.
- Resolve BCD devices to Linux mountpoints and partition paths.
- Create, copy, and delete boot objects and BCD elements.
- Configure Windows-style keyboard shortcuts and view them in context menus.
- View project and release links on the About tab.
- Save edits to a separate file or overwrite the open BCD store after confirmation.
- Build a standalone x86_64 AppImage with bundled Qt, X11, and native Wayland support.

BCDScribe edits offline store files. It does not run Windows `bcdedit.exe` or directly modify the active Windows boot configuration.

On Linux, BCDScribe requests administrator authorization through PolicyKit at launch. The application then runs with elevated privileges; review BCD stores carefully before saving changes.

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

**Git publishing:** the default CMake build includes an automatic Git update target. For the repository owner, commits push to `UsefulVideos/BCDScribe`. Other authenticated users get or reuse a personal fork, with the source configured as `upstream` and their fork as `origin`; their commits push to that fork. The target stages all non-ignored repository changes, so review your changes before building if you do not intend to publish them. Build output directories and AppImages are ignored by Git.

**GitHub releases:** after the AppImage is built and the commit is pushed, CMake creates a versioned GitHub release when `HEAD` is newer than the latest release. Releases start at `v0.1.0` and increment the patch version; repeat builds of an already released commit do not create duplicates. This requires GitHub CLI (`gh`) to be authenticated for the repository (`gh auth login`).

## Build an AppImage

```sh
./build-appimage.sh
```

The output is `dist/BCDScribe-x86_64.AppImage`. The AppImage bundles Qt's Wayland and X11 platform plugins; it uses native Wayland where available and can use XWayland as a fallback. See [packaging/README.md](packaging/README.md) for tool configuration and compatibility notes. AppImages do not bundle the Linux kernel or glibc; building on an older Linux baseline improves compatibility with older distributions.
