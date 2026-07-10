# Codebase hardening results

- Date: 2026-07-10
- Branch: `refactor/codebase-hardening`
- Baseline: `b71adf928989436fe98b889586a0e4c4fcbed679`

## Outcome

The hardening pass keeps the C17, GTK 4, GtkSourceView 5 and Meson/Ninja
stack. It keeps the neutral-gray UI, single-pane Editable Preview, GKeyFile
configuration at `~/.config/lmme/config.ini`, and permanent deletion. No new
runtime dependency was added; the unused libcmark dependency was removed.

The main data-safety changes are:

- durable atomic writes with a unique same-directory temporary file,
  permission preservation, file and directory `fsync()`, and error cleanup;
- versioned recovery metadata tied to the original path, workspace and file
  fingerprint, with legacy `.path` import;
- one guarded shutdown/prepare-close path for window close and Quit;
- stable document IDs and cancellable image clipboard operations;
- subtree-aware tab remapping for rename and tab/recovery cleanup only after a
  successful delete;
- persistent external-change and external-delete states that block ordinary
  autosave and Save until the conflict is resolved;
- workspace-root, symlink and mount/device checks before permanent deletion.

The main functional and performance changes are:

- explicit Find/Replace commands and deterministic single-wrap navigation;
- cached, debounced word count that is independent of cursor movement;
- one debounced structural Editable Preview parse for the active document and
  line-local marker updates on cursor movement;
- linear UTF-8 byte-to-character range conversion without a file-sized lookup
  table;
- non-recursive root workspace scan, load-on-expand directories, targeted
  parent refresh and monitors only for loaded directories;
- a single validated command catalog for actions and accelerators;
- removal of the deprecated tree/dialog code paths and unused Markdown module.

## Verification

All checks below were run from this branch:

```text
GCC 14.2 regular build:             pass
GCC 14.2 release + LTO + strip:     pass
GCC 14.2 debug + -Werror:           pass
AddressSanitizer + UBSanitizer:     12/12 tests pass
Regular test profile:               12/12 tests pass
Release test profile:               12/12 tests pass
Meson benchmarks:                   2/2 pass
git diff --check:                   pass
```

The tests cover safe writes, config, recovery metadata and restoration,
fingerprints, command uniqueness, preview ranges, search/replace, document
lifecycle and path remapping, image target identity, destructive workspace
boundaries, and lazy loading of a 10,000-file fixture. The lifecycle test also
asserts that cursor movement does not increment the full-preview-parse count.

## Measurements

Release executable on x86-64:

```text
File size:       186,576 bytes (0.18 MiB)
text/data/bss:   143,339 / 9,088 / 72 bytes
Limit:           20 MiB
```

Direct dynamic dependencies remain GTK 4, GtkSourceView 5, GLib/GIO/GObject
and their platform dependencies. `ldd` no longer reports libcmark.

Single-run release benchmarks on the current filesystem:

```text
Editable Preview
  100 KiB:     7.824 ms,  26,864 ranges
  1 MiB:      95.880 ms, 275,040 ranges
  2 MiB:     190.787 ms, 550,080 ranges

Atomic safe write
  100 KiB:     0.056 ms
  1 MiB:       0.298 ms
  2 MiB:       0.379 ms
```

The write measurements support keeping the current debounced synchronous
autosave model for this pass. These benchmarks are diagnostic, not unit-test
thresholds.

## Environment limitations

Clang and Xvfb are not installed, so the requested Clang build, automated GUI
smoke test, startup-to-window timing and trustworthy idle RSS measurement could
not be run. `clang-format` is installed, but the repository has no
`.clang-format`; applying its default LLVM style would reformat the entire
codebase, so formatting was checked through the existing project style and the
strict warning-clean builds instead.
