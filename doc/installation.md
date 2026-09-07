# Dolphin SVN Plugin: Installation

| | |
|---|---|
| **Version** | 1.0.0 |
| **Date** | 2026-08-19 |
| **Describes** | Dolphin SVN Plugin 1.0.0 |
| **Audience** | Users and packagers on Linux with KDE Plasma 6 |
| **Not covered** | Using the plugin. See `usage.md`. Working on the source. See `development.md`. |

This guide covers the complete path from the requirements to a loaded plugin. It describes the package install for end users and the development install for a fast build and test cycle.

---

## Contents

1. [Requirements](#1-requirements)
2. [Build from source](#2-build-from-source)
3. [Install as a Debian package](#3-install-as-a-debian-package)
4. [Development install](#4-development-install)
5. [Activate the plugin](#5-activate-the-plugin)
6. [External diff and merge tool](#6-external-diff-and-merge-tool)
7. [Remove the plugin](#7-remove-the-plugin)
8. [Troubleshooting](#8-troubleshooting)

---

## 1. Requirements

The plugin targets KDE Plasma 6 with Qt 6 and drives the Subversion command line client. Install the build tools, the Qt 6 base libraries, the KDE Frameworks 6 development packages and Subversion. On Debian or Kubuntu:

```bash
sudo apt install cmake g++ gettext extra-cmake-modules \
    qt6-base-dev \
    libkf6coreaddons-dev libkf6kio-dev libkf6i18n-dev libkf6notifications-dev \
    subversion
```

The runtime requires the same Qt 6 and KDE Frameworks 6 libraries and the `svn` binary. The Debian package build detects the library dependencies on its own and writes them into the package.

## 2. Build from source

Run the build from the repository root:

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

The build produces two shared objects:

1. `build/bin/kf6/kfileitemaction/dolphinsvnplugin.so` provides the context menu.
2. `build/bin/kf6/overlayicon/svnoverlayplugin.so` provides the status overlay icons.

## 3. Install as a Debian package

The package build compiles a release build, stages the files, detects the library dependencies from the linked libraries and produces a `.deb`. Root rights are not required for the build.

```bash
./scripts/build-deb.sh            # version 1.0.0
./scripts/build-deb.sh 1.2.0      # a custom version number
```

The result is `release/dolphin-svn-plugin_<version>_<arch>.deb`. Install it with apt so the dependencies are resolved:

```bash
sudo apt install ./release/dolphin-svn-plugin_1.0.0_amd64.deb
```

> **Attention** Remove a previous development install before installing the package. Two copies in the system lead to unpredictable behaviour. Run `./scripts/uninstall-dev.sh` first.

## 4. Development install

The development install compiles the current build and copies both plugins into the system plugin directory. It is meant for a fast edit and test cycle during development.

```bash
./scripts/install-dev.sh
```

The script asks for the sudo password when it needs to write to the system directory. Remove the development install again:

```bash
./scripts/uninstall-dev.sh
```

## 5. Activate the plugin

Restart Dolphin so it loads the new plugins:

```bash
pkill dolphin; dolphin &
```

Open Settings, Configure Dolphin, Context Menu and enable the SVN entry if it is not active. The status overlays appear on the next directory refresh. Use the Refresh Status Overlays entry in the SVN submenu to force an update after an external change.

## 6. External diff and merge tool

The plugin shows differences and resolves conflicts through an external tool. It searches for a tool in this order: AnGscheidrDiffer, Meld, KDiff3, Kompare. The order is configurable under SVN, Settings, Diff Tool. Install at least one of these tools for the diff and merge actions.

## 7. Remove the plugin

Remove a package install:

```bash
sudo apt remove dolphin-svn-plugin
```

Remove a development install:

```bash
./scripts/uninstall-dev.sh
```

Both restore the plugin cache. Restart Dolphin afterwards.

## 8. Troubleshooting

| Symptom | Cause | Action |
|---|---|---|
| The SVN submenu does not appear | The plugin is not loaded or the context menu entry is disabled | Restart Dolphin, then enable the entry under Settings, Configure Dolphin, Context Menu |
| No overlay icons on files | The overlay plugin is not loaded or the cache is stale | Reinstall, run the Refresh Status Overlays action, restart Dolphin |
| Actions report that svn was not found | The Subversion client is missing or not on the path | Install the `subversion` package |
| Two copies behave inconsistently | A development install and a package install are both present | Run `./scripts/uninstall-dev.sh`, then keep only the package |
