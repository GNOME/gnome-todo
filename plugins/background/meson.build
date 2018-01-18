plugin_name = 'background'

plugins_ldflags += ['-Wl,--undefined=gtd_plugin_background_register_types']

plugins_libs += static_library(
          plugin_name,
              sources: 'gtd-plugin-' + plugin_name + '.c',
  include_directories: plugins_incs,
         dependencies: gnome_todo_deps,
               c_args: '-DPACKAGE_DATA_DIR="@0@"'.format(gnome_todo_pkgdatadir),
)

install_data(
  'org.gnome.todo.' + plugin_name + '.gschema.xml',
  install_dir: gnome_todo_schemadir
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
