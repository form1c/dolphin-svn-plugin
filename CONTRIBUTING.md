# Contributing

Thank you for your interest in improving the Dolphin SVN Plugin.

## Reporting issues

Open an issue with the following information:

1. The distribution, the Plasma version and the Qt version.
2. The output of `svn --version --quiet`.
3. The steps to reproduce, the expected result and the actual result.

## Changes

1. Keep each change focused on one topic.
2. Build without warnings with `cmake --build build`.
3. Run the backend tests with `ctest --test-dir testing/backend-tests/build`. All suites must pass.
4. Add a test for a fixed bug or a new backend function where a test is possible.
5. Write identifiers, comments and test names in English. User-facing strings pass through `i18n`.

The source layout and the test workflow are described in [doc/development.md](doc/development.md).

## Commit messages

Write a short summary line in the imperative, followed by a blank line and a body that explains the reason for the change.

## License

By contributing you agree that your contribution is licensed under the MIT License, as stated in [LICENSE](LICENSE).
