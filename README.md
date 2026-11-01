# BCDScribe

BCDScribe is a Linux desktop application for inspecting and editing offline Windows Boot Configuration Data (BCD) stores. It is built with C++17 and Qt 6, and uses libhivex to read and write registry hive files.

## Features

- Browse BCD objects, select and highlight the configured default entry, and inspect settings with mapped BCDEdit option names.
- Create a fresh BCD store with a Boot Manager object and configurable boot-menu timeout.
- Create NTLDR, GRUB4DOS, Windows Memory Diagnostic, and configurable WIM/Ramdisk boot entries.
- Create Windows-on-VHD/VHDX entries with GPT or MBR host and guest partition descriptors.
- Edit supported Boolean, policy, reference, device, and timeout values.
- Resolve BCD devices to Linux mountpoints and partition paths.
- Create, copy, and delete boot objects and BCD elements.
- Configure Windows-style keyboard shortcuts and view them in context menus.
- View project and release links on the About tab.
- Save edits to a separate file or overwrite the open BCD store after confirmation.
- Build a standalone x86_64 AppImage with bundled Qt, X11, and native Wayland support.

BCDScribe edits offline store files. It does not run Windows `bcdedit.exe` or directly modify the active Windows boot configuration.

On Linux, BCDScribe requests administrator authorization through PolicyKit at launch. The application then runs with elevated privileges; review BCD stores carefully before saving changes.

The **Theme** tab follows the distro's Qt/platform theme by default and stores preferences in the logged-in desktop user's configuration directory, even when the editor is elevated. Optional Qt widget styles and Light/Dark palettes can be selected explicitly.

## Console commands

BCDScribe also supports offline-store commands without starting the GUI or opening a PolicyKit prompt:

```sh
BCDScribe --help
BCDScribe --store /path/to/BCD /enum
BCDScribe --store /path/to/BCD /create "Windows PE" --write
BCDScribe --store /path/to/BCD /set '{GUID}' 12000004 string "Windows PE" --write
BCDScribe --store /path/to/BCD /deletevalue '{GUID}' 12000004 --write
BCDScribe --store /path/to/BCD /delete '{GUID}' --write
BCDScribe --store /path/to/BCD /timeout 10 --write
BCDScribe --store /path/to/BCD /default '{GUID}' --write
BCDScribe --store /path/to/BCD /displayorder '{GUID}' '{OTHER-GUID}' --write
BCDScribe --store /path/to/BCD /bootdebug '{GUID}' on --write
```

The console currently implements `/enum`, `/export`, `/import`, `/create`, `/set`, `/delete`, `/deletevalue`, `/timeout`, `/default`, `/displayorder`, `/bootsequence`, `/toolsdisplayorder`, `/bootdebug`, `/debug`, `/ems`, `/event`, and `/badmemoryaccess`. `/enum` and `/export` are read-only. Commands that change a store require `--write`; binary values for `/set` use comma-separated hex bytes (for example `01,00,ff`), and the optional `--type DWORD` on `/create` changes the object's BCD type. Run the command with permissions to access the selected offline store.

VHD/VHDX entry creation requires `qemu-nbd`, `sfdisk`, `findmnt`, and `lsblk`, plus the Linux `nbd` kernel module. The app attaches the selected image read-only to inspect its partition table and detaches it before writing the BCD store. If no `/dev/nbdN` device is available, load the module (for example, `sudo modprobe nbd`) and retry. The VHD file must be stored on a GPT/MBR partition that Windows Boot Manager can read; select the partition inside the image that contains the Windows installation.

WIM/Ramdisk entry creation provides editable text fields for the WIM image, SDI file, Windows loader, and Windows directory paths. It writes the ramdisk device and options objects into the BCD store. The header shows the Qt style, detected desktop toolkit, and current light/dark color scheme; Qt's native platform style and palette remain in control.

When editing a BCD device element, the device chooser has separate Partition, VHD/VHDX, and WIM/Ramdisk tabs. The Ramdisk tab accepts both the WIM image path and its `boot.sdi` path; VHD and Ramdisk selections update the paired application-device elements together.

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
