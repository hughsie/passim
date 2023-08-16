project('passim', 'c',
  version: '0.1.0',
  license: 'LGPL-2.1+',
  meson_version: '>=0.61.0',
  default_options: ['warning_level=2', 'c_std=c11'],
)

# libtool versioning - this applies to libpassim
libpassim_lt_current = '1'
libpassim_lt_revision = '0'
libpassim_lt_age = '0'
libpassim_lt_version = '@0@.@1@.@2@'.format(libpassim_lt_current, libpassim_lt_age, libpassim_lt_revision)

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
add_project_arguments('-DPASSIM_COMPILATION', language: 'c')

# needed for memfd_create()
add_project_arguments('-D_GNU_SOURCE', language: 'c')

prefix = get_option('prefix')
sysconfdir = join_paths(prefix, get_option('sysconfdir'))
bindir = join_paths(prefix, get_option('bindir'))
localstatedir = join_paths(prefix, get_option('localstatedir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
datadir = join_paths(prefix, get_option('datadir'))

conf = configuration_data()
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_DATADIR', datadir)
conf.set_quoted('PACKAGE_SYSCONFDIR', sysconfdir)
conf.set_quoted('PACKAGE_LOCALSTATEDIR', localstatedir)
conf.set_quoted('VERSION', meson.project_version())

varr = meson.project_version().split('.')
conf.set('MAJOR_VERSION', varr[0])
conf.set('MINOR_VERSION', varr[1])
conf.set('MICRO_VERSION', varr[2])

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

libgio = dependency('gio-unix-2.0', version: '>= 2.68.0')
libsoup = dependency('libsoup-3.0', version: '>= 3.4.0')
libgnutls = dependency('gnutls', version: '>= 3.6.0')

if cc.has_function('memfd_create')
  conf.set('HAVE_MEMFD_CREATE', '1')
endif

configure_file(
  output: 'config.h',
  configuration: conf
)

python3 = import('python').find_installation('python3')

subdir('libpassim')
subdir('src')
subdir('data')
