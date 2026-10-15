"""List the undefined dynamic symbols (imports) of one or more ARM ELF shared objects.

Usage: python tools/list_imports.py lib1.so [lib2.so ...]

When several libs are given, symbols that one of them exports are reported as
resolved internally (e.g. libGltest2jni.so -> libc++_shared.so), so what is
left is what the Vita loader must provide.
"""
import sys
from collections import defaultdict
from elftools.elf.elffile import ELFFile


def classify(name):
    if name.startswith('gl'):
        return 'gles1'
    if name.startswith('pthread_') or name.startswith('sched_'):
        return 'pthread'
    if name.startswith('__cxa_') or name.startswith('__gxx_') or name.startswith('_Unwind'):
        return 'c++abi'
    if name.startswith('_Z'):
        return 'c++'
    if name.startswith('__aeabi_'):
        return 'aeabi'
    if name.startswith('dl'):
        return 'dl'
    if name in ('compress', 'uncompress', 'crc32'):
        return 'zlib'
    if name.startswith('__android_log'):
        return 'log'
    if name.endswith('_chk') or name.startswith('__') or name in ('getauxval',):
        return 'bionic-specific'
    return 'libc/libm'


def dynsyms(path):
    with open(path, 'rb') as f:
        elf = ELFFile(f)
        imports, exports = {}, set()
        for sym in elf.get_section_by_name('.dynsym').iter_symbols():
            if not sym.name:
                continue
            if sym.entry.st_shndx == 'SHN_UNDEF':
                imports[sym.name] = sym.entry.st_info.bind
            else:
                exports.add(sym.name)
        return imports, exports


def main(paths):
    libs = {p: dynsyms(p) for p in paths}
    all_exports = set().union(*(e for _, e in libs.values()))
    for path, (imports, _) in libs.items():
        groups = defaultdict(list)
        internal = 0
        for name, bind in sorted(imports.items()):
            if name in all_exports:
                internal += 1
                continue
            groups[classify(name)].append(name + (' (weak)' if bind == 'STB_WEAK' else ''))
        total = sum(len(v) for v in groups.values())
        print(f'## {path.replace(chr(92), "/").split("/")[-1]}: {total} external imports'
              f' ({internal} resolved by the other libs given)\n')
        for group in sorted(groups):
            print(f'### {group} ({len(groups[group])})\n')
            print(' '.join(groups[group]) + '\n')


if __name__ == '__main__':
    main(sys.argv[1:])
