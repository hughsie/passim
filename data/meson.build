install_data(['passim.conf'],
  install_dir: sysconfdir,
)
install_data(['favicon.ico', 'style.css'],
  install_dir: join_paths(datadir, meson.project_name()),
)
install_data(['org.freedesktop.Passim.png'],
  install_dir: join_paths(datadir, 'icons', 'hicolor', 'scalable', 'apps', 'org.freedesktop.Passim.png'),
)

con2 = configuration_data()
con2.set('sbindir', sbindir)
con2.set('localstatedir', localstatedir)
configure_file(
  input: 'passim.service.in',
  output: 'passim.service',
  configuration: con2,
  install: true,
  install_dir: systemdunitdir,
)
configure_file(
  input: 'org.freedesktop.Passim.service.in',
  output: 'org.freedesktop.Passim.service',
  configuration: con2,
  install: true,
  install_dir: join_paths(datadir, 'dbus-1', 'system-services'),
)
install_data(['org.freedesktop.Passim.conf'],
  install_dir: join_paths(datadir, 'dbus-1', 'system.d')
)
install_data(['org.freedesktop.Passim.metainfo.xml'],
  install_dir: join_paths(datadir, 'metainfo')
)
install_data(['a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447-HELLO.md'],
  install_dir: join_paths(localstatedir, 'lib', meson.project_name(), 'data')
)
