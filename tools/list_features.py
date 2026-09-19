import io, re, sys
p = r'D:\Deepseek-Harness\work\260918_143041\config\FSDTrainer.ini'
s = io.open(p, encoding='utf-16').read()
secs = re.split(r'(?m)^(\[feature\.[^\]]+\])\s*$', s)
print('sections:', (len(secs)-1)//2)
for i in range(1, len(secs), 2):
    b = secs[i+1]
    cat = re.search(r'cat=(\S+)', b)
    cat = cat.group(1) if cat else '?'
    if cat != '常规':
        continue
    ui = re.search(r'ui=(\S+)', b)
    k = re.search(r'key=(\S+)', b)
    print('  %-16s ui=%-7s key=%-16s def=%-6s %s' % (
        secs[i][9:-1], ui.group(1) if ui else '-', k.group(1) if k else '-',
        (re.search(r'def=(\S+)', b).group(1) if re.search(r'def=(\S+)', b) else '-'),
        'SEALED' if 'sealed=1' in b else ''))
