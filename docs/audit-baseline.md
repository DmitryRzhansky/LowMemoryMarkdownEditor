# Codebase hardening baseline

- Baseline commit: `b71adf928989436fe98b889586a0e4c4fcbed679`
- Date: 2026-07-10
- Compiler: GCC 14.2.0
- GTK: 4.18.6
- GtkSourceView: 5.16.0
- libcmark: 0.30.2
- C/H files under `src/` and `tests/`: 67
- C/H lines under `src/` and `tests/`: 6,634
- Existing tests: 7/7 passed in the regular `build/` directory
- Stripped release executable: 141,840 bytes on x86-64
- Release text/data/bss: 105,649 / 6,376 / 48 bytes

## Build profiles

The release baseline was built with:

```sh
meson setup build-release --buildtype=release -Db_lto=true -Dstrip=true
meson compile -C build-release
```

The requested `-Dwerror=true -Db_sanitize=address,undefined` baseline currently
does not compile on GTK 4.18 because the existing tree and dialog code uses GTK
APIs that became deprecated. The affected areas are `GtkTreeView`/
`GtkTreeStore`, synchronous `GtkDialog`/`GtkFileChooserNative`, and
`gtk_show_uri()`. These warnings predate the hardening work and remain an
explicit quality-gate item; they must not be suppressed globally.

Clang, Xvfb and a repeatable GUI benchmark runner are not installed in the
current environment. Startup-to-window time and idle RSS therefore have no
trustworthy baseline yet. Preview benchmarks for 100 KiB, 1 MiB and 2 MiB will
be added as a dedicated non-gating benchmark executable before preview
optimization, so the before/after data uses the same harness.

## Runtime dependencies

The executable dynamically links GTK 4, GtkSourceView 5, GLib/GIO/GObject and
their platform dependencies. It also links libcmark at baseline; the hardening
plan removes that unused dependency.
