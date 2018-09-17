#################
# Tests Library #
#################

sources = files(
  'dummy-provider.c',
)

tests_incs = [
  src_inc,
  include_directories('.'),
]

libgtd_tests = static_library(
           'gtd_tests',
              sources : sources,
  include_directories : tests_incs,
         dependencies : gnome_todo_deps,
               c_args : cflags,
)

tests_libs = [
  libgtd,
  libgtd_tests,
]


################
# Static tests #
################

static_test_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'PYTHONDONTWRITEBYTECODE=yes',
  'MALLOC_CHECK_=2',
  'MALLOC_PERTURB_=$((${RANDOM:-256} % 256))',
]

static_test_cflags = [
  '-fPIE',
  '-DTEST_DATA_DIR="@0@/data"'.format(meson.current_source_dir()),
]

static_test_link_args = ['-fPIC']

static_tests = [
  'test-model-filter',
  'test-model-sort',
  'test-task-model',
]

foreach static_test : static_tests

  source = ['@0@.c'.format(static_test)]

  static_test_program = executable(
              static_test,
                   source,
                  c_args : static_test_cflags,
               link_args : static_test_link_args,
            dependencies : gnome_todo_deps,
               link_with : tests_libs,
     include_directories : tests_incs,
  )

  test(static_test, static_test_program, env: static_test_env)
endforeach



#####################
# Interactive tests #
#####################

interactive_tests = [
  'test-colorbutton',
  'test-filter-sort',
  'test-task-model',
]

foreach interactive_test : interactive_tests

  interactive_test_name = 'interactive-@0@'.format(interactive_test)

  source = ['interactive/@0@.c'.format(interactive_test)]

  executable(
  interactive_test_name,
                 source,
   include_directories: tests_incs,
          dependencies: gnome_todo_deps,
                c_args: cflags,
             link_with: tests_libs,
)
endforeach
