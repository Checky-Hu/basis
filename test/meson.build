c_args = cc.get_supported_arguments([
  '-Wno-unused-parameter',
])

executable('sita_looper_test',
  'sita_looper_test.c',
  include_directories: [
    inc,
  ],
  c_args: c_args,
  link_with: [
    libbasis_shared,
  ],
)


