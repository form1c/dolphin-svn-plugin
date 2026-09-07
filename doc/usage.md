# Dolphin SVN Plugin: Usage

| | |
|---|---|
| **Version** | 1.0.0 |
| **Date** | 2026-09-01 |
| **Describes** | Dolphin SVN Plugin 1.0.0 |
| **Audience** | Users of a working copy |
| **Not covered** | Installation. See `installation.md`. Working on the source. See `development.md`. |

This manual describes the actions the plugin adds to Dolphin and the state it shows on the icons. Each action runs the Subversion command line client and reports its result in a progress dialog.

---

## Contents

1. [Status overlays](#1-status-overlays)
2. [The SVN submenu](#2-the-svn-submenu)
3. [Everyday actions](#3-everyday-actions)
4. [History and differences](#4-history-and-differences)
5. [Branching, switching and merging](#5-branching-switching-and-merging)
6. [Conflicts](#6-conflicts)
7. [Patches](#7-patches)
8. [Repository browser](#8-repository-browser)
9. [Properties and the ignore list](#9-properties-and-the-ignore-list)
10. [Settings](#10-settings)

---

## 1. Status overlays

The overlay plugin marks files and folders in a working copy with a small icon that shows the Subversion state. The states are normal, modified, added, deleted, conflicted, unversioned and further cases. A folder takes the strongest state of its content, so a modified file inside shows on the folder as well.

The overlays are cached for a short time to keep the file view fast. After a change made outside Dolphin, run the Refresh Status Overlays action in the SVN submenu to force an update.

## 2. The SVN submenu

Right-click a file or a folder to open the context menu. The SVN submenu holds the actions that apply to the selection.

The submenu differs by context. Inside a working copy it shows the full set of actions. On a folder that is not a working copy it shows checkout, export, the repository browser and the option to create a repository. This keeps the menu short and relevant to the selection.

## 3. Everyday actions

| Action | Effect |
|---|---|
| Update | Brings the working copy to the latest revision or to a chosen revision |
| Commit | Sends the selected changes to the repository with a log message. Unversioned items can be added in the same step |
| Add | Puts new files under version control |
| Delete | Removes files from version control, with an option to keep the local copy. When an item has local modifications, it offers to force the removal |
| Rename or Move | Moves a versioned item and keeps its history |
| Revert | Discards local changes |
| Cleanup | Repairs a locked or interrupted working copy |
| Get Lock and Release Lock | Take and release a lock on a file |
| Check for Modifications | Shows the local state of the working copy in a dialog |

The commit dialog shows the pending changes as a folder tree, so files sit under the folders that contain them, rooted at the item you invoked the commit on. A checkbox on each entry selects what to commit, and checking or unchecking a folder applies to its whole subtree. Unversioned items are unchecked by default and are added in the same step once you check them. A large unversioned folder appears as one row instead of each single file, which keeps the dialog fast to open, and checking that row takes the whole folder along. An item that is missing on disk, a file or a folder, can be scheduled for deletion from the tree through the right-click menu, which also works on several selected items at once.

## 4. History and differences

The Show Log action opens a dialog with three panes. It lists the revisions, the changed paths of the selected revision and the log message. The options stop on copy, include merged revisions and show only affected paths control the query. From the log you reach diff, blame, revert, checkout, export and merge for a chosen revision.

The diff actions show changes through the external diff tool. You compare the working copy against BASE, against a chosen revision or between two revisions. The Blame action shows the last change per line with the revision and the author.

## 5. Branching, switching and merging

The Branch or Tag action creates a copy in the repository through a single server-side commit. The Switch action points the working copy at another URL. The Relocate action records a pure URL change without fetching anything, for the case that only the repository address changed.

The Merge action offers a range merge and a tree merge. The range merge applies a set of revisions from a source URL and uses merge tracking to skip revisions that are already merged. A pick dialog lists the eligible revisions. The tree merge applies the difference between two trees. After a merge the plugin checks for conflicts and offers the conflict dialog.

## 6. Conflicts

The conflict dialog handles text and tree conflicts. For a text conflict you choose the working version, your version or the incoming version, or you postpone the decision. A three-way merge through the external tool is available for a file in a text conflict. It opens base, mine and theirs and writes the result back to the working file. On a clean save the plugin offers to mark the conflict resolved.

After a merge that stopped on a conflict, resolve the conflict and use the continue action. Merge tracking makes the repeated merge a no-op for the revisions that are already applied, so only the remaining revisions are merged.

## 7. Patches

The Create Patch action writes the selected changes to a patch file. It lists the changed files with a checkbox each, all selected by default, and offers a target file that defaults to the working copy name with a `.patch` suffix. The patch is produced with `svn diff --git`, so it carries markers for added, deleted and binary files.

The Apply Patch action applies a patch file to the working copy. It offers a dry-run preview that shows the effect without changing anything, and a reverse option that undoes the changes in the patch.

> **Attention** The paths in a Subversion patch are relative to the repository root. A patch from a working copy of a branch such as `^/trunk` carries that prefix in its paths. The Apply Patch dialog detects how many leading path components to strip and fills in the value. Correct the value by hand for a patch from a different layout. Hunks that do not fit are rejected and written next to the affected file as a `.svnpatch.rej` file, and the rest of the patch is still applied. The plugin reports the rejected files after the run.

## 8. Repository browser

The repository browser shows the content of a repository URL. From it you check out, export, copy, move and edit properties. A copy or move runs as a single commit. The browser also serves as a URL picker for the branch, switch and merge dialogs.

## 9. Properties and the ignore list

The Properties action reads and sets Subversion properties. A completion list offers the common `svn:` properties. The Add to Ignore List action adds an entry to the `svn:ignore` property of the containing folder, either by the exact name or by the file extension, without opening the property editor.

## 10. Settings

The Settings dialog holds the path to the `svn` binary, the choice and order of the diff tool, advanced options such as the synchronous timeout and the overlay cache time, and the default values for the dialogs. It also shows the installed plugin version under About. Changes take effect on the next action.
