# 正确版：按节处理，给指定节末尾追加 sealed=1
import io, os, re

targets = [r'D:\Deepseek-Harness\work\260918_143041\config\FSDTrainer.ini',
           r'D:\Deepseek-Harness\work\260918_143041\out\verify\FSDTrainer.ini']
wanted = ['显示敌人', '矿物透视', '奔跑速度', '镐子攻速', '夜视']

for p in targets:
    if not os.path.exists(p):
        print('skip', p); continue
    s = io.open(p, encoding='utf-16').read()
    s = s.replace('\r\n', '\n')
    parts = re.split(r'(?m)^(\[feature\.[^\]]+\])\s*$', s)
    # parts: [pre, header1, body1, header2, body2, ...]
    out = [parts[0]]
    for i in range(1, len(parts), 2):
        head, body = parts[i], parts[i+1]
        name = head[len('[feature.'):-1]
        if name in wanted and 'sealed=1' not in body:
            body = body.rstrip('\n') + '\nsealed=1\n\n'
        out.append(head)
        out.append(body)
    io.open(p, 'w', encoding='utf-16', newline='').write(''.join(out))
    print('patched', os.path.basename(p))

# 复核
s = io.open(targets[0], encoding='utf-16').read()
for name in wanted + ['霸体', '移速设置', '怪物全灭']:
    key = '[feature.' + name + ']'
    body = s.split(key, 1)[1].split('\n[', 1)[0] if key in s else ''
    print(name.encode('unicode_escape').decode(), 'sealed=' + ('1' if 'sealed=1' in body else '0'))
