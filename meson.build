project(
  'LowMemoryMarkdownEditor',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c17',
    'warning_level=2',
    'buildtype=debugoptimized'
  ]
)

cc = meson.get_compiler('c')

pkg_config = find_program('pkg-config', required: false)
if not pkg_config.found()
  error('pkg-config was not found. Install it first, for example: sudo apt install pkg-config. If your distro uses pkgconf, install pkgconf and make sure /usr/bin/pkg-config exists, then remove the failed build directory and rerun meson setup build.')
endif

gtk_dep = dependency('gtk4')
gtksource_dep = dependency('gtksourceview-5')
cmark_dep = dependency(
  'libcmark',
  required: false,
)

if not cmark_dep.found()
  error('libcmark was not found. On Debian/MX Linux install it with: sudo apt install libcmark-dev')
endif

deps = [
  gtk_dep,
  gtksource_dep,
  cmark_dep,
]

common_args = [
  '-Wall',
  '-Wextra',
  '-Wpedantic',
  '-Wshadow',
  '-Wstrict-prototypes',
  '-Wmissing-prototypes',
  '-Werror=implicit-function-declaration',
]

if cc.has_argument('-Wconversion')
  common_args += ['-Wconversion']
endif

gnome = import('gnome')
resources = gnome.compile_resources(
  'lmme-resources',
  'data/lmme.gresource.xml',
  source_dir: 'data',
  c_name: 'lmme',
)

core_sources = files(
  'src/app/app.c',

  'src/ui/window.c',
  'src/ui/menu.c',
  'src/ui/toolbar.c',
  'src/ui/statusbar.c',
  'src/ui/search_bar.c',
  'src/ui/file_tree_view.c',
  'src/ui/tree_context_menu.c',
  'src/ui/tab_context_menu.c',

  'src/command/command_registry.c',
  'src/command/command_actions.c',
  'src/command/command_shortcuts.c',

  'src/document/document.c',
  'src/document/tabs.c',
  'src/document/document_autosave.c',
  'src/document/recovery.c',
  'src/document/file_monitor.c',

  'src/infra/config.c',
  'src/workspace/workspace.c',
  'src/editor/editor.c',
  'src/editor/editor_ops.c',
  'src/editor/editor_search.c',
  'src/editor/preview.c',
  'src/editor/preview_style.c',
  'src/features/image_insert.c',
  'src/editor/markdown.c',
  'src/infra/dialogs.c',
  'src/infra/safe_write.c',
  'src/infra/util.c',
)

lmme_lib = static_library(
  'lmme-core',
  core_sources,
  dependencies: deps,
  c_args: common_args,
  include_directories: include_directories('src'),
)

executable(
  'lmme',
  'src/main.c',
  resources,
  link_with: lmme_lib,
  dependencies: deps,
  c_args: common_args,
  include_directories: include_directories('src'),
  install: true,
)

subdir('tests')
