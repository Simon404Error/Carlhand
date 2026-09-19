import re
p = r'D:\Deepseek-Harness\work\260918_143041\build\obj\engine_check.lua'
src = open(p, encoding='utf-8').read()
# strip long strings/comments roughly: remove -- comments
lines = []
for ln in src.split('\n'):
    ln = re.sub(r'--.*$', '', ln)
    lines.append(ln)
code = '\n'.join(lines)
defined = set()
for m in re.finditer(r'\blocal\s+function\s+([A-Za-z_]\w*)', code): defined.add(m.group(1))
for m in re.finditer(r'\bfunction\s+([A-Za-z_]\w*)', code): defined.add(m.group(1))
for m in re.finditer(r'\blocal\s+([A-Za-z_]\w*)\s*[,=]', code): defined.add(m.group(1))
for m in re.finditer(r'^\s*local\s+([A-Za-z_]\w*)', code, re.M): defined.add(m.group(1))
known = set('''pcall tostring tonumber type next pairs ipairs select error assert print require
os io math string table FindFirstOf FindAllOf StaticFindObject LoopAsync ExecuteInGameThread
ExecuteWithDelay RegisterKeyBind RegisterHook CreateInvalidObject UEObject StaticFindObject
_g if for while end function local return and or not then else elseif repeat until do in nil true false'''.split())
calls = {}
for i, ln in enumerate(lines):
    for m in re.finditer(r'(?<![\w.:])([A-Za-z_]\w*)\s*\(', ln):
        n = m.group(1)
        if n in ('if','for','while','function','return','and','or','not','elseif','local','until','then','else','end','repeat','do'):
            continue
        calls.setdefault(n, []).append(i+1)
unknown = {k: v for k, v in calls.items() if k not in defined and k not in known}
print("DEFINED:", sorted(defined))
print()
for k in sorted(unknown):
    print("UNDEFINED-CALL:", k, "at lines", unknown[k][:6], "(x%d)" % len(unknown[k]))
