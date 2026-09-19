import struct, sys
p = sys.argv[1]
d = open(p, 'rb').read()
e = struct.unpack_from('<I', d, 0x3C)[0]
assert d[e:e+4] == b'PE\0\0', 'not PE'
coff = e + 4
mach, nsec, _, _, _, optsz, chars = struct.unpack_from('<HHIIIHH', d, coff)
opt = coff + 20
magic = struct.unpack_from('<H', d, opt)[0]
pe32p = magic == 0x20b
print('machine=0x%04X (0x8664=x64) sections=%d characteristics=0x%04X' % (mach, nsec, chars))
print('  IMAGE_FILE_RELOCS_STRIPPED =', bool(chars & 0x0001))
print('  IMAGE_FILE_DLL             =', bool(chars & 0x2000))
dllch = struct.unpack_from('<H', d, opt + (0x46 if pe32p else 0x46))[0]
print('dllcharacteristics=0x%04X  DYNAMIC_BASE(ASLR)=%s  NX=%s  FORCE_INTEGRITY=%s' % (
    dllch, bool(dllch & 0x40), bool(dllch & 0x100), bool(dllch & 0x80)))
ep = struct.unpack_from('<I', d, opt + 16)[0]
base = struct.unpack_from('<Q', d, opt + 24)[0] if pe32p else struct.unpack_from('<I', d, opt + 28)[0]
print('preferred image base = 0x%X   entrypoint RVA = 0x%X' % (base, ep))
so = opt + optsz
names = []
for i in range(nsec):
    b = so + i * 40
    nm = d[b:b+8].rstrip(b'\0').decode('latin1')
    vsz, va, rsz, ra = struct.unpack_from('<IIII', d, b + 8)
    ch = struct.unpack_from('<I', d, b + 36)[0]
    names.append(nm)
    print('  %-8s vsize=0x%-8X rva=0x%-8X raw=0x%-8X rawsize=0x%-8X ch=0x%08X' % (nm, vsz, va, ra, rsz, ch))
print('has .reloc =', any(n.lower().startswith('.reloc') for n in names))
# import table
ddir = opt + (112 if pe32p else 96)
imp_rva, imp_size = struct.unpack_from('<II', d, ddir + 8)
print('import dir rva=0x%X size=0x%X' % (imp_rva, imp_size))
def rva2off(r):
    for i in range(nsec):
        b = so + i * 40
        vsz, va, rsz, ra = struct.unpack_from('<IIII', d, b + 8)
        if va <= r < va + max(vsz, rsz):
            return ra + (r - va)
    return None
if imp_rva:
    o = rva2off(imp_rva)
    n = 0
    if o:
        while True:
            ent = d[o + n*20:o + n*20 + 20]
            if len(ent) < 20 or ent == b'\0'*20: break
            nr = struct.unpack_from('<I', ent, 12)[0]
            if nr == 0: break
            no = rva2off(nr)
            nm = d[no:d.index(b'\0', no)].decode('latin1') if no else '?'
            print('   import:', nm)
            n += 1
            if n > 20: break
    print('   (import descriptors:', n, ')')
