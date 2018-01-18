#i18n.gettext(meson.project_name(), preset: 'glib')
# FIXME: workaround for extracting translations from plugin files
glib_preset = [
  '--from-code=UTF-8',
  '--add-comments',

  # https://developer.gnome.org/glib/stable/glib-I18N.html
  '--keyword=_',
  '--keyword=N_',
  '--keyword=C_:1c,2',
  '--keyword=NC_:1c,2',
  '--keyword=g_dcgettext:2',
  '--keyword=g_dngettext:2,3',
  '--keyword=g_dpgettext2:2c,3',

  '--flag=N_:1:pass-c-format',
  '--flag=C_:2:pass-c-format',
  '--flag=NC_:2:pass-c-format',
  '--flag=g_dngettext:2:pass-c-format',
  '--flag=g_strdup_printf:1:c-format',
  '--flag=g_string_printf:2:c-format',
  '--flag=g_string_append_printf:2:c-format',
  '--flag=g_error_new:3:c-format',
  '--flag=g_set_error:4:c-format'
]

glib_preset += ['--keyword=Description']

command = [
  find_program('xgettext'),
  '--package-name=' + meson.project_name(),
  '-p', meson.current_source_dir(),
  '-f', join_paths(meson.current_source_dir(), 'POTFILES.in'),
  '-D', meson.source_root(),
  '-k_',
  '-o', join_paths(meson.current_source_dir(), meson.project_name() + '.pot')
] + glib_preset

run_target(
  meson.project_name() + '-pot',
  command: command
)
