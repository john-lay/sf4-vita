"""List the JNI signatures and GlTest2Renderer/GlTest2Activity static methods
that libGltest2jni.so refers to (by matching strings in the .so against the
method names in a local jadx decompile at ~/sf4-decomp/out). Output feeds
tools/gen_java_stubs.py.

Usage: python3 tools/jni_refs.py <libGltest2jni.so> > jnirefs.txt
"""
import re,sys,os,glob
so=open(sys.argv[1],'rb').read()
strs=set(m.decode() for m in re.findall(rb'[\x20-\x7e]{2,}',so))
sigs=sorted(s for s in strs if re.fullmatch(r'\([\[A-Za-z0-9/;$_]*\)[\[A-Za-z0-9/;$_]+',s))
print(len(sigs),'JNI signatures'); print(' '.join(sigs)); print()
src=os.path.expanduser('~/sf4-decomp/out/sources/jp/gltest2/android')
names={}
for f in glob.glob(src+'/*.java'):
    for m in re.finditer(r'public static (?:synchronized )?([\w.<>\[\]]+) (\w+)\(([^)]*)\)',open(f,encoding='utf-8',errors='ignore').read()):
        names.setdefault(m.group(2),[]).append((os.path.basename(f)[:-5],m.group(1),m.group(3)))
hit=sorted(n for n in names if n in strs)
print(len(hit),'Java static methods named in the .so:')
for n in hit:
    for cls,ret,args in names[n]: print(f'  {cls}.{n}({args}) -> {ret}')
other=sorted(s for s in strs if re.fullmatch(r'(android|java|jp)/[\w/$]+',s))
print('\nclasses:',' '.join(other))
