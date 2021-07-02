plugins_incs = [
  incs,
  include_directories('..'),
]

plugins_sources = []
plugins_deps = [ gnome_todo_deps ]
plugins_ldflags = []
plugins_libs = []
plugins_confs = []

plugins_conf = configuration_data()
plugins_conf.set('VERSION', gnome_todo_version)

subdir('all-tasks-panel')
subdir('background')
subdir('eds')
subdir('inbox-panel')
subdir('next-week-panel')
subdir('night-light')
subdir('peace')
subdir('scheduled-panel')
subdir('task-lists-workspace')
subdir('today-panel')
subdir('welcome')

#if get_option('unscheduled_panel_plugin')
  #subdir('unscheduled-panel')
#endif

#if get_option('todo_txt_plugin')
  #subdir('todo-txt')
#endif

#if get_option('todoist_plugin')
  #subdir('todoist')
#endif

plugins_lib = static_library(
            'plugins',
      plugins_sources,
         dependencies: plugins_deps,
  include_directories: plugins_incs,
            link_with: plugins_libs,
            link_args: plugins_ldflags,
)

plugins_dep = declare_dependency(
  link_whole: plugins_lib,
)
