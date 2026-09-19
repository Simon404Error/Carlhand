import struct, sys, io

path = sys.argv[1]
d = open(path, 'rb').read()

def u32(o): return struct.unpack_from('<I', d, o)[0]
def u64(o): return struct.unpack_from('<Q', d, o)[0]

assert d[:4] == b'MDMP', d[:4]
nstreams = u32(8)
rva = u32(12)
streams = {}
for i in range(nstreams):
    t, sz, r = struct.unpack_from('<III', d, rva + i*12)
    streams.setdefault(t, []).append((sz, r))

MODULE_LIST=4; EXCEPTION=6; SYSTEM_INFO=7; MEMORY_LIST=5; THREAD_LIST=3

# modules
mods=[]
if MODULE_LIST in streams:
    sz, r = streams[MODULE_LIST][0]
    n = u32(r)
    for i in range(n):
        base = r + 4 + i*108
        nameRva = u32(base+64)
        # MINIDUMP_STRING
        ln = u32(nameRva)
        nm = d[nameRva+4:nameRva+4+ln].decode('utf-16-le', 'replace')
        start = u64(base+8)  # BaseOfImage is at 8? layout: VersionInfo(4+4)=8, then ...
        mods.append((nm, u32(base+8+8+4+4+4+4+4+4) if False else None))

# redo modules properly: MINIDUMP_MODULE: BaseOfImage(8) SizeOfImage(4) CheckSum(4) TimeDateStamp(4) ModuleNameRva(4) ...
mods=[]
if MODULE_LIST in streams:
    sz, r = streams[MODULE_LIST][0]
    n = u32(r)
    for i in range(n):
        base = r + 4 + i*108
        b = u64(base)
        size = u32(base+8)
        nameRva = u32(base+20)
        ln = u32(nameRva)
        nm = d[nameRva+4:nameRva+4+ln].decode('utf-16-le','replace')
        mods.append((b, size, nm))
print("streams:", sorted(streams.keys()))
print("modules:", len(mods))
def modof(addr):
    best=None
    for b,s,nm in mods:
        if b <= addr < b+s: return nm, addr-b
    for b,s,nm in mods:
        if b <= addr: best=(nm, addr-b)
    return best or (None,None)

if EXCEPTION in streams:
    sz, r = streams[EXCEPTION][0]
    tid = u32(r)
    er = r + 8
    code = u32(er); flags = u32(er+4)
    rec = u64(er+8); addr = u64(er+16)
    npar = u32(er+24)
    params = [u64(er+32+8*i) for i in range(min(npar,15))]
    nm, off = modof(addr)
    print("EXCEPTION thread=%d code=0x%08X flags=0x%X addr=0x%X module=%s off=0x%X" % (tid, code, flags, addr, nm, off or 0))
    print("params:", [hex(p) for p in params])
    # context
    ctx = er + 32 + 15*8
    # MINIDUMP_LOCATION_DESCRIPTOR
    csize = u32(ctx); crva = u32(ctx+4)
    print("ctx size", csize)
    if csize >= 0x4a0:
        # CONTEXT AMD64: Rip at 0xF8, Rsp 0x98, Rbp 0xA0, Rbx 0x90, Rax 0x78, Rcx 0x80, Rdx 0x88, Rsi 0xA8, Rdi 0xB0
        rip = u64(crva+0xF8); rsp = u64(crva+0x98); rbp = u64(crva+0xA0)
        print("RIP=0x%X (%s+0x%X)" % (rip, *((modof(rip)[0], modof(rip)[1]))))
        print("RSP=0x%X RBP=0x%X" % (rsp, rbp))
        regnames = {0x78:'RAX',0x80:'RCX',0x88:'RDX',0x90:'RBX',0xA8:'RSI',0xB0:'RDI',0xB8:'R8',0xC0:'R9',0xC8:'R10',0xD0:'R11',0xD8:'R12',0xE0:'R13',0xE8:'R14',0xF0:'R15'}
        print(" ".join("%s=0x%X" % (n, u64(crva+o)) for o, n in sorted(regnames.items())))
        # stack scan
        print("--- stack (return addresses) ---")
        for i in range(0, 0x600, 8):
            val = u64(crva + 0x98 + i) if False else None
        # read stack from memory list
        # build memory ranges
        ranges=[]
        if MEMORY_LIST in streams:
            sz2, r2 = streams[MEMORY_LIST][0]
            n2 = u32(r2)
            for j in range(n2):
                st = r2+4+j*16
                saddr = u64(st); ssz = u32(st+8); srva = u32(st+12)
                ranges.append((saddr, ssz, srva))
        def rd(addr, ln):
            for saddr, ssz, srva in ranges:
                if saddr <= addr < saddr+ssz:
                    o = srva + (addr-saddr)
                    return d[o:o+min(ln, saddr+ssz-addr)]
            return b''
        for i in range(0, 0x400, 8):
            val = struct.unpack_from('<Q', rd(rsp+i, 8) or b'\0'*8, 0)[0]
            m = modof(val)
            if m[0]:
                print("  [rsp+0x%03X] 0x%X %s+0x%X" % (i, val, m[0], m[1]))
else:
    print("no exception stream")
