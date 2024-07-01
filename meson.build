project('lite-xl-system-fonts', 'c',
    version: '0.0.1',
    license: 'MIT',
    default_options: ['default_library=static'])

fc_dynamic = get_option('fontconfig_dynamic').disable_if(host_machine.system() == 'windows')

deps = []
c_args = []
link_args = ['-static-libgcc']
if fc_dynamic.allowed()
    c_args += '-DFONTCONFIG_DYNAMIC'
    deps += dependency('dl')
else
    dependency('freetype2',
        static: true,
        required: true,
        default_options: ['harfbuzz=disabled'])
    fc_opts = [
        'doc=disabled',
        'nls=disabled',
        'tests=disabled',
        'tools=disabled',
        'cache-build=disabled',
        'default_library=static',
    ]
    deps += dependency('fontconfig',
        default_options: fc_opts,
        static: true,
        required: true) 
endif

plugin = shared_module('systemfonts', 'systemfonts.c',
    c_args: c_args,
    link_args: link_args,
    dependencies: deps,
    name_prefix: '',
    install: true,
    install_dir: '/libraries')

install_data('systemfonts.lua',
    install_dir: '/plugins')