# -*- coding: utf-8 -*-
"""把超长的 Lua 块（>16380 字节，MSVC 上限）从合适的位置拆成两块。
拆点：块内最后一段连续注释里的分隔行 "-- =====" / 空行，尽量靠中间。"""
import io, re

P = r'D:\Deepseek-Harness\work\260918_143041\src\cheat_engine.h'
t = io.open(P, encoding='utf-8').read()

# 找出所有 R"FSDLUA( ... )FSDLUA" 块
blocks = []
i = 0
while True:
    a = t.find('R"FSDLUA(', i)
    if a < 0:
        break
    b = t.find(')FSDLUA"', a)
    if b < 0:
        break
    blocks.append((a, b))
    i = b + 1
print('块数 =', len(blocks))
LIMIT = 16380
for idx, (a, b) in enumerate(blocks):
    size = len(t[a + len('R"FSDLUA('):b].encode('utf-8'))
    print('  块%d 大小 %d 字节 %s' % (idx, size, '← 超限' if size > LIMIT else ''))

# 从后往前处理（拆块会改变索引）
for idx in range(len(blocks) - 1, -1, -1):
    a, b = blocks[idx]
    body_start = a + len('R"FSDLUA(')
    body = t[body_start:b]
    total = len(body.encode('utf-8'))
    if total <= LIMIT:
        continue
    lines = body.split('\n')
    best = None
    acc = 0
    for k, l in enumerate(lines):
        acc += len((l + '\n').encode('utf-8'))
        if l.strip().startswith('-- ====') or l.strip() == '':
            if total * 0.35 < acc < total * 0.8:
                if best is None or abs(acc - total / 2) < abs(best[1] - total / 2):
                    best = (k, acc)
    if best is None:
        print('!! 块%d 找不到合适拆点' % idx)
        continue
    k = best[0]
    left = '\n'.join(lines[:k + 1])
    right = '\n'.join(lines[k + 1:])
    new = 'R"FSDLUA(' + left + '\n)FSDLUA",\nR"FSDLUA(' + right + ')FSDLUA"'
    t = t[:a] + new + t[b + len(')FSDLUA"'):]
    print('已拆块%d：拆点第 %d 行（左 %d / 右 %d 字节）'
          % (idx, k, len(left.encode('utf-8')), len(right.encode('utf-8'))))

# 段数同步
m = re.search(r'kCheatEngineLuaPartCount\s*=\s*(\d+)', t)
if m:
    old = int(m.group(1))
    cnt = t.count('R"FSDLUA(')
    if old != cnt:
        t = t.replace(m.group(0), 'kCheatEngineLuaPartCount = %d' % cnt, 1)
        print('kCheatEngineLuaPartCount: %d -> %d' % (old, cnt))
io.open(P, 'w', encoding='utf-8').write(t)
print('完成，现在块数 =', t.count('R"FSDLUA('))
