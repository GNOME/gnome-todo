plugin_name = 'score'

plugin_dir = join_paths(gnome_todo_pluginsdir, plugin_name)

install_data(
  join_paths(plugin_name, '__init__.py'),
  install_dir: join_paths(plugin_dir, plugin_name)
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
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: plugin_dir
)
