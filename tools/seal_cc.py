# 给 [feature.霸体] 加 sealed=1（config / 游戏目录 / verify 三份 ini，UTF-16LE）
import io, os

targets = [
    r'D:\Deepseek-Harness\work\260918_143041\config\FSDTrainer.ini',
    r'E:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\FSDTrainer.ini',
    r'D:\Deepseek-Harness\work\260918_143041\out\verify\FSDTrainer.ini',
]
for p in targets:
    if not os.path.exists(p):
        print('skip', p); continue
    s = io.open(p, encoding='utf-16').read()
    if '[feature.霸体]' not in s:
        print('no section', p); continue
    head, _, tail = s.partition('[feature.霸体]')
    # 该节到下一个 [ 之前
    body, _, rest = tail.partition('\n[')
    if 'sealed=1' in body:
        print('already sealed', os.path.basename(p)); continue
    body = body.replace('\r\n', '\n').rstrip('\n') + '\nsealed=1\n'
    s2 = head + '[feature.霸体]' + body + '\n[' + rest if rest else head + '[feature.霸体]' + body
    io.open(p, 'w', encoding='utf-16', newline='').write(s2)
    print('sealed ->', p)
