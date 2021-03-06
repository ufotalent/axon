AddOption('--no-tcmalloc',
        dest='no_tcmalloc',
        action='store_true',
        help='whether to disable tcmalloc')
AddOption('--no-test',
        dest='no_test',
        action='store_true',
        help='whether to disable tests')

base_env = Environment(
    CCFLAGS = ['-ggdb', '-march=native', '-mtune=native', '-O2', '-std=c++0x'],
    #LINKFLAGS = ['-Wl,--as-needed', '-rdynamic', '-O2', '-lprofiler'],
    LINKFLAGS = ['-rdynamic', '-O2'],
    LIBS = ['pthread'],
    CPPPATH = ['.', 'include']
    )
if not GetOption('no_tcmalloc'):
    base_env.Append(LINKFLAGS = ['-ltcmalloc'])

conf = Configure(base_env)
# ==== check boost context
if not conf.CheckLibWithHeader('boost_context', 'boost/context/all.hpp', 'cpp'):
    print 'missing boost_context, exit'
    Exit(1)

# ==== build libaxon
libaxon_env = base_env.Clone()

libaxon_files = Glob('src/*.cpp')
libaxon_files += Glob('src/*/*.cpp')
libaxon_files += Glob('src/*/*/*.cpp')

libaxon_headers = Glob('src/*.hpp')
libaxon_headers += Glob('src/*/*.hpp')
libaxon_headers += Glob('src/*/*/*.hpp')
libaxon_env.Local(libaxon_headers)

libaxon_target = libaxon_env.SharedLibrary('axon', libaxon_files)


# ==== build test
if not GetOption('no_test'):
    test_env = libaxon_env.Clone()
    test_env.Append(
            LIBPATH = ['.'],
            LIBS = ['axon'],
            CPPPATH = ['usr/src/gtest/'],
            LINKFLAGS = ['-Wl,-rpath=\$$ORIGIN/../']
            )
    test_files = Glob('test/*.cpp')
    test_env.Repository('/usr/src/gtest/')
    gtest_files = ['src/gtest-all.cc', 'src/gtest_main.cc']

    for i in test_files:
        test_env.Program('test/' + i.name.partition('.')[0], [i] + gtest_files)
    test_env.Program('test/all', test_files + gtest_files)



# ==== build sample
sample_env = libaxon_env.Clone();
sample_env.Append(
        LIBPATH = ['.'],
        LIBS = ['axon'],
        LINKFLAGS = ['-Wl,-rpath=\$$ORIGIN/../']
        )
sample_files = Glob('sample/*.cc')
for i in sample_files:
    sample_env.Program('sample/' + i.name.partition('.')[0], [i])
# vim: ft=python
