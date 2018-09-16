plugins_ldflags += ['-Wl,--undefined=gtd_night_light_plugin_register_types']

plugins_libs += static_library(
          'nightlight',
              sources : 'gtd-night-light-plugin.c',
  include_directories : plugins_incs,
         dependencies : gnome_todo_deps
)

plugins_confs += configure_file(
          input : 'night-light.plugin.in',
         output : 'night-light.plugin',
  configuration : plugins_conf
)
