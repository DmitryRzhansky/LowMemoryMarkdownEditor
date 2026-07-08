test_sources = {
  'test_util': 'test_util.c',
  'test_config': 'test_config.c',
  'test_markdown': 'test_markdown.c',
  'test_autosave': 'test_autosave.c',
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
