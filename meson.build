project('passim', 'c',
  version: '0.1.0',
  license: 'LGPL-2.1+',
  meson_version: '>=0.61.0',
  default_options: ['warning_level=2', 'c_std=c11'],
)

warning_flags = [
  '-Waggregate-return',
  '-Wunused',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wdeclaration-after-statement',
  '-Wdiscarded-qualifiers',
  '-Wduplicated-branches',
  '-Wduplicated-cond',
  '-Wempty-body',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Wimplicit-int',
  '-Winit-self',
  '-Wint-conversion',
  '-Wlogical-op',
  '-Wmaybe-uninitialized',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-cast-function-type',
  '-Wno-address-of-packed-member',  # incompatible with g_autoptr()
  '-Wno-unknown-pragmas',
  '-Wno-missing-fiepassim-initializers',
  '-Wno-strict-aliasing',
  '-Wno-suggest-attribute=format',
  '-Wno-typedef-redefinition',
  '-Wno-unknown-warning-option',
  '-Wno-unused-parameter',
  '-Wopassim-style-definition',
  '-Woverride-init',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wreturn-type',
  '-Wshadow',
  '-Wsign-compare',
  '-Wstrict-aliasing',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wtype-limits',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused-but-set-variable',
  '-Wunused-variable',
  '-Wvla',
  '-Wwrite-strings'
]
cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(warning_flags), language: 'c')

prefix = get_option('prefix')
sysconfdir = join_paths(prefix, get_option('sysconfdir'))
bindir = join_paths(prefix, get_option('bindir'))
sbindir = join_paths(prefix, get_option('sbindir'))
localstatedir = join_paths(prefix, get_option('localstatedir'))
datadir = join_paths(prefix, get_option('datadir'))

conf = configuration_data()
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_DATADIR', datadir)
conf.set_quoted('PACKAGE_SYSCONFDIR', sysconfdir)
conf.set_quoted('PACKAGE_LOCALSTATEDIR', localstatedir)
conf.set_quoted('VERSION', meson.project_version())

systemd = dependency('systemd', version: '>= 211')
systemd_root_prefix = get_option('systemd_root_prefix')
if systemd_root_prefix == ''
  systemdunitdir = systemd.get_variable(pkgconfig: 'systemdsystemunitdir')
else
  systemdunitdir = systemd.get_variable(pkgconfig: 'systemdsystemunitdir', pkgconfig_define: ['rootprefix', systemd_root_prefix])
endif

# get source version, falling back to package version
git = find_program('git', required: false)
tag = false
if git.found()
  source_version = run_command([git, 'describe'], check: false).stdout().strip()
  if source_version == ''
    source_version = meson.project_version()
  endif
  tag = run_command([git, 'describe', '--exact-match'], check: false).returncode() == 0
else
  source_version = meson.project_version()
endif
conf.set_quoted('SOURCE_VERSION', source_version)

root_incdir = include_directories('.')

libgio = dependency('gio-unix-2.0', version: '>= 2.76.0')

configure_file(
  output: 'config.h',
  configuration: conf
)

subdir('src')
subdir('data')
