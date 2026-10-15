"""Resolve ARM PLT call targets in an objdump disassembly of an Android .so.

Calls to exported functions inside libGltest2jni.so go through PLT stubs, so
objdump labels them as e.g. <android_setCpuArm@@Base+0x1c8c>. This maps each
PLT stub to the symbol whose GOT slot it jumps through and rewrites the call
targets.

Usage:
    arm-vita-eabi-objdump -d --no-show-raw-insn -C lib.so > dis.txt
    python tools/plt_calls.py lib.so dis.txt > dis_resolved.txt

Each ARM PLT entry is:
    add ip, pc, #A ; add ip, ip, #B ; ldr pc, [ip, #C]!
so the GOT slot is entry_address + 8 + A + B + C.
"""
import re
import subprocess
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


def demangle(names):
    try:
        out = subprocess.run(['c++filt'], input='\n'.join(names), capture_output=True, text=True).stdout
        return out.split('\n')[:len(names)]
    except FileNotFoundError:
        return names


def got_to_symbol(path):
    with open(path, 'rb') as f:
        elf = ELFFile(f)
        rel = elf.get_section_by_name('.rel.plt')
        dynsym = elf.get_section_by_name('.dynsym')
        names = {r['r_offset']: dynsym.get_symbol(r['r_info_sym']).name for r in rel.iter_relocations()}
    keys = list(names)
    return dict(zip(keys, demangle([names[k] for k in keys])))


def plt_map(dis_lines, got):
    stubs = {}
    pending = []
    insn = re.compile(r'^\s*([0-9a-f]+):\s+(add|ldr)\s+(.*)$')
    for line in dis_lines:
        m = insn.match(line)
        if not m:
            pending = []
            continue
        addr, op, args = int(m.group(1), 16), m.group(2), m.group(3)
        imm = re.search(r'#(-?(?:0x)?[0-9a-f]+)', args)
        if op == 'add' and args.startswith('ip, pc') and imm:
            pending = [(addr, int(imm.group(1), 0))]
        elif op == 'add' and args.startswith('ip, ip') and imm and len(pending) == 1:
            pending.append((addr, int(imm.group(1), 0)))
        elif op == 'ldr' and args.startswith('pc, [ip') and len(pending) == 2:
            c = int(imm.group(1), 0) if imm else 0
            start = pending[0][0]
            slot = start + 8 + pending[0][1] + pending[1][1] + c
            if slot in got:
                stubs[start] = got[slot]
            pending = []
        else:
            pending = []
    return stubs


def main(so, dis):
    lines = open(dis, encoding='utf-8', errors='replace').read().split('\n')
    stubs = plt_map(lines, got_to_symbol(so))
    sys.stderr.write('%d PLT stubs resolved\n' % len(stubs))
    call = re.compile(r'(\sblx?\s+)([0-9a-f]+) <[^>]*>')
    for line in lines:
        m = call.search(line)
        if m and int(m.group(2), 16) in stubs:
            line = line[:m.start()] + m.group(1) + m.group(2) + ' <' + stubs[int(m.group(2), 16)] + ' (plt)>' + line[m.end():]
        print(line)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
