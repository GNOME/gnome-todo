desktop = 'org.gnome.Todo.desktop'

i18n.merge_file(
  desktop,
  type: 'desktop',
  input: desktop + '.in',
  output: desktop,
  po_dir: po_dir,
  install: true,
  install_dir: join_paths(gnome_todo_datadir, 'applications')
)

autostart = 'org.gnome.Todo.Autostart.desktop'

i18n.merge_file(
  autostart,
  type: 'desktop',
  input: autostart + '.in',
  output: autostart,
  po_dir: po_dir,
  install: true,
  install_dir: gnome_todo_pkgdatadir
)

service_conf = configuration_data()
service_conf.set('bindir', gnome_todo_bindir)

service = 'org.gnome.Todo.service'

configure_file(
  input: service + '.in',
  output: service,
  install: true,
  install_dir: join_paths(gnome_todo_datadir, 'dbus-1', 'services'),
  configuration: service_conf
)

search_provider_conf = configuration_data()
search_provider_conf.set('bindir', gnome_todo_bindir)

configure_file(
  input: files('org.gnome.Todo.search-provider.in'),
  output: 'todo.search-provider.ini',
  install_dir: join_paths(gnome_todo_datadir, 'gnome-shell', 'search-providers'),
  configuration: search_provider_conf
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
