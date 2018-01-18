plugin_name = 'todoist'

plugins_ldflags += ['-Wl,--undefined=gtd_plugin_todoist_register_types']

sources = files(
  'gtd-plugin-' + plugin_name + '.c',
  'gtd-provider-' + plugin_name + '.c',
  'gtd-' + plugin_name + '-preferences-panel.c'
)

todoist_deps = gnome_todo_deps + [
  dependency('rest-0.7'),
  dependency('json-glib-1.0')
]

plugins_libs += static_library(
          plugin_name,
              sources: sources,
  include_directories: plugins_incs,
         dependencies: todoist_deps
)

plugin_data = plugin_name + '.plugin'

plugin_data_in = configure_file(
  input: plugin_data + '.desktop.in.in',
  output: plugin_data + '.in',
  configuration: plugins_conf
)

plugins_confs += custom_target(
  plugin_data,
  input: plugin_data_in,
  output: plugin_data,
  command: msgfmt_plugin_cmd
)
