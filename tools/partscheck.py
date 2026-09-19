import re, sys
s = open(r'D:\Deepseek-Harness\work\260918_143041\src\cheat_engine.h', encoding='utf-8').read()
parts = re.findall(r'R"FSDLUA\((.*?)\)FSDLUA"', s, re.S)
print('parts', len(parts))
for i, p in enumerate(parts):
    print(i, 'bytes', len(p.encode('utf-8')), 'lines', p.count('\n'))
print('total bytes', sum(len(p.encode('utf-8')) for p in parts))
