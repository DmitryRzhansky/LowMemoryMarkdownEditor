# Codebase hardening cycle 2

## Baseline

- Date: 2026-07-12
- Branch: `main`
- Baseline commit: `74c8b98a56e33d8630daf8be3894837e6144b5c8`
- Baseline working tree: clean
- GCC: 14.2.0
- Clang: 19.1.7
- Meson: 1.7.0
- Ninja: 1.12.1

The historical cycle-1 results were repeated before the first production
change. The clean GCC `debugoptimized --werror` build passed, all 14 tests
passed with no skipped tests, and all 5 benchmarks passed.

## Changes

### Recovery listing

`lmme_recovery_list()` is now read-only. Missing, corrupt, truncated, or
unsupported metadata no longer causes unreferenced recovery generations to be
deleted. Explicit removal and post-commit replacement cleanup remain in place.
The recovery v2 writer and its `recovery_file` and `previous_recovery_file`
fields are unchanged.

### File-tree lifetime

Tree rows now own stable path, name, and kind values instead of retaining a
borrowed `LmmeFileNode *`. Child expansion resolves the current workspace node
before consulting the cached store. Cached stores and monitors are pruned when
the path disappears or stops being a directory.

The regression fixture retains the old row and child store for `a/b`, refreshes
`a`, and invokes child expansion through that exact old row. It also covers
directory deletion and a directory changing into a Markdown file. The test
uses GTK tree-model objects without a GDK renderer, so it cannot be skipped for
lack of a display.

Sanitizer verification also exposed that `gtk_tree_list_row_get_item()` returns
an owned reference. All file-tree call sites now release that reference.

### Recovery health and close policy

Documents track recovery health independently from save and external-disk
state. The status bar can therefore show combinations such as `Conflict |
Recovery failed`. Recovery failure is cleared only after a durable snapshot or
durable save of the current buffer revision.

External conflicts keep Save As, Overwrite Disk, and Keep Local Changes
available. Reload from Disk is available only when the current local buffer has
a durable recovery snapshot. External deletion never offers Reload.

Close preparation permits closing only after a durable save, a durable
recovery snapshot, or explicit `Discard Local Changes`. Discard is recorded as
a pending disposition and applied only during close commit. Bulk close remains
two-phase: one failed preparation closes no tabs.

## Commits

```text
62eee9c fix: preserve unreferenced recovery generations
9a938f3 fix: resolve file-tree nodes after branch refresh
5a7acbd fix: surface document recovery write failures
f2ab4a7 fix: release owned file-tree row items
```

## Final verification

```text
GCC 14.2 debugoptimized --werror:       pass
GCC tests:                              15/15 pass, 0 skipped
Clang 19 debugoptimized --werror:       pass
Clang tests:                            15/15 pass, 0 skipped
ASan + UBSan + leak detection:          15/15 pass, 0 skipped
File-tree sanitizer gate:               pass
Meson benchmarks:                        5/5 pass
Release + LTO build:                    pass
git diff --check:                       pass
```

Release executable measurements:

```text
Unstripped:       200,568 bytes
Stripped:         173,424 bytes
text/data/bss:    155,788 / 9,168 / 96 bytes
Staged payload:   200,568 bytes
Payload limit:    less than 20 MiB
```

Direct dynamic dependencies remain GTK 4, GtkSourceView 5, GdkPixbuf,
Graphene, GLib/GIO/GObject, and libc. No WebKit/WebView, database engine,
scripting runtime, or new project dependency was introduced.

Valgrind and Xvfb are not installed. The file-tree sanitizer regression does
not require Xvfb. The repository still has no checked-in clang-format or
clang-tidy configuration, so no repository-wide formatting or unspecific
static-analysis rewrite was performed.
