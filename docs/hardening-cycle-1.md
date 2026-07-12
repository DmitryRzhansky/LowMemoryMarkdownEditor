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

The delivery strategy changed after the baseline at the user's explicit
request: the hardening commits were applied directly to main, without
hardening branches or Pull Requests. The implementation range after the
baseline is dc5a00e..ee3f1ee; every technical stage remains an individual
commit.

### Data safety

- Safe write has deterministic per-invocation fault injection for temporary
  creation, fchmod, write, file fsync, close, rename, directory open, and
  directory fsync.
- Safe write returns NOT_COMMITTED, COMMITTED_DURABLE, or
  COMMITTED_NOT_DURABLE, plus the fingerprint of a committed replacement.
  All production callers use the typed API; no boolean compatibility wrapper
  remains.
- Workspace save-target validation checks lexical containment, the resolved
  parent, and an existing leaf, rejecting intermediate and leaf symlink
  escapes.
- Recovery v2 uses <hash>-<uuid>.recover generations and commits an update by
  replacing metadata containing recovery_file. Readers remain compatible with
  v1 metadata and legacy .path records; a subsequent successful write migrates
  v1 data to v2.
- Save, Save As, and Overwrite use one immutable buffer snapshot and
  prepare/write/commit state transitions. A pre-commit failure leaves document
  state unchanged; a post-rename durability failure records that the target
  changed, keeps recovery, and does not schedule an automatic retry.
- Rename prepares all open-document destinations and recovery remaps before the
  filesystem rename. Failed preparation or rename keeps old paths and recovery;
  successful rename commits the prepared in-memory paths and monitor changes.

The deterministic fault matrix produced these verified outcomes:

~~~text
temp create / fchmod / write / file fsync / close / rename
  outcome: NOT_COMMITTED
  target:  old content (or absent for a new target)

directory open / directory fsync
  outcome: COMMITTED_NOT_DURABLE
  target:  new content and committed fingerprint

all fault points
  temporary save files: 0
  open descriptors for the fixture: 0
~~~

Recovery update tests inject every fault into both the recovery generation
write and metadata write. The last metadata-selected recovery always remains
readable; post-rename metadata uncertainty exposes the new usable generation
without discarding the previous one.

### Lifecycle and state

- Bulk tab close is two-phase: all documents are prepared before any tab is
  removed.
- Workspace switching scans and validates the candidate before close
  preparation and changes no current state when preparation is rejected.
- File loading is centralized, bounded before and after the read, and validates
  UTF-8.
- External file-monitor decisions are a pure state transition function covered
  independently from GTK/GIO callbacks.
- File-tree selection and context paths are cleared when they become stale.

### Performance and contracts

- Editable Preview active-line marker lookup uses sorted ranges and binary
  lower-bound lookup. The active-line benchmark remains effectively constant
  as the document grows from 10,000 to 50,000 lines.
- File-tree refresh updates the dirty branch's GListStore in place instead of
  rebuilding the full tree and its monitors.
- Repeatable benchmarks cover bounded reads, file copies, workspace root scans,
  PNG encoding, recursive delete, preview updates, tree refresh, and safe
  writes.
- Among the stage-13 I/O candidates, only PNG encoding exceeded the 50 ms
  operation threshold. Clipboard PNG
  conversion and encoding now run in a cancellable worker after GDK copies the
  texture pixels on the main thread. Stable document IDs prevent completion
  from targeting a closed tab, and cancellation removes a created destination.
  gdk-pixbuf-2.0 is now an explicit build dependency but was already a GTK
  runtime dependency, so this adds no new runtime component.
- One-shot 10,000-entry workspace scanning and recursive deletion stayed below
  50 ms. They were not moved off-thread because they did not demonstrate a
  repeated interactive callback stall, and async destructive deletion would
  weaken lifecycle ordering.
- Ownership, nullable outputs, committed-state semantics, and array element
  ownership are documented in the touched headers.
- lmme_validate_basename(NULL, error) now reports G_FILE_ERROR_INVAL instead of
  dereferencing NULL.

Final GCC debugoptimized benchmark output:

~~~text
Editable Preview full parse
  100 KiB:     6.496 ms,  26,864 ranges   (baseline 6.535 ms)
  1 MiB:      71.185 ms, 275,040 ranges   (baseline 74.758 ms)
  2 MiB:     158.017 ms, 550,080 ranges   (baseline 153.724 ms)

Editable Preview active-line update
  10,000 lines: 20.717 us/move
  50,000 lines: 21.888 us/move

Dirty tree branch refresh
  100 directories x 100 files: 2.557 ms, 1 monitor

Atomic safe write
  100 KiB:     0.043 ms   (baseline 0.039 ms)
  1 MiB:       0.099 ms   (baseline 0.096 ms)
  2 MiB:       0.177 ms   (baseline 0.168 ms)

Main-loop candidates
  bounded read, 10 MiB:        1.583 ms median / 3.405 ms p95
  copy, 10 MiB:                1.898 ms median / 2.481 ms p95
  root scan, 10,000 entries:  31.843 ms median / 32.944 ms p95
  recursive delete, 10,000:   35.106 ms

PNG, 10 MiB raw
  synchronous encode: 115.745 ms median
  async scheduling:     3.007 ms median
  async completion:   102.634 ms median

PNG, 51.84 MiB raw
  synchronous encode: 609.040 ms median
  async scheduling:    26.001 ms median
  async completion:   531.018 ms median
~~~

Safe-write timings include the new pre-rename fstat fingerprint used to report
the identity of a committed replacement. The absolute increase is 0.004-0.009
ms on this filesystem; percentages at the two sub-0.2 ms fixtures are dominated
by single-run filesystem noise.

The remaining 26 ms scheduling cost at 51.84 MiB is the main-thread
GdkTexture download required before worker access. Moving the GDK object into
the worker would violate GTK/GDK threading rules; normal 1 MiB clipboard images
schedule in 0.322 ms.

### Final verification

~~~text
GCC 14.2 debugoptimized, clean build:  pass, no warnings
Tests:                                 14/14 pass
Benchmarks:                             5/5 pass
Clang 19, -Dwerror=true:               pass
Clang tests:                           14/14 pass
GCC ASan + UBSan + leak detection:     14/14 pass
git diff --check:                      pass
~~~

The Clang sign-conversion errors recorded in the baseline are fixed. The same
GLib enum-to-gint boundary was made explicit in the remaining safe-write,
workspace, config, and image paths.

The final release executable built with
--buildtype=release -Db_lto=true measured:

~~~text
Unstripped file size: 200,472 bytes   (baseline 186,200)
Stripped file size:   173,424 bytes   (baseline 160,944)
text/data/bss:        156,832 / 9,168 / 96 bytes
baseline sections:   140,461 / 8,944 / 72 bytes
~~~

Valgrind, cppcheck, Xvfb, and xvfb-run remain unavailable. Consequently no
Valgrind pass or headless full-GUI smoke test was run. clang-tidy-19 and
clang-format are installed, but the repository has no checked-in configuration
for either; no repository-wide style rewrite or unspecific tidy pass was
performed.
