test_sources = {
  'test_util': 'test_util.c',
  'test_config': 'test_config.c',
  'test_safe_write': 'test_safe_write.c',
  'test_recovery': 'test_recovery.c',
  'test_command_registry': 'test_command_registry.c',
  'test_preview_style': 'test_preview_style.c',
  'test_workspace': 'test_workspace.c',
  'test_image_insert_target': 'test_image_insert_target.c',
  'test_search_flow': 'test_search_flow.c',
  'test_file_fingerprint': 'test_file_fingerprint.c',
  'test_file_io': 'test_file_io.c',
  'test_file_monitor_state': 'test_file_monitor_state.c',
  'test_document_lifecycle': 'test_document_lifecycle.c',
  'test_document_paths': 'test_document_paths.c',
}

foreach test_name, test_source : test_sources
  exe = executable(
    test_name,
    test_source,
    link_with: lmme_lib,
    dependencies: deps,
    include_directories: include_directories('../src'),
    c_args: common_args,
  )
  test(test_name, exe)
endforeach

preview_benchmark = executable(
  'benchmark_preview',
  'benchmark_preview.c',
  link_with: lmme_lib,
  dependencies: deps,
  include_directories: include_directories('../src'),
  c_args: common_args,
)
benchmark('preview_style', preview_benchmark)

preview_active_line_benchmark = executable(
  'benchmark_preview_active_line',
  'benchmark_preview_active_line.c',
  link_with: lmme_lib,
  dependencies: deps,
  include_directories: include_directories('../src'),
  c_args: common_args,
)
benchmark('preview_active_line', preview_active_line_benchmark)

safe_write_benchmark = executable(
  'benchmark_safe_write',
  'benchmark_safe_write.c',
  link_with: lmme_lib,
  dependencies: deps,
  include_directories: include_directories('../src'),
  c_args: common_args,
)
benchmark('safe_write', safe_write_benchmark)
