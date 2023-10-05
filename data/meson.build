install_data(
  'passim.conf',
  install_dir: sysconfdir,
)
install_data(
  'passim.sysusers.conf',
  rename: ['passim.conf'],
  install_dir: sysusersdir,
)
install_data(
  'favicon.ico',
  'style.css',
  install_dir: datadir / meson.project_name(),
)
install_data(
  'org.freedesktop.Passim.png',
  install_dir: datadir / 'icons/hicolor/scalable/apps/org.freedesktop.Passim.png',
)

con2 = configuration_data()
con2.set('libexecdir', libexecdir)
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
  install_dir: datadir / 'dbus-1/system-services',
)
install_data(
  'org.freedesktop.Passim.conf',
  install_dir: datadir / 'dbus-1/system.d',
)
install_data(
  'org.freedesktop.Passim.metainfo.xml',
  install_dir: datadir / 'metainfo',
)
install_data(
  'a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447-HELLO.md',
  install_dir: localstatedir / 'lib' / meson.project_name() / 'data',
)
