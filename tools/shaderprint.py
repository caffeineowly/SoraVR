#!/usr/bin/env python3
"""Code fingerprint of a vertex shader disassembly (a SoraVR_VS_<hash>.txt hunting dump).

Same normalisation as ShaderPrint() in src/soravr.cpp: comments and blank lines dropped, and every
number on structured-buffer lines blanked, so a shader whose only change is its instance record
layout (as in game 1.07) keeps its fingerprint. Usage: tools/shaderprint.py dump.txt [...]
"""
import re, sys


def fingerprint(text):
    out = []
    for line in text.replace('\r', '').split('\n'):
        line = line.rstrip()
        s = line.lstrip()
        if not s or s.startswith('//'):
            continue
        if s.startswith('ld_structured') or s.startswith('dcl_resource_structured'):
            line = re.sub(r'[0-9]+', '#', line)
        out.append(line)
    h = 0xcbf29ce484222325
    for c in '\n'.join(out).encode('latin1'):
        h = ((h ^ c) * 0x100000001b3) & 0xffffffffffffffff
    return h


if __name__ == '__main__':
    for p in sys.argv[1:]:
        print(f'{fingerprint(open(p, "rb").read().rstrip(b"\0").decode("latin1")):016x}  {p}')
