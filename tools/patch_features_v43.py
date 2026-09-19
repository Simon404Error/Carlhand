# 常规页新增功能 + 解封（ini）
import io, re, os

paths = [r'D:\Deepseek-Harness\work\260918_143041\config\FSDTrainer.ini']

def load(p):
    s = io.open(p, encoding='utf-16').read().replace('\r\n', '\n')
    parts = re.split(r'(?m)^(\[feature\.[^\]]+\])\s*$', s)
    secs = []
    for i in range(1, len(parts), 2):
        secs.append([parts[i], parts[i+1]])
    return parts[0], secs

def dump(p, pre, secs):
    out = pre
    for h, b in secs:
        out += h + '\n' + b
    io.open(p, 'w', encoding='utf-16', newline='').write(out)

def body_get(b, k, d=''):
    m = re.search(r'(?m)^' + k + r'=(.*)$', b)
    return m.group(1) if m else d

def body_set(b, k, v):
    if re.search(r'(?m)^' + k + r'=', b):
        return re.sub(r'(?m)^' + k + r'=.*$', k + '=' + v, b)
    return b.rstrip('\n') + '\n' + k + '=' + v + '\n'

def unseal(b):
    return re.sub(r'(?m)^sealed=1\s*$', 'sealed=0', b)

NEW = [
    ('立刻补给', 'cheat', 'action', 'resupply', '', '按一下立刻空投一个补给仓（C_Resupply）'),
    ('自杀', 'cheat', 'action', 'suicide', '', '按一下自杀（C_KillPlayer）'),
    ('杀某个队友', 'cheat', 'value', 'kill_slot', '1', '队友编号 1~4（0=关）；由 C_KillPlayer <编号> 实现，游戏若不支持编号会落到自己身上'),
    ('杀全队', 'cheat', 'action', 'kill_team', '', '按一下杀掉全队（C_KillTeam）'),
    ('醉酒-玩家编号', 'cheat', 'value', 'intox_player', '0', '0=全队一起设；1~4=只对某个玩家'),
    ('醉酒-数值', 'cheat', 'value', 'intox_value', '0', '醉酒度 0~100（0=不发送）'),
]

for p in paths:
    pre, secs = load(p)
    # 1) 解封三项
    for h, b in secs:
        name = h[len('[feature.'):-1]
        if name in ('奔跑速度', '镐子攻速', '飞行模式'):
            b2 = unseal(b)
            # 完善说明
            if name == '奔跑速度':
                b2 = body_set(b2, 'note', '倍率>1 即开启游戏自带"快速移动"（唯一真生效的方式，写 MaxWalkSpeed 会被游戏覆盖）；=1 关闭')
            if name == '镐子攻速':
                b2 = body_set(b2, 'note', '倍率>1 即开启游戏自带 SetQuadPickAxe（四倍镐）；=1 关闭')
            if name == '飞行模式':
                b2 = body_set(b2, 'note', '开启/关闭游戏自带飞行 C_SetFlyMode（用 IsFlyModeActive 校验状态）')
            secs[secs.index([h, b])] = [h, b2]
            print('unsealed', p.encode('unicode_escape').decode() if False else name)

    # 2) 把特殊功能里的 set_health 挪到常规页
    keymap = {}
    for idx, (h, b) in enumerate(secs):
        k = body_get(b, 'key')
        if k:
            keymap.setdefault(k, []).append(idx)
    assert len(keymap.get('set_health', [])) <= 1, 'set_health 重复'
    if 'set_health' in keymap:
        i = keymap['set_health'][0]
        h, b = secs[i]
        b = body_set(b, 'cat', '常规')
        b = unseal(b)
        b = body_set(b, 'ui', 'value')
        b = body_set(b, 'lo', '1')
        b = body_set(b, 'hi', '100000')
        b = body_set(b, 'def', '0')
        b = body_set(b, 'unit', '点(0=关)')
        b = body_set(b, 'note', '填大于 0 的值 → 发送 Cheat_SetHealth <值>；0=不发送')
        # 改名
        h2 = '[feature.设置血量]'
        secs[i] = [h2, b]
        # 挪到 常规 组末尾
        secs.append(secs.pop(i))
        print('moved set_health -> 常规 as 设置血量')
    else:
        print('警告: 没找到 set_health 节')

    # 3) 追加新功能（在最后一个 cat=常规 之后）
    last_reg = -1
    for idx, (h, b) in enumerate(secs):
        if body_get(b, 'cat') == '常规':
            last_reg = idx
    add = []
    for name, kind, ui, key, defv, note in NEW:
        assert key not in keymap, 'key 已存在: ' + key
        body = ('cat=常规\nkind=%s\n' % kind) + ('ui=%s\n' % ui) + ('key=%s\n' % key)
        if ui == 'value':
            body += 'lo=0\nhi=1000\ndef=%s\n' % defv
        else:
            body += 'def=0\n'
        body += 'note=%s\naddr=0\n\n' % note
        add.append(['[feature.%s]' % name, body])
    secs[last_reg + 1:last_reg + 1] = add
    dump(p, pre, secs)
    print('patched', p)
