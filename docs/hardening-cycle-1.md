# Codebase hardening cycle 1

## Baseline

- Date: 2026-07-12
- Branch: `codebase-hardening`
- Baseline commit: `980dd28a1e2449740ec0cbf4df73caeb7acbb3ff`
- Filesystem: ext2/ext3 family
- GCC: 14.2.0
- Clang: 19.1.7
- Meson: 1.7.0
- Ninja: 1.12.1
- GTK: 4.18.6
- GtkSourceView: 5.16.0
- GLib: 2.84.4

The clean GCC `debugoptimized` build completed without compiler warnings.
All 12 tests passed and both Meson benchmarks passed.

```text
Tests:       12/12 passed
Benchmarks:   2/2 passed
```

Baseline benchmark output on the current filesystem:

```text
Editable Preview
  100 KiB:     6.535 ms,  26,864 ranges
  1 MiB:      74.758 ms, 275,040 ranges
  2 MiB:     153.724 ms, 550,080 ranges

Atomic safe write
  100 KiB:     0.039 ms
  1 MiB:       0.096 ms
  2 MiB:       0.168 ms
```

The release executable built with `--buildtype=release -Db_lto=true` measured:

```text
Unstripped file size: 186,200 bytes
Stripped file size:   160,944 bytes
text/data/bss:        140,461 / 8,944 / 72 bytes
```

The GCC AddressSanitizer + UndefinedBehaviorSanitizer profile built cleanly and
all 12 tests passed with leak detection and halt-on-error enabled.

The Clang 19 `-Dwerror=true` baseline does not build. It reports two pre-existing
`-Wsign-conversion` errors in `src/document/recovery.c` where
`g_file_error_from_errno()` is passed to `g_set_error()`. These warnings predate
cycle 1 and are recorded rather than hidden.

Available analysis tools are `clang-19`, `clang-tidy-19`, and `clang-format`.
Valgrind, cppcheck, Xvfb, and xvfb-run are not installed, so their checks are not
part of this baseline.

## Results

Cycle 1 implementation and final verification results will be recorded here
after the data-safety commits are complete.
