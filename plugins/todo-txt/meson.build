plugin_name = 'todo-txt'

plugins_ldflags += ['-Wl,--undefined=gtd_plugin_todo_txt_register_types']

sources = files(
  'gtd-plugin-' + plugin_name + '.c',
  'gtd-provider-' + plugin_name + '.c',
  'gtd-' + plugin_name + '-parser.c',
  'gtd-' + 'task-' + plugin_name + '.c'
)

plugins_libs += static_library(
  'todotxt',
              sources: sources,
  include_directories: plugins_incs,
         dependencies: gnome_todo_deps
)

install_data(
  'org.gnome.todo.txt.gschema.xml',
  install_dir: gnome_todo_schemadir
)

plugin_data = plugin_name + '.plugin'

plugins_confs += configure_file(
          input: plugin_data + '.in',
         output: plugin_data,
  configuration: plugins_conf
)
