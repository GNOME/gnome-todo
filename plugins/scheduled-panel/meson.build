plugin_name = 'scheduled-panel'

plugins_ldflags += ['-Wl,--undefined=gtd_plugin_scheduled_panel_register_types']

sources = files(
  'gtd-panel-scheduled.c',
  'gtd-plugin-' + plugin_name + '.c'
)

plugins_libs += static_library(
  'scheduledpanel',
  sources: sources,
  include_directories: plugins_incs,
  dependencies: gnome_todo_deps
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
