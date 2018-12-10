desktop_conf = configuration_data()
desktop_conf.set('icon', application_id)
desktop = i18n.merge_file(
  type: 'desktop',
  input: configure_file(
    input: 'org.gnome.Todo.desktop.in.in',
    output: 'org.gnome.Todo.desktop.in',
    configuration: desktop_conf
  ),
  output: '@0@.desktop'.format(application_id),
  po_dir: po_dir,
  install: true,
  install_dir: join_paths(gnome_todo_datadir, 'applications')
)

autostart = i18n.merge_file(
  type: 'desktop',
  input: configure_file(
    input: 'org.gnome.Todo.Autostart.desktop.in.in',
    output: 'org.gnome.Todo.Autostart.desktop.in',
    configuration: desktop_conf
  ),
  output: '@0@.Autostart.desktop'.format(application_id),
  po_dir: po_dir,
  install: true,
  install_dir: gnome_todo_pkgdatadir
)

service_conf = configuration_data()
service_conf.set('bindir', gnome_todo_bindir)
service_conf.set('application_id', application_id)

service = configure_file(
  input: 'org.gnome.Todo.service.in',
  output: '@0@.service'.format(application_id),
  install: true,
  install_dir: join_paths(gnome_todo_datadir, 'dbus-1', 'services'),
  configuration: service_conf
)

gnome.mkenums(
  'org.gnome.todo.enums.xml',
  sources: enum_headers,
  comments: '<!-- @comment@ -->',
  fhead: '<schemalist>',
  vhead: '  <@type@ id="org.gnome.todo.@EnumName@">',
  vprod: '    <value nick="@valuenick@" value="@valuenum@"/>',
  vtail: '  </@type@>',
  ftail: '</schemalist>',
  install_header: true,
  install_dir: gnome_todo_schemadir
)

install_data(
  'org.gnome.todo.gschema.xml',
  install_dir: gnome_todo_schemadir
)

subdir('appdata')
subdir('icons')
