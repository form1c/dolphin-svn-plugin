# Changelog

All notable changes to this project are documented in this file. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-08-19

### Added

- Status overlay icons for Subversion working copies, with a short-lived cache and a manual refresh action.
- Context menu actions for update, commit, revert, add, delete, rename and move, cleanup, lock and unlock.
- Log dialog with three panes, stop on copy, include merged revisions and affected paths, and diff, blame and revert from the log.
- Diff against BASE, against a chosen revision and between two revisions, shown through an external diff tool.
- Branch and tag through a server-side copy, switch and relocate.
- Range merge and tree merge with the common options, merge tracking and a pick dialog for eligible revisions.
- Text and tree conflict resolution, with an optional three-way merge through an external tool.
- Create a patch from the selected changes and apply a patch with a dry-run preview and a reverse option.
- Repository browser with checkout, export, copy, move and property editing.
- Read and set Subversion properties, including a helper to add entries to the ignore list.
- Debian package build and a development install script.
