subdir('xml')

private_headers = [
  'gtd-application.h',
  'gtd-arrow-frame.h',
  'gtd-dnd-row.h',
  'gtd-edit-pane.h',
  'gtd-empty-list-widget.h',
  'gtd-initial-setup-window.h',
  'gtd-list-selector.h',
  'gtd-list-selector-grid.h',
  'gtd-list-selector-grid-item.h',
  'gtd-list-selector-item.h',
  'gtd-list-selector-list.h',
  'gtd-list-selector-list-item.h',
  'gtd-list-selector-panel.h',
  'gtd-manager-protected.h',
  'gtd-notification-widget.h',
  'gtd-plugin-manager.h',
  'gtd-provider-dialog.h',
  'gtd-provider-popover.h',
  'gtd-provider-row.h',
  'gtd-provider-selector.h',
  'gtd-resources.h',
  'gtd-task-row.h',
  'gtd-types.h'
]

html_images = 'images/notification.png'

version_conf = configuration_data()
version_conf.set('VERSION', gnome_todo_version)

version_xml = 'version.xml'

configure_file(
          input: version_xml + '.in',
         output: version_xml,
  configuration: version_conf
)

glib_prefix = glib_dep.get_pkgconfig_variable('prefix')
glib_doc_path = join_paths(glib_prefix, 'share', 'gtk-doc', 'html')

gtk_prefix = gtk_dep.get_pkgconfig_variable('prefix')
gtk_doc_path = join_paths(gtk_prefix, 'share', 'gtk-doc', 'html')

doc_path = join_paths(gnome_todo_datadir, 'gtk-doc', 'html', meson.project_name())

gnome.gtkdoc(
  meson.project_name(),
          main_sgml: meson.project_name() + '-docs.sgml',
            src_dir: join_paths(meson.source_root(), 'src'),
       dependencies: libgtd_dep,
          mkdb_args: [ '--sgml-mode', '--output-format=xml' ],
        html_assets: html_images,
            install: true,
        install_dir: doc_path,
  gobject_typesfile: meson.project_name() + '.types',
          scan_args: [
    '--rebuild-types',
    '--ignore-headers=' + ' '.join(private_headers)
  ],
       fixxref_args: [
    '--extra-dir=@0@'.format(join_paths(glib_doc_path, 'glib')),
    '--extra-dir=@0@'.format(join_paths(glib_doc_path, 'gmodule')),
    '--extra-dir=@0@'.format(join_paths(glib_doc_path, 'gobject')),
    '--extra-dir=@0@'.format(join_paths(glib_doc_path, 'gio')),
    '--extra-dir=@0@'.format(join_paths(gtk_doc_path, 'gdk')),
    '--extra-dir=@0@'.format(join_paths(gtk_doc_path, 'gtk')),
    '--extra-dir=@0@'.format(join_paths(gtk_doc_path, 'gtksourceview-3.0'))
  ],
)
