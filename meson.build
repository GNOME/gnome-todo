project(
     'gnome-todo',
              'c',
          version: '40.0',
          license: 'GPL3+',
  default_options: [ 'buildtype=debugoptimized', 'warning_level=1' ],
    meson_version: '>= 0.53.0'
)

gnome_todo_version = meson.project_version()

gnome_todo_gir_namespace = 'Gtd'
gnome_todo_gir_version = '1.0'

gnome_todo_prefix = get_option('prefix')
gnome_todo_bindir = join_paths(gnome_todo_prefix, get_option('bindir'))
gnome_todo_datadir = join_paths(gnome_todo_prefix, get_option('datadir'))
gnome_todo_includedir = join_paths(gnome_todo_prefix, get_option('includedir'))
gnome_todo_libdir = join_paths(gnome_todo_prefix, get_option('libdir'))
gnome_todo_libexecdir = join_paths(gnome_todo_prefix, get_option('libexecdir'))
gnome_todo_localedir = join_paths(gnome_todo_prefix, get_option('localedir'))

gnome_todo_pkgdatadir = join_paths(gnome_todo_datadir, meson.project_name())
gnome_todo_pkgincludedir = join_paths(gnome_todo_includedir, meson.project_name())
gnome_todo_pkglibdir = join_paths(gnome_todo_libdir, meson.project_name())

gnome_todo_pluginsdir = join_paths(gnome_todo_pkglibdir, 'plugins')
gnome_todo_schemadir = join_paths(gnome_todo_datadir, 'glib-2.0', 'schemas')

soversion = 0
current = 0
revision = 0
libversion = '@0@.@1@.@2@'.format(soversion, current, revision)

gnome_todo_debug = get_option('buildtype').contains('debug')
gnome_todo_tracing = get_option('tracing')

cc = meson.get_compiler('c')

if get_option('profile') == 'development'
  profile = 'Devel'
  name_suffix = ' (Development)'
  application_id = 'org.gnome.Todo.Devel'
else
  profile = ''
  name_suffix = ''
  application_id = 'org.gnome.Todo'
endif

config_h = configuration_data()

config_h.set_quoted('GETTEXT_PACKAGE', meson.project_name())

# development build
config_h.set('DEVELOPMENT_BUILD', profile == 'Devel')

# debug options
config_h.set('GNOME_TODO_ENABLE_DEBUG', gnome_todo_debug)
config_h.set('ENABLE_TRACING', gnome_todo_tracing)
config_h.set('NDEBUG', not gnome_todo_debug)

# package
set_defines = [
  ['APPLICATION_ID', application_id],
  ['AUTOSTART_FILE', application_id + '.Autostart.desktop'],
  ['NAME_SUFFIX', name_suffix],
  ['PACKAGE', meson.project_name()],
  ['PACKAGE_BUGREPORT', 'https://gitlab.gnome.org/GNOME/' + meson.project_name() + '/issues/new'],
  ['PACKAGE_NAME', meson.project_name()],
  ['PACKAGE_STRING', '@0@ @1@'.format(meson.project_name(), gnome_todo_version)],
  ['PACKAGE_TARNAME', meson.project_name()],
  ['PACKAGE_URL', 'https://wiki.gnome.org/Apps/Todo'],
  ['PACKAGE_VERSION', meson.project_version()],
  ['VERSION', meson.project_version()],
  # i18n
  ['GETTEXT_PACKAGE', meson.project_name()]
]

foreach define: set_defines
  config_h.set_quoted(define[0], define[1])
endforeach

# headers
check_headers = [
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_LOCALE_H', 'locale.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_UNISTD_H', 'unistd.h']
]

foreach header: check_headers
  config_h.set(header[0], cc.has_header(header[1]))
endforeach

# functions
check_functions = [
  # i18n
  ['HAVE_DCGETTEXT', 'dcgettext'],
  ['HAVE_GETTEXT', 'gettext'],
  ['HAVE_ICONV', 'iconv']
]

if host_machine.system().contains('darwin')
  check_functions += [
    ['HAVE_CFLOCALECOPYCURRENT', 'CFLocaleCopyCurrent'],
    ['HAVE_CFPREFERENCESCOPYAPPVALUE', 'CFPreferencesCopyAppValue']
  ]
endif

foreach func: check_functions
  config_h.set(func[0], cc.has_function(func[1]))
endforeach

# compiler flags
common_flags = ['-DHAVE_CONFIG_H']
compiler_flags = []

if gnome_todo_debug
  test_cflags = [
    '-Wall',
    '-Wextra',
    '-Wno-error=unused-parameter', '-Wno-unused-parameter',
    '-Wno-error=missing-field-initializers', '-Wno-missing-field-initializers',
  ]

  foreach cflag: test_cflags
    if cc.has_argument(cflag)
      compiler_flags += [cflag]
    endif
  endforeach
endif

add_project_arguments(common_flags + compiler_flags, language: 'c')


#######################
# Shared dependencies #
#######################

glib_dep = dependency('glib-2.0', version: '>= 2.58.0')
gtk_dep = dependency('gtk4', version: '>= 3.92.0')

gnome_todo_deps = [
  glib_dep,
  gtk_dep,
  dependency('libadwaita-1'),
  dependency('gio-2.0', version: '>= 2.43.4'),
  dependency('gmodule-export-2.0'),
  dependency('goa-1.0', version: '>= 3.2.0'),
  dependency('libpeas-1.0', version: '>= 1.17'),
  cc.find_library('m', required: true)
]

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')

data_dir = join_paths(meson.source_root(), 'data')
icons_dir = join_paths(data_dir, 'icons')
po_dir = join_paths(meson.source_root(), 'po')
plugins_dir = join_paths(meson.source_root(), 'plugins')

subdir('build-aux')
subdir('src')
subdir('data')
subdir('po')
subdir('tests')

enable_documentation = get_option('documentation')
if enable_documentation
  subdir('doc/reference')
endif

summary({
  'Source': meson.source_root(),
  'Prefix': gnome_todo_prefix,
})

summary({
  'Debug': gnome_todo_debug,
  'Tracing': gnome_todo_tracing,
  'Documentation': enable_documentation,
}, section: 'Development')

summary({
  'Unscheduled panel': get_option('unscheduled_panel_plugin'),
  'todo.txt': get_option('todo_txt_plugin'),
  'Todoist': get_option('todoist_plugin'),
}, section: 'Plugins')
