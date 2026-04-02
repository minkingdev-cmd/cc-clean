# cc-clean

[![CI](https://github.com/minkingdev-cmd/cc-clean/actions/workflows/ci.yml/badge.svg)](https://github.com/minkingdev-cmd/cc-clean/actions/workflows/ci.yml)

`cc-clean` is a cross-platform cleanup utility for **fully uninstalling Claude Code**.

It scans, backs up, removes, and restores Claude Code related local data and platform-specific integration entries, helping users perform a complete and controlled uninstall workflow.

## What it does

- Scans Claude Code related local data
- Cleans common leftover content, including:
  - configuration directories
  - cache directories
  - log directories
  - session history
  - plugin directories
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

## Typical use cases

- Check whether Claude Code leftovers still exist on a machine
- Perform a full Claude Code cleanup
- Restore content after cleanup if needed
- Integrate uninstall and cleanup workflows into automation scripts

## Quick start

### Build

```bash
cmake -S . -B build
cmake --build build
```

The executable is typically located at:

```bash
./build/cc-clean
```

### Check leftovers

```bash
./build/cc-clean check
```

### Clean and back up

```bash
./build/cc-clean clean --backup-dir ./backup -y
```

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
- `README.md`: Chinese project overview
- `LICENSE`: project license
- `scripts/test_cc_clean_posix.sh`: macOS / Linux regression tests
- `scripts/test_cc_clean_windows.ps1`: Windows regression tests
- `scripts/cc_clean.py`: earlier Python reference implementation
- `docs/BUILDING.original.md`: preserved historical build note
- `docs/releases/`: release notes

## License

This project is released under the MIT License.
