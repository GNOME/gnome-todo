plugins_ldflags += ['-Wl,--undefined=gtd_plugin_all_tasks_panel_register_types']

sources = files(
  'gtd-all-tasks-panel.c',
  'gtd-plugin-all-tasks-panel.c'
)

plugins_libs += static_library(
      'alltaskspanel',
              sources: sources,
  include_directories: plugins_incs,
         dependencies: gnome_todo_deps
)

plugins_confs += configure_file(
          input: 'all-tasks-panel.plugin.in',
         output: 'all-tasks-panel.plugin',
  configuration: plugins_conf
)
