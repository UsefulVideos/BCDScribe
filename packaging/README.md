# AppImage packaging

The default CMake build creates the application and then runs the packaging script on Linux x86_64. The script builds a Release version, installs it into a temporary AppDir, bundles Qt and its platform plugin with linuxdeploy, and writes `dist/BCDScribe-x86_64.AppImage`. A successful run replaces the previous AppImage atomically.

Fresh BCD stores use the minimal hive fixture from [libguestfs/hivex](https://github.com/libguestfs/hivex/tree/master/images/minimal), the upstream project for libhivex. The fixture is embedded in compressed form so store creation works offline.

## Setup

Run the repository bootstrap script to install the build dependencies for Debian/Ubuntu, Fedora/RHEL, Arch, or openSUSE, and the x86_64 AppImage packaging tools into `~/.local/bin`:

```sh
./install-dependencies.sh
```

Use `./install-dependencies.sh --dry-run` to preview commands, or add `--build` to create the AppImage after setup. Package installation uses `sudo` where needed. Rerunning the script is safe; set `UPDATE_APPIMAGE_TOOLS=1` to refresh the AppImage utilities.

The packaging tools are published at [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy/releases), [linuxdeploy-plugin-qt](https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases), and [appimagetool](https://github.com/AppImage/appimagetool/releases). The `*_BIN` environment variables can point to downloaded executables whose filenames differ from the command names above. The script prefers `qmake6`; set `QMAKE` if Qt 6 uses a different executable path.

After setup, the regular CMake build creates or refreshes the AppImage. To package separately, run from the repository root:

```sh
./build-appimage.sh
```

The result is `dist/BCDScribe-x86_64.AppImage`. Alternate build and output locations can be selected with `BUILD_DIR` and `OUTPUT`. If the packaging tools use different executable names or paths, set `LINUXDEPLOY_BIN`, `QT_PLUGIN_BIN`, and `APPIMAGETOOL_BIN`.

AppImage bundles application libraries, not the Linux kernel or glibc. To support older distributions, build on the oldest Linux/glibc baseline you intend to support and test the resulting image on each target family. Some graphics and desktop integration libraries are also supplied by the host system.

## Automatic Git publishing

The default CMake build includes a `git_update` target. After the application builds, it stages non-ignored repository changes, commits them, and pushes the current branch to `origin`. Build outputs in `build/`, `build-appimage/`, and `dist/` are ignored. Publishing is skipped with a warning if `origin` is not configured or the checkout is detached; Git credentials and author identity must be set up for publishing. This target publishes all other staged and working-tree changes, not only CMake files.