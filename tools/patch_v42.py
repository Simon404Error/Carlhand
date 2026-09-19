# v42：「恢复默认」真的能还原（移速降不回来 / 以前 只要 cur>目标 就被当成"玩家在冲刺"直接 return）
import io
p = r'D:\Deepseek-Harness\work\260918_143041\src\cheat_engine.h'
s = io.open(p, encoding='utf-8').read()

def rep(old, new, n=1):
    global s
    assert old in s, 'anchor: ' + old[:70]
    s = s.replace(old, new, n)

rep('R"FSDLUA(-- Carlhand 功能引擎 v41', 'R"FSDLUA(-- Carlhand 功能引擎 v42')
rep('''if _G.__FSDT_ENGINE_V41 then return end
_G.__FSDT_ENGINE_V41 = true''',
'''-- v42：修「点击恢复默认没效果」——移速以前只要"当前值>目标"就当成玩家在冲刺、直接 return，
--   于是把倍率调回 1 之后速度降不下来。现在记录"我们自己写进去的值"，并识别残留的加速值，强制回落。
if _G.__FSDT_ENGINE_V42 then return end
_G.__FSDT_ENGINE_V42 = true''')
rep('W("\\n>> 功能引擎 v41 已载入 "', 'W("\\n>> 功能引擎 v42 已载入 "')

rep('local fastState, quadState = 0, 0   -- 游戏自带"快速移动"/"四倍镐"的开关状态',
    'local fastState, quadState = 0, 0   -- 游戏自带"快速移动"/"四倍镐"的开关状态\nlocal lastWrote = nil               -- 上一次我们写进 MaxWalkSpeed 的值（用来区分"我们写的"和"游戏自己在冲刺"）')

rep('''  local base=walkBase or 300
  local walkT=base*walkMult
  local target, tag
  if cur and cur>walkT*1.02 then''',
'''  local base=walkBase or 300
  local walkT=base*walkMult
  -- 「恢复默认」必须真的降得回来：
  --   ① 当前速度≈我们上次写进去的值 → 这就是我们写进去的，直接按新目标改写
  --   ② 倍率都回到默认、但速度远大于游戏正常冲刺(约 2×走路) → 是残留加速，强制回落
  --   （游戏自己的冲刺≈600，小于 base*2.6，所以不会误伤正常冲刺）
  local ours = (lastWrote and cur and math.abs(cur-lastWrote)<=math.max(5,lastWrote*0.05))
  local leftover = (walkMult==1 and sprintMult==1 and cur and cur>base*2.6)
  if ours or leftover then
    sp(c,"MaxWalkSpeed",walkT)
    lastWrote=walkT
    if lastLogged~=walkT then
      lastLogged=walkT
      W("  [移速] "..(ours and "按新值改写" or "检测到残留加速，已回落").."="..f2(walkT)
        .."（读回="..tostring(gp(c,"MaxWalkSpeed")).."）")
    end
    return
  end
  local target, tag
  if cur and cur>walkT*1.02 then''')

rep('''  if target<50 then target=50 elseif target>20000 then target=20000 end
  sp(c,"MaxWalkSpeed",target)''',
'''  if target<50 then target=50 elseif target>20000 then target=20000 end
  sp(c,"MaxWalkSpeed",target)
  lastWrote=target''')

# 换图/重生时清掉记忆
rep('      fastState=0 quadState=0                       -- 重生后游戏自带的开关状态未知，重新确认一次',
    '      fastState=0 quadState=0                       -- 重生后游戏自带的开关状态未知，重新确认一次\n      lastWrote=nil walkBase=nil sprintBase=nil basesCaptured=false')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('v42 patched')
