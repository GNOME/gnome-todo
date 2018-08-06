#####################
# Interactive tests #
#####################

interactive_tests = [
  'test-colorbutton',
]

foreach interactive_test : interactive_tests

  interactive_test_name = 'interactive-@0@'.format(interactive_test)

  source = ['interactive/@0@.c'.format(interactive_test)]

  executable(
  interactive_test_name,
                 source,
   include_directories: src_inc,
          dependencies: gnome_todo_deps,
                c_args: cflags,
             link_with: [ libgtd ],
)
endforeach
