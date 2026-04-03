# cc-clean

[![CI](https://github.com/minkingdev-cmd/cc-clean/actions/workflows/ci.yml/badge.svg)](https://github.com/minkingdev-cmd/cc-clean/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/minkingdev-cmd/cc-clean)](https://github.com/minkingdev-cmd/cc-clean/releases)

[中文](./README.md) | English

`cc-clean` is a cross-platform cleanup utility for **fully uninstalling Claude Code**.

It scans, backs up, removes, and restores Claude Code related local data and platform-specific integration entries, helping users perform a complete and controlled uninstall workflow.

## What it does

- Scans Claude Code related local data
- Cleans common leftover content, including:
  - configuration directories
  - cache directories
  - log directories
  - session history
  - local install directories and the native installer
  - shell config injections and cached completions
  - Linux mimeapps / deep-link associations
  - browser integrations (Claude in Chrome / deep-link)
  - VS Code / Cursor / Windsurf extension directories
  - JetBrains plugin directories
  - global npm installation leftovers
  - credentials and platform-specific system entries
- Creates backups before deletion
- Restores from backups with the `restore` command
- Provides JSON output for automation and scripting
- Includes cross-platform tests and CI

## Supported platforms

- macOS
- Linux
- Windows
- x86_64 / aarch64

## Recommended workflow

1. Run `check` to inspect local leftovers
2. Run `clean --backup-dir <dir>` to back up and remove them
3. Run `restore --backup-dir <dir>` when rollback is needed

## Get the tool

### Build from source

```bash
cmake -S . -B build
cmake --build build
```

The executable is typically located at:

```bash
./build/cc-clean
```

### Download from Release

- Releases page: <https://github.com/minkingdev-cmd/cc-clean/releases>
- Current version: <https://github.com/minkingdev-cmd/cc-clean/releases/tag/v0.1.2>
- Checksum file: `SHA256SUMS.txt`

Future tag releases are built and uploaded automatically by GitHub Actions.

The current source tree also covers more uninstall leftovers, including local installs, the native installer, shell config injections, Linux mimeapps associations, Claude in Chrome Native Host, IDE plugin directories, global npm installation leftovers, and the Linux deep-link desktop entry.

`v0.1.2` currently includes:

- `cc-clean-v0.1.2-linux-x86_64.tar.gz`
- `cc-clean-v0.1.2-linux-x86_64.zip`
- `cc-clean-v0.1.2-macos-aarch64.tar.gz`
- `cc-clean-v0.1.2-macos-aarch64.zip`
- `cc-clean-v0.1.2-windows-x86_64.zip`
- `SHA256SUMS.txt`

## Usage examples

### Check leftovers

```bash
./build/cc-clean check
```

### Clean and back up

```bash
./build/cc-clean clean --backup-dir ./backup -y
```

### Purge all known Claude Code traces

```bash
./build/cc-clean clean --purge-all
```

Notes:

- `--purge-all` removes all known Claude Code traces, including config, cache, logs, history, install leftovers, shell/browser/IDE integrations, and system-level items
- `--purge-all` does not require an extra `--allow-unsafe-purge`
- even with `-y`, execution still requires reviewing the plan and manually typing `PURGE-ALL`
- use `--dry-run` first if you only want to inspect the plan

### Restore from backup

```bash
./build/cc-clean restore --backup-dir ./backup -y
```

### JSON output

```bash
./build/cc-clean check --json
```

## Tests

```bash
cd build
cmake ..
ctest --output-on-failure
```

Run a single test by name:

```bash
ctest -R cfg_restore_json --output-on-failure
```

Run tests by label:

```bash
ctest -L json -V
ctest -L restore -V
ctest -L safety -V
ctest -L dangerous -V
```

## Repository layout

- `csrc/cc_clean_main.c / cc_clean_cli.c / cc_clean_core.c / cc_clean_report.c`: main C implementation
- `CMakeLists.txt`: cross-platform build configuration
- `BUILDING.md`: build, test, and CTest usage notes
- `CHANGELOG.md`: project change log
- `README.md`: Chinese project overview
- `LICENSE`: project license
- `scripts/test_cc_clean_posix.sh`: macOS / Linux regression tests
- `scripts/test_cc_clean_windows.ps1`: Windows regression tests
- `scripts/cc_clean.py`: earlier Python reference implementation
- `docs/BUILDING.original.md`: preserved historical build note
- `docs/releases/`: release notes
- `docs/releases/TEMPLATE.md`: release note template for new versions

## Docs

- [Build and test guide](./BUILDING.md)
- [Changelog](./CHANGELOG.md)
- [Contributing](./CONTRIBUTING.md)
- [Security policy](./SECURITY.md)
- [Release notes](./docs/releases/v0.1.2.md)

## License

This project is released under the MIT License.
