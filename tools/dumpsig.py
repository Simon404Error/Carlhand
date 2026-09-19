import struct, sys, glob, os

def sig(path):
    d = open(path,'rb').read()
    if d[:4]!=b'MDMP': return ("notdump",)
    def u32(o): return struct.unpack_from('<I',d,o)[0]
    def u64(o): return struct.unpack_from('<Q',d,o)[0]
    n=u32(8); rva=u32(12)
    st={}
    for i in range(n):
        t,sz,r=struct.unpack_from('<III',d,rva+i*12); st.setdefault(t,[]).append((sz,r))
    mods=[]
    if 4 in st:
        sz,r=st[4][0]; cnt=u32(r)
        for i in range(cnt):
            b=r+4+i*108; base=u64(b); size=u32(b+8); nr=u32(b+20); ln=u32(nr)
            nm=d[nr+4:nr+4+ln].decode('utf-16-le','replace')
            mods.append((base,size,nm))
    def modof(a):
        for b,s,nm in mods:
            if b<=a<b+s: return nm, a-b
        return None,None
    if 6 not in st: return ("noexc",)
    sz,r=st[6][0]; er=r+8
    code=u32(er); addr=u64(er+16); npar=u32(er+24)
    pr=[u64(er+32+8*i) for i in range(min(npar,4))]
    ctx=er+32+15*8; csize=u32(ctx); crva=u32(ctx+4)
    rip=u64(crva+0xF8); rsp=u64(crva+0x98)
    rn,ro=modof(rip)
    # stack
    ranges=[]
    if 5 in st:
        sz2,r2=st[5][0]; n2=u32(r2)
        for j in range(n2):
            s0=r2+4+j*16; ranges.append((u64(s0),u32(s0+8),u32(s0+12)))
    def rd(a,ln):
        for s0,ss,sr in ranges:
            if s0<=a<s0+ss: return d[sr+(a-s0):sr+(a-s0)+ln]
        return b''
    seen=[]
    for i in range(0,0x200,8):
        v=struct.unpack_from('<Q',rd(rsp+i,8),0)[0]
        m,off=modof(v)
        if m and off is not None:
            seen.append((i,v,m,off))
    return code,addr,pr,rip,rn,ro,os.path.basename(rn or ''),[(hex(v),os.path.basename(m),hex(o)) for _,v,m,o in seen[:12]]

for p in sorted(glob.glob(sys.argv[1]), key=os.path.getmtime):
    try:
        s=sig(p)
    except Exception as e:
        s=("err",str(e))
    print(os.path.basename(p))
    print("   ", s)
