# LowMemoryMarkdownEditor

Small Linux-only GTK Markdown editor focused on low memory usage and local folders.

## What it does

- opens a local folder as a workspace
- shows folders, Markdown files and images
- edits Markdown files with GtkSourceView
- supports tabs
- supports autosave and manual save
- supports editor-only and editor+preview modes
- supports image insertion into a workspace-level `img` folder
- stores basic config in `~/.config/lmme/config.ini`

## What it does not do

- no Electron
- no WebEngine or WebView
- no cloud
- no plugins
- no graph
- no backlinks
- no PDF/HTML export
- no Mermaid/LaTeX rendering in v0.1

## Dependencies

```bash
sudo apt update
sudo apt install -y build-essential meson ninja-build pkg-config libgtk-4-dev libgtksourceview-5-dev libcmark-dev
```

## Build

```bash
meson setup build
meson compile -C build
```

## Run

```bash
./build/lmme
```

## Tests

```bash
meson test -C build
```

## Development notes

UI language is English only.
Primary target is MX Linux / Debian-based systems.
Preview is a basic native Markdown preview, not full GitHub Markdown rendering.

## License

MIT.
