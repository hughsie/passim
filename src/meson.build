install_data(
  'org.freedesktop.Passim.xml',
  install_dir: datadir / 'dbus-1/interfaces',
)
install_data(
  'passim.1',
  install_dir: datadir / 'man/man1',
)

executable(
  'passimd',
  sources: [
    'passim-avahi.c',
    'passim-avahi-service-browser.c',
    'passim-avahi-service.c',
    'passim-avahi-service-resolver.c',
    'passim-common.c',
    'passim-gnutls.c',
    'passim-server.c',
  ],
  include_directories: [
    root_incdir,
    passim_incdir,
  ],
  dependencies: [
    libgio,
    libsoup,
    libgnutls,
  ],
  link_with: [
    passim,
  ],
  install: true,
  install_dir: libexecdir,
)

passim_cli = executable(
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
    libsoup,
  ],
  link_with: [
    passim,
  ],
  install: true,
  install_dir: bindir,
)

installed_tests = get_option('installed_tests')
installed_tests_metadir = datadir / 'installed-tests' / meson.project_name()
installed_tests_execdir = libexecdir / 'installed-tests' / meson.project_name()

env = environment()
env.set('G_TEST_SRCDIR', meson.current_source_dir())
env.set('G_TEST_BUILDDIR', meson.current_build_dir())
e = executable(
  'passim-self-test',
  sources: [
    'passim-common.c',
    'passim-gnutls.c',
    'passim-self-test.c',
  ],
  include_directories: [
    root_incdir,
    passim_incdir,
  ],
  dependencies: [
    libgio,
    libgnutls,
  ],
  link_with: [
    passim
  ],
  c_args: [
    '-DSRCDIR="' + meson.current_source_dir() + '"',
    '-DBUILDDIR="' + meson.current_build_dir() + '"',
  ],
  install: installed_tests,
  install_dir: installed_tests_execdir,
)
test('passim-self-test', e, is_parallel: false, timeout: 180, env: env)
if installed_tests
  configure_file(
    input: 'passim-self-test.test.in',
    output: 'passim-self-test.test',
    configuration: {
      'installed_tests_execdir': installed_tests_execdir,
    },
    install_dir: installed_tests_metadir,
  )
endif

passim_client_test_helper = executable(
  'passim-client-test-helper',
  sources: [
    'passim-client-test-helper.c',
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
)

integration_env = environment()
integration_env.set('PASSIM_BUILDDIR', meson.project_build_root())
integration_env.set('PASSIM_SRCDIR', meson.current_source_dir())
integration_env.set('PASSIM_CLIENT_TEST_HELPER', passim_client_test_helper.full_path())
integration_env.set('PASSIM_CLI', passim_cli.full_path())
test('passim-integration-test',
  python3,
  args: [files('passim-integration-test.py')],
  env: integration_env,
  is_parallel: false,
  timeout: 120,
)
