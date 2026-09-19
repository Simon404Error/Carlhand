# 按用户要求改 ini：封印 显示敌人 / 奔跑速度 / 镐子攻速 / 夜视；显示采集 → 改名"矿物透视"并封印
import io, os

targets = [r'D:\Deepseek-Harness\work\260918_143041\config\FSDTrainer.ini']
for extra in [r'D:\Deepseek-Harness\work\260918_143041\out\verify\FSDTrainer.ini']:
    if os.path.exists(extra):
        targets.append(extra)

def patch(path):
    s = io.open(path, encoding='utf-16').read()
    lines = s.split('\n')
    out, sec = [], None
    renamed = 0
    wanted = {'显示敌人', '矿物透视', '奔跑速度', '镐子攻速', '夜视'}
    i = 0
    while i < len(lines):
        ln = lines[i]
        st = ln.strip()
        if st.startswith('[feature.'):
            name = st[len('[feature.'):-1]
            if name == '显示采集':
                ln = ln.replace('[feature.显示采集]', '[feature.矿物透视]')
                name = '矿物透视'
                renamed += 1
            sec = name
        out.append(ln)
        # 在节内最后一个键后追加 sealed=1（放在 addr= 后面最简单）
        if sec in wanted and st.startswith('addr='):
            nxt = lines[i+1].strip() if i+1 < len(lines) else ''
            if not nxt.startswith('sealed=') and not nxt.startswith('['):
                out.append('sealed=1')
        i += 1
    io.open(path, 'w', encoding='utf-16', newline='').write('\n'.join(out))
    print('patched', os.path.basename(path), 'renamed=', renamed)

for t in targets:
    patch(t)
