plugin_name = 'eds'

plugins_ldflags += ['-Wl,--undefined=gtd_plugin_eds_register_types']


################
# Dependencies #
################

eds_plugin_deps = [
  dependency('libecal-2.0', version: '>= 3.33.1'),
  dependency('libedataserver-1.2', version: '>= 3.17.1'),
  dependency('libedataserverui-1.2', version: '>= 3.17.1'),
]

eds_plugin_deps += gnome_todo_deps

sources = files(
  'gtd-plugin-' + plugin_name + '.c',
  'gtd-provider-' + plugin_name + '.c',
  'gtd-provider-goa.c',
  'gtd-provider-local.c',
  'gtd-task-' + plugin_name + '.c',
  'gtd-task-list-' + plugin_name + '.c',
)

plugins_libs += static_library(
          plugin_name,
              sources: sources,
  include_directories: plugins_incs,
         dependencies: eds_plugin_deps
)

plugin_data = plugin_name + '.plugin'

plugins_confs += configure_file(
          input: plugin_data + '.in',
         output: plugin_data,
  configuration: plugins_conf
)
