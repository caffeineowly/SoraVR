#!/usr/bin/env python3
"""Check SoraVR's game code signatures against one or more sora_1st.exe builds.

Usage: tools/sigcheck.py path/to/sora_1st.exe [another.exe ...]

Reads the kSig* patterns from src/soravr.cpp and reports, per exe, where each one matches.
Every pattern must match exactly once, or the mod switches that feature off on that game version.
"""
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'src', 'soravr.cpp')


def load_sigs():
    text = open(SRC, encoding='utf-8').read()
    return re.findall(r'static const char (kSig\w+)\[\]\s*=\s*"([0-9a-fA-F? ]+)"', text)


def exec_sections(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    n = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    stamp = struct.unpack_from('<I', d, pe + 8)[0]
    o = pe + 24 + opt
    out = []
    for i in range(n):
        name, vs, va, rs, ra, *_ = struct.unpack_from('<8sIIIIIIHHI', d, o + 40 * i)
        chars = struct.unpack_from('<I', d, o + 40 * i + 36)[0]
        if chars & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
            out.append((va, d[ra:ra + min(vs, rs)]))
    return stamp, out


def find(sections, pat):
    rx = b''.join(b'.' if t.startswith('?') else re.escape(bytes([int(t, 16)])) for t in pat.split())
    return [va + m.start() for va, data in sections for m in re.finditer(rx, data, re.S)]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    sigs = load_sigs()
    bad = 0
    for exe in sys.argv[1:]:
        stamp, secs = exec_sections(exe)
        print(f'{os.path.basename(exe)}  (build stamp {stamp:08x})')
        for name, pat in sigs:
            hits = find(secs, pat)
            ok = len(hits) == 1
            bad += not ok
            where = ', '.join(f'exe+{h:x}' for h in hits[:4]) or '-'
            print(f'  {"ok " if ok else "BAD"} {name:16s} {len(hits)} match(es)  {where}')
    print('all signatures unique' if not bad else f'{bad} signature(s) not unique')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
