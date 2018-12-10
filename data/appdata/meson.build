appdata_conf = configuration_data()
appdata_conf.set('appid', application_id)
appdata = i18n.merge_file(
  type: 'xml',
  input: configure_file(
    input: 'org.gnome.Todo.appdata.xml.in.in',
    output: 'org.gnome.Todo.appdata.xml.in',
    configuration: appdata_conf
  ),
  output: '@0@.appdata.xml'.format(application_id),
  po_dir: po_dir,
  install: true,
  install_dir: join_paths(gnome_todo_datadir, 'metainfo')
)
