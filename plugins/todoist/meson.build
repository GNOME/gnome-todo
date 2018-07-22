plugin_name = 'todoist'

plugins_ldflags += ['-Wl,--undefined=gtd_plugin_todoist_register_types']

sources = files(
  'gtd-plugin-' + plugin_name + '.c',
  'gtd-provider-' + plugin_name + '.c',
  'gtd-' + plugin_name + '-preferences-panel.c'
)

todoist_deps = gnome_todo_deps + [
  dependency('rest-0.7'),
  dependency('json-glib-1.0'),
  dependency('webkit2gtk-4.0', version: '>= 2.20'),
  dependency('libsecret-1', version: '>= 0.18')
]

plugins_libs += static_library(
          plugin_name,
              sources: sources,
  include_directories: plugins_incs,
         dependencies: todoist_deps
)

plugin_data = plugin_name + '.plugin'

plugins_confs += configure_file(
          input: plugin_data + '.in',
         output: plugin_data,
  configuration: plugins_conf
)
