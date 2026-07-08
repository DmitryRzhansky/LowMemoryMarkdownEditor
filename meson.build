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

core_sources = [
  'src/app.c',
  'src/window.c',
  'src/config.c',
  'src/workspace.c',
  'src/file_tree.c',
  'src/editor.c',
  'src/tabs.c',
  'src/preview.c',
  'src/preview_style.c',
  'src/image_insert.c',
  'src/markdown.c',
  'src/autosave.c',
  'src/dialogs.c',
  'src/util.c',
]

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
