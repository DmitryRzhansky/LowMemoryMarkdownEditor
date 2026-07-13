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
gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0')
deps = [
  gtk_dep,
  gtksource_dep,
  gdk_pixbuf_dep,
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

foreach warning_arg : [
  '-Wmissing-declarations',
  '-Wformat=2',
  '-Wundef',
  '-Wwrite-strings',
  '-Wswitch-enum',
  '-Wnull-dereference',
]
  if cc.has_argument(warning_arg)
    common_args += [warning_arg]
  endif
endforeach

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
  'src/ui/external_conflict.c',

  'src/command/command_registry.c',
  'src/command/command_actions.c',
  'src/command/command_enabled.c',
  'src/command/command_handlers.c',
  'src/command/command_file.c',
  'src/command/command_workspace.c',
  'src/command/command_document.c',
  'src/command/command_editor.c',
  'src/command/command_view.c',

  'src/document/document.c',
  'src/document/document_persistence.c',
  'src/document/document_paths.c',
  'src/document/file_io.c',
  'src/document/tabs.c',
  'src/document/document_autosave.c',
  'src/document/recovery.c',
  'src/document/file_monitor.c',
  'src/infra/file_fingerprint.c',

  'src/infra/config.c',
  'src/workspace/workspace.c',
  'src/editor/editor.c',
  'src/editor/editor_ops.c',
  'src/editor/editor_search.c',
  'src/editor/preview.c',
  'src/editor/preview_style.c',
  'src/features/image_insert.c',
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

lmme_test_lib = static_library(
  'lmme-core-test',
  core_sources,
  dependencies: deps,
  c_args: common_args + ['-DLMME_TESTING'],
  include_directories: include_directories('src'),
)

executable(
  'lmme',
  'src/main.c',
  resources,
  link_with: lmme_lib,
  dependencies: deps,
  c_args: common_args + ['-Wno-overlength-strings'],
  include_directories: include_directories('src'),
  install: true,
)

subdir('tests')
