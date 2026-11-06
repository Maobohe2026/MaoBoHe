import struct

P = r'E:\Documents\deepseek-harness\default-workspace\qmfindpic-single\dist\zaoyo.dll'
OUT = r'E:\Documents\deepseek-harness\default-workspace\qmfindpic-single\build\scan.txt'
d = open(P, 'rb').read()
lines = []
lines.append('file=%s size=%d' % (P, len(d)))

for s in (b'IsDebuggerPresent', b'CheckHost', b'VMProtect', b'OutputDebugStringA',
          b'NtQueryInformationProcess', b'msvcrt', b'MFC42', b'mscoree'):
    lines.append('  %-26s %s' % (s.decode(), 'YES' if s in d else 'no'))

e = struct.unpack_from('<I', d, 0x3C)[0]
coff = e + 4
nsec = struct.unpack_from('<H', d, coff + 2)[0]
opt = coff + 20
magic = struct.unpack_from('<H', d, opt)[0]
dd = opt + (96 if magic == 0x10b else 112)
erva, esz = struct.unpack_from('<II', d, dd)
szopt = struct.unpack_from('<H', d, coff + 16)[0]
s0 = opt + szopt
secs = []
for i in range(nsec):
    o = s0 + i * 40
    vsz, va, rsz, rp = struct.unpack_from('<IIII', d, o + 8)
    secs.append((va, vsz, rp, rsz))


def r2o(r):
    for va, vsz, rp, rsz in secs:
        if va <= r < va + max(vsz, rsz):
            return rp + (r - va)
    return None


names = []
o = r2o(erva)
if o is not None:
    while True:
        ent = d[o:o + 20]
        if len(ent) < 20 or ent == b'\0' * 20:
            break
        nr = struct.unpack_from('<I', ent, 12)[0]
        no = r2o(nr)
        if no is None:
            break
        end = d.index(b'\0', no)
        names.append(d[no:end].decode('latin1'))
        o += 20
lines.append('imports: ' + ', '.join(names))

open(OUT, 'w', encoding='utf-8').write('\n'.join(lines))
print('written', OUT)
