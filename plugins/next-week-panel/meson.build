plugins_ldflags += ['-Wl,--undefined=gtd_plugin_next_week_panel_register_types']

sources = files(
  'gtd-next-week-panel.c',
  'gtd-plugin-next-week-panel.c'
)

plugins_libs += static_library(
      'nextweekpanel',
              sources: sources,
  include_directories: plugins_incs,
         dependencies: gnome_todo_deps
)

plugins_confs += configure_file(
          input: 'next-week-panel.plugin.in',
         output: 'next-week-panel.plugin',
  configuration: plugins_conf
)
