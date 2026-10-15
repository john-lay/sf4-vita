"""Generate loader/source/java_stubs.h: a logged stub for every GlTest2Renderer
static method that loader/source/java.c doesn't implement.

Usage (paths are your own local files, never committed):
    python3 tools/jni_refs.py <libGltest2jni.so> > jnirefs.txt      # needs ~/sf4-decomp from jadx
    python tools/gen_java_stubs.py jnirefs.txt loader/source/java.c loader/source/java_stubs.h
"""
import re, sys
refs = open(sys.argv[1], encoding='utf-8').read()
java = open(sys.argv[2], encoding='utf-8').read()
known = set(re.findall(r'\{ *\d+, *"([^"]+)", *METHOD_TYPE_', java))
TYPES = {'void': 'VOID', 'int': 'INT', 'boolean': 'BOOLEAN', 'long': 'LONG',
         'float': 'FLOAT', 'double': 'DOUBLE', 'char': 'CHAR'}
BS = chr(92)  # backslash for macro line continuations
seen, rows = set(), []
for cls, name, ret in re.findall(r'^  (GlTest2Renderer|GlTest2Activity)\.(\w+)\([^)]*\) -> (\S+)', refs, re.M):
    if cls != 'GlTest2Renderer' or name in known or name in seen:
        continue
    seen.add(name)
    rows.append((name, TYPES.get(ret, 'OBJECT')))
ids = {n: 1000 + i for i, (n, _) in enumerate(rows)}
out = ['/* Generated from the decompiled GlTest2Renderer: every static method native',
       ' * code can call that java.c does not implement. Each one logs its name when',
       ' * called and returns 0 / false / "" (see stub_* in java.c). */', '']
out.append('#define SF4_STUB_NAMES ' + BS)
out += ['    { %d, "%s", METHOD_TYPE_%s }, %s' % (ids[n], n, t, BS) for n, t in rows]
out.append('')
for t in sorted(set(t for _, t in rows)):
    out.append('#define SF4_STUBS_%s %s' % (t, BS))
    out += ['    { %d, stub_%s }, %s' % (ids[n], t.lower(), BS) for n, tt in rows if tt == t]
    out.append('')
out.append('/* end of generated stubs */')
# Binary write: always LF line endings, and the last macro line is followed by
# a real line rather than the end of the file.
open(sys.argv[3], 'wb').write(('\n'.join(out) + '\n').encode())
sys.stderr.write('%d stubs, types: %s\n' % (len(rows), sorted(set(t for _, t in rows))))
