# v47：安全修复（getCM 不再用 FindAllOf / 不做 c[fn] 下标，避免打死 UE4SS 的 Lua 线程）
#      + 奔跑改成"直接写速度属性"（这条路已被验证有效：移速就是靠它）
import io
p = r'D:\Deepseek-Harness\work\260918_143041\src\cheat_engine.h'
s = io.open(p, encoding='utf-8').read()
def rep(old, new, n=1):
    global s
    assert old in s, 'anchor: ' + old[:90]
    s = s.replace(old, new, n)

rep('R"FSDLUA(-- Carlhand 功能引擎 v46', 'R"FSDLUA(-- Carlhand 功能引擎 v47')
rep('''if _G.__FSDT_ENGINE_V46 then return end
_G.__FSDT_ENGINE_V46 = true''',
'''-- v47：① 安全修复——getCM 不再调用 FindAllOf("FSDCheatManager"/"CheatManager")，也不做 c[fn] 下标：
--   实测这两件事会直接把 UE4SS 的 Lua 线程打死（日志到"速度基准"就断、执行器心跳也停），
--   症状就是"所有功能突然全都不生效"。现在只读 pc.CheatManager，取不到就用控制台命令兜底。
--   ② 奔跑不再依赖游戏自带 SetFastMovement（本版本实测无效），改成直接写速度属性（移速已验证有效）：
--   有效倍率 = 移速倍率 × 奔跑倍率，填 >1 就立刻生效。
if _G.__FSDT_ENGINE_V47 then return end
_G.__FSDT_ENGINE_V47 = true''')
rep('W("\\n>> 功能引擎 v46 已载入 "', 'W("\\n>> 功能引擎 v47 已载入 "')

# getCM 简化 + 安全诊断
old = s[s.index('-- 取 CheatManager：优先 pc.CheatManager'):s.index('local function fastSet(want,cm,pc)')]
new = '''-- 取 CheatManager：只读 pc.CheatManager（安全）。注意：绝不要用 FindAllOf 去试探可能存在/不存在的类名，
-- 也不要用 obj["函数名"] 下标——UE4SS 在这两种情况下会抛出 C++ 异常，pcall 接不住，会直接打死 Lua 线程。
local cmFixed, cmDiagDone = nil, false
local function getCM(pc)
  if valid(cmFixed) then return cmFixed end
  local c = pc and gp(pc,"CheatManager") or nil
  if not cmDiagDone then
    cmDiagDone = true
    local info = "pc.CheatManager="..type(c)
    if c ~= nil then
      local okv, v = pcall(function() return c:IsValid() end)
      info = info.." valid="..tostring(okv and v)
      local okt, tp = pcall(function() return c:type() end)
      info = info.." 类型="..(okt and tostring(tp) or "取不到")
      local okq, q = pcall(function() return c:IsFastMovementActive() end)
      info = info.." IsFastMovementActive="..(okq and tostring(q) or "调不了")
    end
    W("  [诊断] "..info)
  end
  if valid(c) then cmFixed = c return c end
  return nil
end

'''
s = s.replace(old, new, 1)

# applySpeed：奔跑直接乘进速度目标
rep('  fastSet(sprintMult>1,cm,pc)', '  fastSet(sprintMult>1,cm,pc)      -- 游戏自带快速移动：留着（有些版本有效）')
rep('''  local base=walkBase or 300
  local walkT=base*walkMult''',
'''  local base=walkBase or 300
  -- 奔跑：本版本 SetFastMovement 无效，所以直接把"奔跑倍率"乘进速度目标里（移速已验证这条路有效）
  local walkT=base*walkMult*sprintMult''')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('v47 patched')
