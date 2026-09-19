# -*- coding: utf-8 -*-
"""精简 MINIDUMP 解析：异常码 / 异常地址 / 所属模块（只读，用于定位崩溃隐患）"""
import struct, sys, io

path = sys.argv[1]
d = open(path, 'rb').read()
u32 = lambda o: struct.unpack_from('<I', d, o)[0]
u64 = lambda o: struct.unpack_from('<Q', d, o)[0]
assert d[:4] == b'MDMP', d[:4]
n = u32(8)
rva = u32(12)
dirs = {}
for i in range(n):
    t, sz, r = struct.unpack_from('<III', d, rva + i * 12)
    dirs.setdefault(t, []).append((sz, r))

mods = []
if 4 in dirs:
    sz, r = dirs[4][0]
    nm_count = u32(r)
    for i in range(nm_count):
        base = r + 4 + i * 108
        img = u64(base)
        size = u32(base + 8)
        nameRva = u32(base + 20)
        if nameRva + 4 <= len(d):
            ln = u32(nameRva)
            if ln < 520 and nameRva + 4 + ln <= len(d):
                nm = d[nameRva + 4:nameRva + 4 + ln].decode('utf-16-le', 'replace')
                mods.append((img, size, nm))
print('modules', len(mods))
out = []
if 6 in dirs:
    sz, r = dirs[6][0]
    tid = u32(r)
    code = u32(r + 8)
    flags = u32(r + 12)
    addr = u64(r + 24)
    nparam = u32(r + 32)
    params = [u64(r + 40 + 8 * k) for k in range(min(nparam, 15))]
    print('ExceptionCode=0x%08X flags=%d addr=0x%X thread=%d' % (code, flags, addr, tid))
    print('params', [hex(p) for p in params])
    hit = None
    for img, size, nm in mods:
        if img <= addr < img + size:
            hit = (nm, addr - img)
            break
    print('faulting module:', hit)
else:
    print('no exception stream')
# 打印最后几个模块名（找我们的 dll / UE4SS）
print('modules tail:')
for img, size, nm in mods[-8:]:
    print('   0x%X %s' % (img, nm))
