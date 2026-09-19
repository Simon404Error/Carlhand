# 修正：奔跑倍率 >1 就直接开游戏自带快速移动（原先放在"检测到冲刺"分支里，不冲刺就不触发）
import io
p = r'D:\Deepseek-Harness\work\260918_143041\src\cheat_engine.h'
s = io.open(p, encoding='utf-8').read()

old = '''    target=sprintBase*sprintMult tag="奔跑"
    -- 关键：真正有效的是游戏自带的"快速移动"，不是我们写 MaxWalkSpeed（游戏每帧会覆盖回去）
    if fastState~=1 then
      fastState=1
      local how="命令"
      local ok2=pcall(function() return cm:SetFastMovement() end)
      if not ok2 then pcall(function() return cm:SetFastMovement(true) end) end
      if not ok2 then pcall(cmd,"SetFastMovement",pc) how="控制台" end
      local st="?"
      local okq,q=pcall(function() return cm:IsFastMovementActive() end)
      if okq then st=tostring(q) end
      W("  [奔跑] 已开启游戏自带快速移动("..how..")，IsFastMovementActive="..st)
    end
    pcall(sprintProbe,pawn,c)'''
new = '''    target=sprintBase*sprintMult tag="奔跑"
    pcall(sprintProbe,pawn,c)'''
assert old in s
s = s.replace(old, new, 1)

old2 = '''  if sprintMult<=1 and fastState==1 then
    fastState=0
    local ok2=pcall(function() return cm:SetFastMovement() end)
    if not ok2 then pcall(cmd,"SetFastMovement",pc) end
    W("  [奔跑] 已关闭游戏自带快速移动")
  end
  local c=mov(pawn) if not valid(c) then return end'''
new2 = '''  -- 奔跑：只要倍率>1 就开游戏自带的"快速移动"（唯一真生效的办法；写 MaxWalkSpeed 会被游戏每帧覆盖）
  fastSet(sprintMult>1,cm,pc)
  local c=mov(pawn) if not valid(c) then return end'''
assert old2 in s
s = s.replace(old2, new2, 1)

# 把 fastSet 定义插在 applySpeed 之前
anchor = 'local function applySpeed(F,pawn,pc)'
assert anchor in s
helper = '''-- 游戏自带"快速移动"的开关（带状态校验：IsFastMovementActive 能读回真实状态）
local function fastSet(want,cm,pc)
  if not valid(cm) then return end
  local okq,cur=pcall(function() return cm:IsFastMovementActive() end)
  if okq and cur==want then fastState=(want and 1 or 0) return end
  if (want and fastState==1) or ((not want) and fastState==0) then return end
  fastState=want and 1 or 0
  local ok1=pcall(function() return cm:SetFastMovement() end)
  if not ok1 then pcall(function() return cm:SetFastMovement(want) end) end
  if not ok1 then pcall(cmd,"SetFastMovement",pc) end
  local st="?"
  local ok2,q=pcall(function() return cm:IsFastMovementActive() end)
  if ok2 then st=tostring(q) end
  W("  [奔跑] "..(want and "已开启" or "已关闭").."游戏自带快速移动，读回 IsFastMovementActive="..st)
end
'''
s = s.replace(anchor, helper + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('speed patch OK')
