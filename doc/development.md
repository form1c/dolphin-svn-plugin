# Dolphin SVN Plugin: Development

| | |
|---|---|
| **Version** | 1.0.0 |
| **Date** | 2026-09-01 |
| **Describes** | Dolphin SVN Plugin 1.0.0 |
| **Audience** | Development on the source |
| **Not covered** | Installing a release. See `installation.md`. Using the plugin. See `usage.md`. |

This manual describes the layout of the source, the build, the test suite and the design decisions behind the plugin. Where a decision looks arbitrary, the reason is given next to it.

---

## Contents

1. [Outline](#1-outline)
2. [Source layout](#2-source-layout)
3. [Build](#3-build)
4. [Backend tests](#4-backend-tests)
5. [Architecture](#5-architecture)
6. [Coding conventions](#6-coding-conventions)
7. [Adding an action](#7-adding-an-action)
8. [Manual tests](#8-manual-tests)

---

## 1. Outline

The plugin consists of two parts. The context menu part is a `KAbstractFileItemActionPlugin` that adds the SVN submenu to Dolphin. The overlay part is a `KOverlayIconPlugin` that marks the working copy state on the icons. Both parts drive the Subversion command line client through a shared backend class.

## 2. Source layout

| Path | Content |
|---|---|
| `svn/` | The Subversion backend. `SvnManager` wraps the `svn` client and parses its XML output. `svnrevrange.*` holds a GUI-free helper for revision ranges. |
| `plugin/` | The context menu plugin and its dialogs. Each SVN action has one dialog class. `svnuihelpers.*` holds shared helpers, and `svntreecheck.*` and `svncommittree.*` hold the KF6-free commit tree logic that the backend tests exercise. |
| `overlays/` | The status overlay icon plugin. |
| `scripts/` | Build, development install and package scripts. |
| `testing/` | The backend test suite and a repository creator for manual tests. |
| `doc/` | Public documentation. |
| `img/` | Public images. |

## 3. Build

Run the build from the repository root. The build must be free of warnings.

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

The build products live under `build/`, `build-deb/` and `release/`, all of which `.gitignore` excludes. Remove them at any time to start clean:

```bash
rm -rf build build-deb release
```

### Version

The plugin version is defined in a single place, the `project()` command at the top of the top-level `CMakeLists.txt`:

```cmake
project(dolphin-svn-plugin VERSION 1.0.0)
```

To release a new version, change the number there and nowhere else. It flows automatically to three places:

- the compiled plugin, through the `DOLPHIN_SVN_PLUGIN_VERSION` compile definition (see `plugin/CMakeLists.txt`), where the Settings dialog shows it under **About**,
- the Debian package, whose file name and control field `scripts/build-deb.sh` reads back from the same `CMakeLists.txt`,
- this document, which states the current version in its header table.

## 4. Backend tests

The backend tests exercise the Subversion backend against real `file://` repositories that the fixtures create at run time. Each test starts from a fresh repository state, so the suites are independent of each other and of the developer's own working copies.

```bash
cmake -S testing/backend-tests -B testing/backend-tests/build
cmake --build testing/backend-tests/build -j$(nproc)
ctest --test-dir testing/backend-tests/build --output-on-failure
```

All suites must pass. The test project compiles the backend sources from `svn/` directly rather than copying them, so the tested code cannot drift from the plugin build.

| Suite | Covers |
|---|---|
| `tst_smoke` | The build and the fixtures |
| `tst_parsestatus`, `tst_parselog` | XML parsing of status and log |
| `tst_conflicts` | Text and tree conflict detection |
| `tst_merge`, `tst_mergemessage` | Merge and the merge message builder |
| `tst_diffexport` | Diff, change diff and export with a peg revision |
| `tst_asyncops`, `tst_syncmisc` | Asynchronous operations and synchronous helpers |
| `tst_repoops` | Direct repository operations |
| `tst_revrange` | The revision range helper |
| `tst_remotestatus` | Remote status with `svn status -u` |
| `tst_patch` | Create patch, apply patch and the strip level detection |
| `tst_treecheck` | The commit tree check-state cascade (`svntreecheck`) |
| `tst_committree` | The commit tree structure: folder nesting, grouping folders, missing directories and collapsing large unversioned folders (`svncommittree`) |
| `tst_settings` | The settings accessors over `QSettings`, isolated from the developer's own configuration |

The last three suites cover widget-level and settings logic that was factored out into KF6-free units (`svntreecheck`, `svncommittree`, the `svnsettings` header), so they build against Qt Widgets alone and run headless through the offscreen platform.

## 5. Architecture

`SvnManager` is the single point that runs the `svn` client. It offers two kinds of methods.

1. Synchronous methods such as `status`, `info`, `log` and `cat` run in the calling thread and block until the result is ready. They suit short queries.
2. Asynchronous methods such as `updateAsync`, `commitAsync` and `mergeRangeAsync` start a background process and send signals for progress and completion. The dialogs run them behind a progress dialog.

The backend keeps the client's diagnostic messages in English on every call, through `LC_MESSAGES=C`, so the textual output is parseable regardless of the user's language. At the same time it keeps a UTF-8 character set, so non-ASCII text in commit and other messages is accepted. It reads the machine-readable `--xml` output where the client offers it.

The context menu is built in `SvnPlugin::actions`. Each action is a `QAction` with its own dialog class. The dialogs share helpers from `svnuihelpers.*`, among them the progress runner and the URL picker.

> **Note** A patch produced with `svn diff --git` carries paths relative to the repository root. The apply path detects the strip level from the patch and the target working copy. See `SvnManager::detectPatchStripLevelSync`.

## 6. Coding conventions

1. Identifiers, comments, test names and routes are written in English.
2. User-facing strings pass through `i18n` for translation.
3. Text in dialogs and documents is written in the language of the audience.
4. A change builds without warnings and keeps all backend suites green.
5. A fixed backend bug or a new backend function comes with a test where a test is possible.

## 7. Adding an action

1. Add the backend method to `SvnManager`, synchronous for a query and asynchronous for an operation that changes the repository or the working copy.
2. Add a backend test that exercises the method against a fixture.
3. Add the dialog class under `plugin/` and register it in the plugin `CMakeLists.txt`.
4. Add the `QAction` in `SvnPlugin::actions` and run the operation behind the progress runner.

## 8. Manual tests

The repository creator under `testing/svn-repo-creator` builds a local demo repository with a trunk, branches and tags and a small C++ demo application. Use it to set up a working copy for manual tests of the dialogs.
