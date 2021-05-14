sources = []

#######
# VCS #
#######

configure_file(output: 'config.h', configuration: config_h)

vcs_tag = vcs_tag(
  fallback: get_option('profile') != 'default'? 'devel' : 'stable',
     input: 'gtd-vcs.h.in',
    output: 'gtd-vcs.h'
)

vcs_identifier_h = declare_dependency(sources: vcs_tag)

gnome_todo_deps += vcs_identifier_h

###########
# Plugins #
###########

incs = include_directories(
  'animation',
  'core',
  'gui',
  'models',
)

###########
# Subdirs #
###########

subdir('gui')
subdir('plugins')

################
# Dependencies #
################

gtd_deps = gnome_todo_deps + [
  plugins_dep,
]


#########
# Enums #
#########

enum_headers = files(
  join_paths('animation', 'gtd-animation-enums.h'),
)

enum_types = 'gtd-enum-types'

sources += gnome.mkenums(
  enum_types,
     sources: enum_headers,
  c_template: enum_types + '.c.template',
  h_template: enum_types + '.h.template'
)


################
# Header files #
################

headers = files(
  'animation/gtd-animatable.h',
  'animation/gtd-animation-utils.h',
  'animation/gtd-easing.h',
  'animation/gtd-interval.h',
  'animation/gtd-keyframe-transition.h',
  'animation/gtd-property-transition.h',
  'animation/gtd-timeline.h',
  'animation/gtd-transition.h',
  'core/gtd-activatable.h',
  'core/gtd-clock.h',
  'core/gtd-manager.h',
  'core/gtd-notification.h',
  'core/gtd-object.h',
  'core/gtd-provider.h',
  'core/gtd-task.h',
  'core/gtd-task-list.h',
  'gui/gtd-bin-layout.h',
  'gui/gtd-max-size-layout.h',
  'gui/gtd-menu-button.h',
  'gui/gtd-omni-area.h',
  'gui/gtd-omni-area-addin.h',
  'gui/gtd-panel.h',
  'gui/gtd-provider-popover.h',
  'gui/gtd-star-widget.h',
  'gui/gtd-task-list-view.h',
  'gui/gtd-widget.h',
  'gui/gtd-window.h',
  'gui/gtd-workspace.h',
  'models/gtd-list-model-filter.h',
  'models/gtd-list-store.h',
  'gtd-types.h',
  'gtd-utils.h',
  'gnome-todo.h'
)

install_headers(headers, subdir: meson.project_name())


################
# Source files #
################

sources += files(
  'animation/gtd-animatable.c',
  'animation/gtd-animation-utils.c',
  'animation/gtd-easing.c',
  'animation/gtd-interval.c',
  'animation/gtd-keyframe-transition.c',
  'animation/gtd-property-transition.c',
  'animation/gtd-timeline.c',
  'animation/gtd-transition.c',
  'core/gtd-activatable.c',
  'core/gtd-clock.c',
  'core/gtd-log.c',
  'core/gtd-manager.c',
  'core/gtd-notification.c',
  'core/gtd-object.c',
  'core/gtd-plugin-manager.c',
  'core/gtd-provider.c',
  'core/gtd-task.c',
  'core/gtd-task-list.c',
  'gui/gtd-bin-layout.c',
  'gui/gtd-panel.c',
  'gui/gtd-workspace.c',
  'gui/gtd-notification-widget.c',
  'gui/gtd-provider-popover.c',
  'gui/gtd-provider-row.c',
  'gui/gtd-provider-selector.c',
  'gui/gtd-edit-pane.c',
  'gui/gtd-markdown-renderer.c',
  'gui/gtd-new-task-row.c',
  'gui/gtd-task-list-popover.c',
  'gui/gtd-task-list-view.c',
  'gui/gtd-task-list-view-model.c',
  'gui/gtd-task-row.c',
  'gui/gtd-color-button.c',
  'gui/gtd-empty-list-widget.c',
  'gui/gtd-menu-button.c',
  'gui/gtd-star-widget.c',
  'gui/gtd-application.c',
  'gui/gtd-initial-setup-window.c',
  'gui/gtd-css-provider.c',
  'gui/gtd-max-size-layout.c',
  'gui/gtd-omni-area.c',
  'gui/gtd-omni-area-addin.c',
  'gui/gtd-theme-manager.c',
  'gui/gtd-widget.c',
  'gui/gtd-window.c',
  'models/gtd-list-model-filter.c',
  'models/gtd-list-model-sort.c',
  'models/gtd-list-store.c',
  'models/gtd-task-model.c',
  'gtd-utils.c',
  'main.c'
)


#############
# Resources #
#############

sources += gnome.compile_resources(
  'gtd-resources',
  'todo.gresource.xml',
  c_name: 'todo',
  export: true,
)

sources += gnome.compile_resources(
  'gtd-icon-resources',
  join_paths(icons_dir, 'icons.gresource.xml'),
    source_dir: [ icons_dir ],
        c_name: 'todo_icons',
        export: true,
)

cflags = [
  '-DPACKAGE_DATA_DIR="@0@"'.format(gnome_todo_pkgdatadir),
  '-DPACKAGE_LIB_DIR="@0@"'.format(gnome_todo_pkglibdir),
  '-DPACKAGE_LOCALE_DIR="@0@"'.format(gnome_todo_localedir),
  '-DPACKAGE_SRC_DIR="@0@"'.format(meson.current_source_dir()),
  '-DUI_DATA_DIR="@0@"'.format(join_paths(gnome_todo_pkgdatadir, 'style'))
]

ldflags = [ '-Wl,--export-dynamic' ]


#########
# Debug #
#########

debug_conf = configuration_data()
debug_conf.set('BUGREPORT_URL', 'https://gitlab.gnome.org/GNOME/gnome-todo/issues/new')
debug_conf.set10('ENABLE_TRACING', gnome_todo_tracing)


sources += configure_file(
          input: 'gtd-debug.h.in',
         output: 'gtd-debug.h',
  configuration: debug_conf,
)


##############
# gnome-todo #
##############

gnome_todo = executable(
  meson.project_name(),
               sources,
  include_directories: incs,
         dependencies: [ vcs_identifier_h, gtd_deps ],
               c_args: cflags,
            link_args: ldflags,
              install: true,
          install_dir: gnome_todo_bindir
)


###################
# Private library #
###################

libgtd = shared_library(
                'gtd',
              sources: sources,
              version: libversion,
            soversion: soversion,
  include_directories: incs,
         dependencies: gtd_deps,
               c_args: cflags
)

libgtd_dep = declare_dependency(
            link_with: libgtd,
         dependencies: gtd_deps
)


##############
# pkg-config #
##############

pkg.generate(
    libraries: libgtd,
      version: gnome_todo_version,
         name: 'GNOME To Do',
  description: 'Header and path for GNOME Todo Plugins',
     filebase: meson.project_name(),
      subdirs: meson.project_name(),
    variables: 'exec_prefix=' + gnome_todo_libexecdir,
  install_dir: join_paths(gnome_todo_libdir, 'pkgconfig')
)


#########################
# GObject-Introspection #
#########################

if get_option('introspection')
  gir_sources = files(
    'core/gtd-activatable.c',
    'core/gtd-activatable.h',
    'core/gtd-clock.c',
    'core/gtd-clock.h',
    'core/gtd-manager.c',
    'core/gtd-manager.h',
    'core/gtd-notification.c',
    'core/gtd-notification.h',
    'core/gtd-object.c',
    'core/gtd-object.h',
    'core/gtd-provider.c',
    'core/gtd-provider.h',
    'core/gtd-task.c',
    'core/gtd-task.h',
    'core/gtd-task-list.c',
    'core/gtd-task-list.h',
    'gui/gtd-bin-layout.c',
    'gui/gtd-bin-layout.h',
    'gui/gtd-max-size-layout.c',
    'gui/gtd-max-size-layout.h',
    'gui/gtd-menu-button.c',
    'gui/gtd-menu-button.h',
    'gui/gtd-omni-area.c',
    'gui/gtd-omni-area.h',
    'gui/gtd-omni-area-addin.c',
    'gui/gtd-omni-area-addin.h',
    'gui/gtd-panel.c',
    'gui/gtd-panel.h',
    'gui/gtd-provider-popover.c',
    'gui/gtd-provider-popover.h',
    'gui/gtd-star-widget.c',
    'gui/gtd-star-widget.h',
    'gui/gtd-task-list-view.c',
    'gui/gtd-task-list-view.h',
    'gui/gtd-widget.c',
    'gui/gtd-widget.h',
    'gui/gtd-window.c',
    'gui/gtd-window.h',
    'gui/gtd-workspace.c',
    'gui/gtd-workspace.h',
    'models/gtd-list-model-filter.c',
    'models/gtd-list-model-filter.h',
    'models/gtd-list-store.c',
    'models/gtd-list-store.h',
    'gtd-types.h',
  )

  gir_incs = [
    'Gio-2.0',
    'GObject-2.0',
    'Gtk-4.0'
  ]

  gir_extra_args = '--warn-all'

  gir_dir = join_paths(gnome_todo_datadir, '@0@-@1@'.format('gir', gnome_todo_gir_version))
  typelib_dir = join_paths(gnome_todo_libdir, '@0@-@1@'.format('girepository', gnome_todo_gir_version))

  gnome.generate_gir(
                 libgtd,
                sources: gir_sources,
              namespace: gnome_todo_gir_namespace,
              nsversion: gnome_todo_gir_version,
      identifier_prefix: gnome_todo_gir_namespace,
          symbol_prefix: gnome_todo_gir_namespace.to_lower(),
               includes: gir_incs,
    include_directories: incs,
             extra_args: gir_extra_args,
                install: true,
        install_dir_gir: gir_dir,
    install_dir_typelib: typelib_dir,
  )
endif
