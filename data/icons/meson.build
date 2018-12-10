# Symbolic application icon
symbolic_icondir = join_paths('hicolor', 'symbolic', 'apps')

install_data(
  join_paths(symbolic_icondir, 'org.gnome.Todo-symbolic.svg'),
  install_dir: join_paths(
    gnome_todo_datadir,
    join_paths('icons', symbolic_icondir)
  ),
  rename: '@0@-symbolic.svg'.format(application_id)
)

# Main application icon
icondir = join_paths('hicolor', 'scalable', 'apps')

install_data(
  join_paths(icondir, 'org.gnome.Todo.svg'),
  install_dir: join_paths(
    gnome_todo_datadir,
    join_paths('icons', icondir)
  ),
  rename: '@0@.svg'.format(application_id)
)

# Other icons
action_icondir = join_paths('hicolor', 'symbolic', 'actions')

install_subdir(
  action_icondir,
  install_dir: join_paths(
    gnome_todo_datadir,
    join_paths('icons', action_icondir)
  ),
  strip_directory: true
)
