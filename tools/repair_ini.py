# 修上一步 unseal 把换行吃掉导致 "[feature." 粘在上一行的问题，并复核封印状态
import io, re
p = r'D:\Deepseek-Harness\work\260918_143041\config\FSDTrainer.ini'
s = io.open(p, encoding='utf-16').read().replace('\r\n', '\n')

# 1) 任何 "[feature." 不在行首 → 前面补空行
s2 = re.sub(r'(?<!\n)(\[feature\.)', r'\n\n\1', s)
# 2) 行尾没有换行的 sealed=X 后面紧跟 [feature. 的情况（双保险）
s2 = re.sub(r'(sealed=[01])[ \t]*(\[feature\.)', r'\1\n\n\2', s2)
if s2 != s:
    print('repaired glued headers:', s.count('[feature.') - s2.count('\n[feature.'))
    io.open(p, 'w', encoding='utf-16', newline='').write(s2)
else:
    print('no glued header found')

# 3) 复核
s = io.open(p, encoding='utf-16').read()
secs = re.split(r'(?m)^(\[feature\.[^\]]+\])\s*$', s)
ok = 0
for i in range(1, len(secs), 2):
    b = secs[i + 1]
    cat = re.search(r'cat=(\S+)', b)
    if cat and cat.group(1) == '常规':
        ok += 1
print('常规页功能数:', ok)
for name in ['奔跑速度', '镐子攻速', '飞行模式', '设置血量', '立刻补给', '自杀', '杀某个队友', '杀全队', '醉酒-玩家编号', '醉酒-数值']:
    key = '[feature.' + name + ']'
    if key not in s:
        print('  缺失:', name); continue
    b = s.split(key, 1)[1].split('\n[', 1)[0]
    print('  %-14s sealed=%s cat=%s' % (name, '1' if 'sealed=1' in b else '0',
          (re.search(r'cat=(\S+)', b).group(1) if re.search(r'cat=(\S+)', b) else '?')))
