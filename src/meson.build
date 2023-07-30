install_data(['org.freedesktop.Passim.xml'],
  install_dir: join_paths(datadir, 'dbus-1', 'interfaces'),
)
install_data(['passim.1'],
  install_dir: join_paths(datadir, 'man', 'man1'),
)

executable(
  'passimd',
  sources: [
    'passim-avahi.c',
    'passim-avahi-service-browser.c',
    'passim-avahi-service.c',
    'passim-avahi-service-resolver.c',
    'passim-common.c',
    'passim-server.c',
  ],
  include_directories: [
    root_incdir,
    passim_incdir,
  ],
  dependencies: [
    libgio,
  ],
  link_with: [
    passim,
  ],
  install: true,
  install_dir: sbindir,
)

executable(
  'passim',
  sources: [
    'passim-cli.c',
    'passim-common.c',
  ],
  include_directories: [
    root_incdir,
    passim_incdir,
  ],
  dependencies: [
    libgio,
  ],
  link_with: [
    passim,
  ],
  install: true,
  install_dir: bindir,
)

env = environment()
env.set('G_TEST_SRCDIR', meson.current_source_dir())
env.set('G_TEST_BUILDDIR', meson.current_build_dir())
e = executable(
  'passim-self-test',
  sources: [
    'passim-common.c',
    'passim-self-test.c',
  ],
  include_directories: [
    root_incdir,
    passim_incdir,
  ],
  dependencies: [
    libgio
  ],
  link_with: [
    passim
  ],
  c_args: [
    '-DSRCDIR="' + meson.current_source_dir() + '"',
    '-DBUILDDIR="' + meson.current_build_dir() + '"',
  ],
)
test('passim-self-test', e, is_parallel: false, timeout: 180, env: env)
