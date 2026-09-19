import struct, sys

path = sys.argv[1]
d = open(path,'rb').read()
e_lfanew = struct.unpack_from('<I', d, 0x3C)[0]
assert d[e_lfanew:e_lfanew+4] == b'PE\0\0'
coff = e_lfanew+4
nsec = struct.unpack_from('<H', d, coff+2)[0]
optsz = struct.unpack_from('<H', d, coff+16)[0]
opt = coff+20
magic = struct.unpack_from('<H', d, opt)[0]
pe32p = magic == 0x20b
ddir = opt + (112 if pe32p else 96)
exp_rva, exp_size = struct.unpack_from('<II', d, ddir)
secs = []
so = opt + optsz
for i in range(nsec):
    b = so + i*40
    name = d[b:b+8].rstrip(b'\0').decode()
    vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', d, b+8)
    secs.append((name, vaddr, vsize, raddr, rsize))
def rva2off(rva):
    for name,vaddr,vsize,raddr,rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr)
    return None
eo = rva2off(exp_rva)
nfun, nnam = struct.unpack_from('<II', d, eo+20)
afun, anam, aord = struct.unpack_from('<III', d, eo+28)
funs = [struct.unpack_from('<I', d, rva2off(afun)+4*i)[0] for i in range(nfun)]
names = [struct.unpack_from('<I', d, rva2off(anam)+4*i)[0] for i in range(nnam)]
orders = [struct.unpack_from('<H', d, rva2off(aord)+2*i)[0] for i in range(nnam)]
exp = {}
for nm_rva, ordi in zip(names, orders):
    no = rva2off(nm_rva)
    s = d[no:d.index(b'\0', no)].decode('ascii','replace')
    if ordi < len(funs):
        exp.setdefault(funs[ordi], []).append(s)
print("exports:", len(exp), "image size:", hex(len(d)))
targets = [int(x,16) for x in sys.argv[2:]]
allr = sorted(exp)
for t in targets:
    prev = [r for r in allr if r <= t]
    nxt = [r for r in allr if r > t]
    p = prev[-1] if prev else None
    n = nxt[0] if nxt else None
    print("0x%X  prev=%s %s   next=%s %s" % (t, hex(p) if p else '-', exp.get(p), hex(n) if n else '-', exp.get(n)))
