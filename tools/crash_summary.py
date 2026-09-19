import sys, os, re, glob
from collections import Counter

def summarize(folder):
    p = os.path.join(folder, 'CrashContext.runtime-xml')
    print('=' * 90)
    print(os.path.basename(folder))
    if not os.path.exists(p):
        print('  no CrashContext.runtime-xml')
        return
    t = open(p, encoding='utf-8', errors='replace').read()

    def tag(name):
        m = re.search(r'<%s>(.*?)</%s>' % (name, name), t, re.S)
        return m.group(1).strip() if m else ''
    for k in ['ErrorMessage', 'CrashType', 'ProcessId', 'SecondsSinceStart', 'GameName', 'EngineVersion',
              'BuildVersion', 'PlatformFullName', 'GPUBreadcrumbs', 'RHIName', 'MemoryStats.TotalPhysical']:
        v = tag(k)
        if v:
            print('  %-22s %s' % (k + ':', v[:300].replace('\n', ' | ')))
    m = re.search(r'<CallStack>(.*?)</CallStack>', t, re.S)
    if not m:
        print('  no CallStack')
        return
    frames = [x.strip() for x in m.group(1).split('\n') if x.strip()]
    print('  frames: %d' % len(frames))
    c = Counter(frames)
    print('  module histogram: ' + ', '.join('%s:%d' % (k, v) for k, v in c.most_common(10)))
    print('  first 20 frames:')
    for f in frames[:20]:
        print('     ' + f)

for arg in sys.argv[1:]:
    for folder in glob.glob(arg):
        summarize(folder)
