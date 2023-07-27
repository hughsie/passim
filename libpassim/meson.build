gnome = import('gnome')

passim_version_h = configure_file(
  input: 'passim-version.h.in',
  output: 'passim-version.h',
  configuration: conf
)

install_headers([
    'passim.h',
    'passim-client.h',
    'passim-item.h',
    passim_version_h,
  ],
  subdir: 'passim-1',
)

libpassim_deps = [
    libgio,
]

libpassim_src = [
  'passim-client.c',
  'passim-item.c',
  'passim-version.c',
]

passim_mapfile = 'passim.map'
vflag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), passim_mapfile)
passim = library(
  'passim',
  sources: libpassim_src,
  soversion: libpassim_lt_current,
  version: libpassim_lt_version,
  dependencies: libpassim_deps,
  c_args: [
      '-DG_LOG_DOMAIN="Passim"',
      '-DLOCALSTATEDIR="' + localstatedir + '"',
    ],
  include_directories: root_incdir,
  link_args: cc.get_supported_link_arguments([vflag]),
  link_depends: passim_mapfile,
  install: true
)

libpassim_dep = declare_dependency(
  link_with: passim,
  include_directories: [root_incdir, include_directories('.')],
  dependencies: libpassim_deps
)

pkgg = import('pkgconfig')
pkgg.generate(
  passim,
  requires: [ 'gio-2.0' ],
  subdirs: 'passim-1',
  version: meson.project_version(),
  name: 'passim',
  filebase: 'passim',
  description: 'passim is a system daemon for installing device firmware',
)

gir_dep = dependency('gobject-introspection-1.0', required: get_option('introspection'))
introspection = get_option('introspection').disable_auto_if(host_machine.system() != 'linux').disable_auto_if(not gir_dep.found())

if introspection.allowed()
  passim_gir_deps = [
      libgio,
  ]
  passim_gir = gnome.generate_gir(passim,
    sources: [
      'passim-client.c',
      'passim-client.h',
      'passim-item.c',
      'passim-item.h',
      'passim-version.c',
      passim_version_h,
    ],
    nsversion: '1.0',
    namespace: 'Passim',
    symbol_prefix: 'passim',
    identifier_prefix: ['Passim', 'passim'],
    export_packages: 'passim',
    header: 'passim.h',
    dependencies: passim_gir_deps,
    includes: [
      'Gio-2.0',
      'GObject-2.0',
    ],
    install: true
  )

  # Verify the map file is correct -- note we can't actually use the generated
  # file for two reasons:
  #
  #  1. We don't hard depend on GObject Introspection
  #  2. The map file is required to build the lib that the GIR is built from
  #
  # To avoid the circular dep, and to ensure we don't change exported API
  # accidentally actually check in a version of the version script to git.
  generate_version_script = [python3, files('generate-version-script.py')]
  mapfile_target = custom_target('passim_mapfile',
    input: passim_gir[0],
    output: 'passim.map',
    command: [
      generate_version_script,
      'LIBPASSIM',
      '@INPUT@',
      '@OUTPUT@',
    ],
  )
  diffcmd = find_program('diff')
  test('passim-exported-api', diffcmd,
       args: [
        '-urNp',
        files('passim.map'),
        mapfile_target,
        ],
      )
endif

passim_incdir = include_directories('.')
