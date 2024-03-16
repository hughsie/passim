i18n = import('i18n')
i18n.gettext(meson.project_name(),
  preset: 'glib',
  args: [
  '--default-domain=' + meson.project_name(),
  ]
)

run_target('fix-translations',
  command: [
    [python3, files('fix_translations.py')],
    join_paths(meson.project_source_root(), 'po')
  ]
)
