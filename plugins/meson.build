plugins_incs = [top_inc, src_inc]

plugins_ldflags = []
plugins_libs = []
plugins_confs = []

plugins_conf = configuration_data()
plugins_conf.set('VERSION', gnome_todo_version)

subdir('eds')
subdir('next-week-panel')

if get_option('background_plugin')
  subdir('background')
endif

if get_option('dark_theme_plugin')
  subdir('dark-theme')
endif

if get_option('scheduled_panel_plugin')
  subdir('scheduled-panel')
endif

if get_option('score_plugin')
  subdir('score')
endif

if get_option('today_panel_plugin')
  subdir('today-panel')
endif

if get_option('unscheduled_panel_plugin')
  subdir('unscheduled-panel')
endif

if get_option('todo_txt_plugin')
  subdir('todo-txt')
endif

if get_option('todoist_plugin')
  subdir('todoist')
endif
