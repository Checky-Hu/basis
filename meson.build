project('basis', 'c',
  version: '0.0.1',
  meson_version: '>= 0.47',
  default_options: [
    'warning_level=3',
    'buildtype=debugoptimized',
  ],
)

major_version = '1'

cc = meson.get_compiler('c')

inc = include_directories([
  'include',
])

deps = [
  dependency('threads'),
]

api_headers = []
api_header_deps = []

subdir('include')

c_sources = [
  'src/sita_looper.c',
]

libbasis_shared = shared_library(
  'basis',
  c_sources,
  install: false,
  soversion: major_version,
  version: meson.project_version(),
  include_directories: inc,
  dependencies: deps,
)

libbasis_static = static_library(
  'basis',
  c_sources,
  install: false,
  include_directories: inc,
  dependencies: deps,
)

subdir('test')


