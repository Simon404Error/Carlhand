#pragma once
// 游戏内 Lua 引擎（UE4SS 热加载）。DLL 启动时写入 <游戏目录>\FSD\Binaries\Win64\FSDTrainer_code.lua，
// 并在 FSDTrainer_cmd.txt 里写一行 lua 指令加载它。每 300ms 读 FSDTrainer_features.txt。
// v24：飞行含 bCheatFlying（可上下飞）、奔跑属性探测、夜视保守值、怪物全灭（无半径）、卸下喷气靴。
// 注意：MSVC 单个字符串字面量有 16KB 上限（C2026），所以引擎脚本按块存放，写入时依次拼接。
static const char* kCheatEngineLuaParts[] = {
R"FSDLUA(-- Carlhand 功能引擎 v48
-- v35（崩溃修复）：起因是 6 次 crash dump 全部命中
--   UObject::GetFullName -> FName::ToString（游戏侧 0x1D888A0）读 0x18 越界崩溃。
-- 结论：绝不能用 GetFullName 当"有效性校验"——换图/进任务时 Pawn 已销毁但属性指针还在，
--   GetFullName 会去读已释放对象的 FName 索引 → 直接崩进程。改为 UE4SS 的 IsValid()。
-- 另修：v34 主循环第 354 行调用了从未定义的 nm()，导致 v34 每 tick 抛异常、修复代码根本没跑；
--   并新增"世代号"，热加载新引擎时停掉旧的 LoopAsync（否则新旧两个循环同跑互相打架）。
-- v36：换图/重生判定改用 GetAddress() 比较（UE4SS 每次读属性都返回新 userdata，直接比较对象永远不等）；
--   并配套 DLL 侧"注入后置空命令文件"，避免下次单独启动游戏时自动加载磁盘上的旧引擎。
-- v37：ESP 落地（Lua 采集屏幕坐标写 FSDTrainer_esp.txt，DLL 侧画框）。
-- v41：① 奔跑改用游戏自带 SetFastMovement（MaxWalkSpeed 会被游戏每帧覆盖，所以以前"没效果"）；
--   ② 镐子攻速改用 SetQuadPickAxe；③ 夜视关掉平方反比衰减（Intensity 才有视觉效果）+ 大半径/宽锥角；
--   ④ ESP 显示怪物中文名与矿名；只显示"未挖掘矿脉"（排除掉在地上的 ResourceChunk 掉落块），
--      并在进任务后做一次类名探测把矿脉真名写进日志；⑤ 不再把 /Game/Critters 里的小动物当敌人。
-- v42：修「点击恢复默认没效果」——移速以前只要"当前值>目标"就当成玩家在冲刺、直接 return，
--   于是把倍率调回 1 之后速度降不下来。现在记录"我们自己写进去的值"，并识别残留的加速值，强制回落。
-- v43：新增 设置血量 / 立刻补给 / 自杀 / 杀某个队友 / 杀全队 / 醉酒度（走 CheatManager 命令）；
--   飞行改用游戏自带 SetFlyMode + IsFlyModeActive 校验；奔跑/镐子补状态日志。
-- v44：① 修 奔跑/镐子/飞行"点了没反应"——根因是 applySpeed/applyAim/applyFly 里引用的 cm 其实是 nil
--   （那三个函数在 chunk 层，看不到主循环里的 local cm），于是 fastSet 直接 return、镐子/飞行只剩控制台兜底。
--   现在统一用 gp(pc,"CheatManager") 现取。② 立刻补给 → 呼叫补给仓：改发 InstantDropResuppyPod（在玩家旁空投补给仓），
--   顺带仍然 C_Resupply 补满弹药。
-- v45：CheatManager 统一用 getCM() 取（先读 pc.CheatManager，再退到 FindAllOf("FSDCheatManager")），
--   并打印一次诊断；补给仓改成"cm 函数 + 控制台"多路尝试。
-- v47：① 安全修复——getCM 不再调用 FindAllOf("FSDCheatManager"/"CheatManager")，也不做 c[fn] 下标：
--   实测这两件事会直接把 UE4SS 的 Lua 线程打死（日志到"速度基准"就断、执行器心跳也停），
--   症状就是"所有功能突然全都不生效"。现在只读 pc.CheatManager，取不到就用控制台命令兜底。
--   ② 奔跑不再依赖游戏自带 SetFastMovement（本版本实测无效），改成直接写速度属性（移速已验证有效）：
--   有效倍率 = 移速倍率 × 奔跑倍率，填 >1 就立刻生效。
-- v48：奔跑倍率只作用于"冲刺"（之前直接乘进走路目标 → 走路也变快，用户要的是冲刺更快、走路不变）。
--   做法：走路目标恢复成 base×移速倍率；另开一条 60ms 小循环，检测到游戏把速度推到冲刺值(≈2×走路)时，
--   把"游戏冲刺值×奔跑倍率"写回去（游戏可能每帧重写，所以用更快的循环去抢）。
if _G.__FSDT_ENGINE_V48 then return end
_G.__FSDT_ENGINE_V48 = true
_G.__FSDT_GEN = (_G.__FSDT_GEN or 0) + 1
local MYGEN = _G.__FSDT_GEN
local G = "E:/SteamLibrary/steamapps/common/Deep Rock Galactic/FSD/Binaries/Win64/"
local function W(s) local ok,f=pcall(io.open,G.."FSDTrainer_dump.txt","a") if ok and f then f:write(tostring(s).."\n") f:flush() f:close() end end
-- 有效性校验：只用 IsValid（仅查对象数组，不构造任何名字）
local isValidApi = nil
local function valid(o)
  if not o then return false end
  if isValidApi == nil then
    local ok = pcall(function() return o:IsValid() end)
    isValidApi = ok and true or false
    W("  [引擎] UE4SS IsValid() 可用="..tostring(isValidApi))
  end
  if isValidApi then
    local ok, v = pcall(function() return o:IsValid() end)
    return ok and v == true
  end
  local ok, v = pcall(function() return o:GetClass() end)   -- 兜底：只取类对象，不做名字格式化
  return ok and v ~= nil
end
-- 名字只用于日志，且必须先确认有效
local function nm(o)
  if not valid(o) then return "<无效对象>" end
  local ok, v = pcall(function() return o:GetFullName() end)
  if ok and type(v) == "string" then return v end
  return "<未知对象>"
end
-- 对象身份：只能用 GetAddress()。UE4SS 每次读属性都返回新的 Lua userdata，
-- 直接比较 `a ~= b` 永远为真（userdata 没有 __eq）→ 会误判成"每 tick 都在换图"。
local function addrOf(o)
  if not o then return nil end
  local ok, v = pcall(function() return o:GetAddress() end)
  if ok and v then return v end
  return nil
end
local function gp(o,n) if not valid(o) then return end local ok,v=pcall(function() return o:GetPropertyValue(n) end) if ok then return v end end
local function sp(o,n,v) if not valid(o) then return false end local ok=pcall(function() o:SetPropertyValue(n,v) end) return ok end
local function num(v) if type(v)=="number" then return v end return nil end
local function f2(v) return string.format("%.2f", v) end

local ACT = {
  unlock_all="C_UnlockAll", unlock_weapons="C_UnlockAll_Weapons", unlock_upgrades="C_UnlockAll_WeaponUpgrades",
  unlock_skins="C_UnlockAll_WeaponSkins", unlock_paintjobs="C_UnlockAll_WeaponSkins_Paintjobs",
  unlock_frameworks="C_UnlockAll_WeaponSkins_Frameworks", unlock_perks="C_UnlockAll_Perks",
  unlock_beers="C_UnlockAll_Beers", unlock_poses="C_UnlockAll_Poses", unlock_pickaxe="C_UnlockAll_PickaxeParts",
  unlock_armor="C_UnlockAll_ArmorMats", unlock_store="C_UnlockAll_StoreVanity", unlock_overclock="C_UnlockOverclocking",
  unlock_schematics="C_Schematics_UnlockAll", unlock_deepdives="C_DeepDives_UnlockAll", unlock_facility="C_Unlock_Facility",
  xp="C_AddXP 10000", uncapped_xp="C_AddUncappedXP 10000", forge_xp="C_AddForgingXP 10000",
  season_xp="C_Seasons_AddXP 10000", vanity_xp="C_VanityMasteryAddXP 10000", level_up="C_LevelUpCharacter",
  credits="Cheat_AddCredits 5000", resources="R_AddResources", crafting="R_AddCraftingResource",
  all_resources="C_AddAllResourcesToInventory", reset_cheat_res="C_ResetCheatedResources",
  revive="C_Revive", killall_once="C_KillAll", break_armor="C_BreakAllEnemyArmor",
  teleport_to_me="C_TeleportAllPlayersToMe", teleport_near="C_TeleportPlayerToMe",
  spawn_wave="C_SpawnNormalWave", spawn_scripted="C_SpawnScriptedWave", spawn_rewarder="C_SpawnEvenRewarder",
  spawn_bosco="SpawnBosco", spawn_critters="SpawnCritters", spawn_particles="C_SpawnAllParticles",
  spawn_droppod="C_SpawnDropPodOnSelf", spawn_barrel="C_SpawnBarrelOnPlayer", spawn_dancing="C_SpawnDancingCharacterOnSelf",
  datacore="C_Facility_SpawnDataCore", disable_shields="C_Facility_DisableShields",
  mule_speed="C_Escort_SetMuleSpeed 1000",
  suicide="C_KillPlayer", kill_team="C_KillTeam",
  resupply={ onFunc=function(pawn,pc)      -- 呼叫补给仓：先试 cm 上的 UFUNCTION，再试控制台
      local tries={}
      local cm=getCM(pc)
      if valid(cm) then
        local a=pcall(function() return cm:InstantDropResuppyPod() end) tries[#tries+1]="cm:InstantDropResuppyPod="..tostring(a)
      end
      local b=pcall(cmd,"InstantDropResuppyPod",pc) tries[#tries+1]="cmd:InstantDropResuppyPod="..tostring(b)
      local c2=pcall(cmd,"C_SpawnDropPodOnSelf",pc) tries[#tries+1]="cmd:C_SpawnDropPodOnSelf="..tostring(c2)
      pcall(cmd,"C_Resupply",pc)
      W("  [补给仓] "..table.concat(tries,"  "))
    end },
  inf_flare={ on="C_FlareInfiniteDuration" }, fast_flares={ on="C_SetFastFlares" },
  inf_ammo={ on="C_SetAmmoCostEnabled" },
  fullbright={ on="C_ToggleFadingEnabled", off="C_ToggleFadingEnabled" },
  jetboots={ onFunc=function(pawn,pc) pcall(cmd,"C_JetBoots_Enable_MK2",pc) W("  [喷气靴] 已启用（本局有效）") end },
  jetboots_off={ onFunc=function(pawn,pc) pcall(jetbootsOff,pawn,pc) end },
}
local last={}
local function readF()
  local t={} local ok,f=pcall(io.open,G.."FSDTrainer_features.txt","r") if not ok or not f then return t end
  for line in f:lines() do local k,v=string.match(line,"^%s*([%w_]+)%s*=%s*([%w%._%-]*)") if k and v~="" then t[k]=v end end
  f:close() return t
end
local kismet=StaticFindObject("/Script/Engine.Default__KismetSystemLibrary")
local function cmd(c,pc) if not kismet or not valid(pc) or type(c)~="string" then return end
  local ok=pcall(function() return kismet:ExecuteConsoleCommand(pc,c,nil) end) W("  命令["..c.."] ok="..tostring(ok)) end
local function ctx() local pc=FindFirstOf("PlayerController") if not pc then return end return pc, gp(pc,"CheatManager"), gp(pc,"Pawn") end
local function mov(pawn) return gp(pawn,"CharacterMovement") end

-- ================= 喷气靴 =================
function jetbootsOff(pawn,pc)
  pcall(cmd,"C_JetBoots_Enable 0",pc)
  pcall(cmd,"C_JetBoots_Disable",pc)
  pcall(cmd,"C_RemoveJetBoots",pc)
  pcall(cmd,"C_JetBoots_Enable_MK2 0",pc)
  local hit={}
  local cm=pc and gp(pc,"CheatManager")            -- 1) 作弊管理器上的开关（mod 方式可能就是这个标记）
  if valid(cm) then
    for _,p in ipairs({"JetBoots","bJetBoots","JetBootsEnabled","bJetBootsEnabled","ForceJetBoots",
                       "GiveJetBoots","JetBootsMK2","bJetBootsMK2","EnableJetBoots","bEnableJetBoots"}) do
      local cur0=gp(cm,p)
      if cur0~=nil then
        if sp(cm,p,false) then local rb=gp(cm,p) hit[#hit+1]="cm."..p.."="..tostring(rb) end
      end
    end
  end
  for _,cls in ipairs({"BP_JetBootsBox_C","JetBootsBox","FSDJetBootsBox","BP_JetBootsBox","JetBootsItem",
                       "BP_JetBoots_C","FSDJetBootsItem","FSDJetBoots","JetBoots","BP_JetBootsUpgrade_C",
                       "JetBootsPickup","BootsBox","JetBootsComponent","FSDJetBootsComponent"}) do
    local ok,l=pcall(FindAllOf,cls)
    if ok and type(l)=="table" and #l>0 then
      W("  [喷气靴] 找到对象: "..cls.." 数量="..#l.." 例="..nm(l[1]))
      for _,o in ipairs(l) do
        for _,p in ipairs({"bEquipped","Equipped","bIsEquipped","bEnabled","Enabled","bActive","Active",
                           "bBusy","bHasBoots","bJetBoots","Fuel","CurrentFuel"}) do
          local cur0=gp(o,p)
          if cur0~=nil then
            local nv=(type(cur0)=="boolean") and false or ((type(cur0)=="number") and 0 or nil)
            if nv~=nil and sp(o,p,nv) then hit[#hit+1]=cls.."."..p.."="..tostring(gp(o,p)) end
          end
        end
      end
    end
  end
  for _,p in ipairs({"bJetBootsEnabled","bHasJetBoots","bJetBootsEquipped","JetBootsActive","bJetBootsOn",
                     "bEnableJetBoots","bEquippedBoots","bHasFuel","bCanUseJetBoots"}) do
    if type(gp(pawn,p))=="boolean" then
      if sp(pawn,p,false) then hit[#hit+1]="pawn."..p.."="..tostring(gp(pawn,p)) end
    end
  end
  local c=mov(pawn)
  if valid(c) then
    local m=num(gp(c,"MovementMode"))
    if m==5 then                                   -- 5=MOVE_Flying：喷气靴把角色置于飞行模式
      sp(c,"MovementMode",1)                       -- 强制回走路
      sp(c,"bCheatFlying",false)
      local g0=num(gp(c,"GravityScale")) if g0==0 then sp(c,"GravityScale",1.0) end
      hit[#hit+1]="强制回走路模式（原移动模式=5）"
    end
  end
  W("  [喷气靴] 卸下尝试: "..(#hit>0 and table.concat(hit,", ") or "没找到可写的开关（下一轮从盒子对象/升级组件入手）"))
end
)FSDLUA",
R"FSDLUA(-- ================= 移速 / 奔跑 =================
local SPRINT_CANDS={"DebugFastMove","bIsSprinting","SprintSpeed","SprintSpeedMultiplier","SprintMultiplier",
                    "MaxSprintSpeed","MovementSpeedMultiplier","MaxCustomMovementSpeed"}
local sprintHits, sprintLogged = {}, false
function sprintProbe(pawn,c)
  for _,p in ipairs(SPRINT_CANDS) do
    local o, cur0 = nil, nil
    if gp(c,p)~=nil then o=c cur0=gp(c,p) elseif gp(pawn,p)~=nil then o=pawn cur0=gp(pawn,p) end
    if o then
      if type(cur0)=="boolean" then
        sp(o,p,true) if gp(o,p)==true then sprintHits[p]="true" end
      elseif type(cur0)=="number" then
        local v=cur0*1.5
        if sp(o,p,v) then local rb=num(gp(o,p)) if rb and math.abs(rb-v)<0.01 then sprintHits[p]=f2(rb) end end
      end
    end
  end
  if not sprintLogged then
    sprintLogged=true
    local list={} for k,v in pairs(sprintHits) do list[#list+1]=k.."="..tostring(v) end
    W("  [奔跑] 可用属性: "..(#list>0 and table.concat(list,", ") or "8 个候选都没命中，需要换思路"))
  end
end
local walkBase, sprintBase, lastLogged, basesCaptured, sprintDbg = nil, nil, nil, false, 0
local fastState, quadState = 0, 0   -- 游戏自带"快速移动"/"四倍镐"的开关状态
local lastWrote = nil               -- 上一次我们写进 MaxWalkSpeed 的值（用来区分"我们写的"和"游戏自己在冲刺"）
-- 取 CheatManager：只读 pc.CheatManager（安全）。注意：绝不要用 FindAllOf 去试探可能存在/不存在的类名，
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

local function fastSet(want,cm,pc)
  local okc=valid(cm)
  if okc then
    local okq,cur=pcall(function() return cm:IsFastMovementActive() end)
    if okq and cur==want then fastState=(want and 1 or 0) return end
  end
  if (want and fastState==1) or ((not want) and fastState==0) then return end
  fastState=want and 1 or 0
  local how="cm:SetFastMovement"
  local ok1=false
  if okc then
    ok1=pcall(function() return cm:SetFastMovement(want) end)      -- 需要 bool 参数的版本
    if not ok1 then ok1=pcall(function() return cm:SetFastMovement() end) end
  end
  if not ok1 then pcall(cmd,"SetFastMovement",pc) how="控制台" end
  local st="?"
  if okc then local ok2,q=pcall(function() return cm:IsFastMovementActive() end) if ok2 then st=tostring(q) end end
  W("  [奔跑] "..(want and "已开启" or "已关闭").."游戏自带快速移动（"..how.."），读回 IsFastMovementActive="..st)
end
local function applySpeed(F,pawn,pc)
  local cm=getCM(pc)                      -- 本函数在 chunk 层，看不到主循环的 local cm，必须现取
  local walkMult=tonumber(F.walk_speed) or 1
  local sprintMult=tonumber(F.sprint_speed) or 1
  if walkMult<=0 then walkMult=1 end
  if sprintMult<=0 then sprintMult=1 end
  if walkMult>20 then walkMult=20 elseif walkMult<0.1 then walkMult=0.1 end
  if sprintMult>20 then sprintMult=20 elseif sprintMult<0.1 then sprintMult=0.1 end
  -- 奔跑：只要倍率>1 就开游戏自带的"快速移动"（唯一真生效的办法；写 MaxWalkSpeed 会被游戏每帧覆盖）
  fastSet(sprintMult>1,cm,pc)      -- 游戏自带快速移动：留着（有些版本有效）
  local c=mov(pawn) if not valid(c) then return end)FSDLUA",
R"FSDLUA(  local cur=num(gp(c,"MaxWalkSpeed"))
  if cur and (cur<50 or cur>20000) then cur=nil walkBase=nil sprintBase=nil W("  [移速] 读到异常速度值，已重置基准") end
  local base=walkBase or 300
  -- 走路目标只跟"移速倍率"有关；奔跑倍率交给下面那条 60ms 冲刺循环（只影响冲刺）
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
  if cur and cur>walkT*1.02 then
    if sprintMult==1 then
      if sprintBase and cur>sprintBase+1 then sp(c,"MaxWalkSpeed",sprintBase) end
      return
    end
    if not sprintBase then sprintBase=(walkBase or 300)*2.0 end   -- 只取一次；游戏冲刺约=走路×2
    if sprintDbg~=1 then
      sprintDbg=1
      local ok=sp(c,"DebugFastMove",true)
      W("  [奔跑] 额外打开 DebugFastMove ok="..tostring(ok).." 读回="..tostring(gp(c,"DebugFastMove")))
    end
    target=sprintBase*sprintMult tag="奔跑"
    pcall(sprintProbe,pawn,c)
  else
    if cur and cur>=120 and cur<=420 then walkBase=cur end
    target=(walkBase or 300)*walkMult tag="移速"
  end
  if target<50 then target=50 elseif target>20000 then target=20000 end
  sp(c,"MaxWalkSpeed",target)
  lastWrote=target
  if lastLogged~=target then
    lastLogged=target
    local rb=num(gp(c,"MaxWalkSpeed"))
    W("  ["..tag.."] 基准="..tostring(walkBase or 300).." 写="..f2(target).." 读回="..tostring(rb)..(rb and math.abs(rb-target)<1.5 and " ✓生效" or " ✗未生效"))
  end
end

-- ================= 飞行 =================
local flyState = 0
-- 飞行：改用游戏自带的 C_SetFlyMode（一次性命令，不再每 300ms 反复写 MovementMode/GravityScale/Velocity）
-- 之前那套属性写入会和游戏自己的移动逻辑抢写 → 走路一顿一顿，严重时闪退
local function flySet(want,cm,pc)
  if (want and flyState==1) or ((not want) and flyState==-1) then return end
  local okq,cur=pcall(function() return cm:IsFlyModeActive() end)
  if okq and cur==want then flyState=want and 1 or -1 return end      -- 状态已经是目标值，不动
  flyState = want and 1 or -1
  local how="cm:SetFlyMode()"
  local ok1=pcall(function() return cm:SetFlyMode() end)
  if not ok1 then pcall(function() return cm:SetFlyMode(want) end) end
  if not ok1 then pcall(cmd,"C_SetFlyMode",pc) how="控制台 C_SetFlyMode" end
  local st="?"
  local ok2,q=pcall(function() return cm:IsFlyModeActive() end)
  if ok2 then st=tostring(q) end
  W("  [飞行] "..(want and "已开启" or "已关闭").."（"..how.."）IsFlyModeActive="..st)
end
local function applyFly(F,pawn,pc)
  if flyState==0 and F.fly~="1" then return end      -- 从没开过且当前关闭：什么都不做
  flySet(F.fly=="1",getCM(pc),pc)
end
-- ================= 全图高亮 =================
local LIGHT_CANDS={"Flashlight","HeadLight","FlashlightComponent","Light","SpotLight","Lantern","TorchLight","PlayerLight"}
local lightObj, lightOrig, lightLogged = nil, {}, false
local function applyLight(F,pawn)
  local want=(F.bright_light=="1")
  if lightObj and not valid(lightObj) then lightObj=nil lightLogged=false end   -- 换图后旧对象已失效
  if not want and not lightLogged then return end          -- 关闭且没有需要还原的：完全不碰任何对象
  if not lightObj then
    for _,n in ipairs(LIGHT_CANDS) do
      local o=gp(pawn,n)
      if valid(o) then lightObj=o W("  [夜视] 使用灯光组件: "..n.." ("..nm(o)..")") break end
    end
  end
  if not lightObj then
    if want and not lightNoLog then lightNoLog=true W("  [夜视] 没找到灯光组件，退回 C_ToggleFadingEnabled") pcall(cmd,"C_ToggleFadingEnabled",pawn) end)FSDLUA",
R"FSDLUA(    return
  end
  local o=lightObj
  if want then
    if not lightLogged then
      for _,p in ipairs({"Intensity","AttenuationRadius","OuterConeAngle","InnerConeAngle","CastShadows",
                         "bCastShadows","bUseInverseSquaredFalloff","IntensityUnits","LightColor","Visible","bVisible"}) do
        if lightOrig[p]==nil then lightOrig[p]=gp(o,p) end
      end
      sp(o,"CastShadows",false)                    -- 关阴影（大范围光照+阴影容易崩）
      sp(o,"bCastShadows",false)
      -- 关键：UE4 灯光默认"平方反比衰减"，Intensity 的单位是坎德拉，衰减极快 → 改大也看不太出来。
      -- 关掉它 + 单位改成 Unitless，Intensity 才会线性生效。
      sp(o,"bUseInverseSquaredFalloff",false)
      sp(o,"IntensityUnits",0)
      sp(o,"Intensity", 100000)
      sp(o,"AttenuationRadius", 40000)
      sp(o,"OuterConeAngle", 88)
      sp(o,"InnerConeAngle", 45)
      sp(o,"Visible", true)
      sp(o,"bVisible", true)
      lightLogged=true
      W("  [夜视] 已应用 Intensity="..tostring(gp(o,"Intensity")).." 半径="..tostring(gp(o,"AttenuationRadius"))
        .." 平方衰减="..tostring(gp(o,"bUseInverseSquaredFalloff")).." 单位="..tostring(gp(o,"IntensityUnits"))
        .." 可见="..tostring(gp(o,"Visible")).." 组件="..nm(o))
    end
  else
    local iv=num(gp(o,"Intensity"))
    if lightLogged or (iv and iv>2000) then
      lightLogged=false
      for p,val in pairs(lightOrig) do if val~=nil then sp(o,p,val) end end
      sp(o,"Intensity", 50)
      sp(o,"AttenuationRadius", 150)
      sp(o,"CastShadows", true)
      W("  [夜视] 已还原 Intensity="..tostring(gp(o,"Intensity")).." 半径="..tostring(gp(o,"AttenuationRadius")))
    end
  end
end
-- ================= 坐标 / 杀戮光环 =================
local locApi=nil
local function locOf(a)
  if not valid(a) or locApi==false then return end
  if locApi==nil then
    local ok,v=pcall(function() return a:K2_GetActorLocation() end)
    if ok and type(v)=="table" then locApi=true W("  [位置] K2_GetActorLocation 可用")
    else locApi=false W("  [位置] 取不到坐标") return end
  end
  local ok,v=pcall(function() return a:K2_GetActorLocation() end)
  if ok and type(v)=="table" then local x,y,z=v.X or v.x, v.Y or v.y, v.Z or v.z if x then return x,y,z end end
  return
end
local seen={}
local function targets(pawn)
  local ok,l=pcall(FindAllOf,"Pawn")
  if not ok or type(l)~="table" then return {} end
  local out={}
  for _,p in ipairs(l) do
    if p~=pawn and valid(p) then
      local c="" local okc,v=pcall(function() return p:GetClass():GetFullName() end)
      if okc and type(v)=="string" then c=v end
      if not (string.find(c,"PlayerCharacter",1,true) or string.find(c,"NavigatorCharacter",1,true)
              or string.find(c,"BP_Player",1,true) or string.find(c,"Bosco",1,true)) then
        out[#out+1]=p
        if not seen[c] then seen[c]=true W("  [杀戮光环] 目标类名="..c) end
      end
    end
  end
  return out
end
local lastAuraLog, lastKill = 0, 0
local function monsterWipe(F,pc,pawn,now)
  local list=targets(pawn)
  if now-(lastAuraLog or 0)>=5 then lastAuraLog=now W("  [怪物全灭] 场上目标数="..#list) end
  if now-(lastKill or 0)>=1 then lastKill=now W("  [怪物全灭] 执行清场") pcall(cmd,"C_KillAll",pc) end
end
-- ================= 研发中 / ESP =================
-- ================= 研发中：瞄准范围 / 辅助瞄准 / 镐子攻速 =================
local AIM_CANDS={ "AimRange","AimAssistRange","AimAssistStrength","AimAssistMultiplier","AimAssistFactor",
                  "AutoAimRange","AutoAimStrength","bAimAssistEnabled","AimAssistEnabled" }
local PICK_CANDS={ "MiningSpeedMultiplier","PickaxeDamageMultiplier","PickaxeSpeedMultiplier","MiningSpeed",
                   "DigSpeed","PickaxeSwingSpeed","MiningRateMultiplier","PickaxeDamage" }
local probeLog={}
local function probeGroup(pawn,c,key,cands,scaleWanted)
  if probeLog[key] then return end
  probeLog[key]=true
  local got={}
  for _,p in ipairs(cands) do
    local o,cur0=nil,nil
    if gp(c,p)~=nil then o=c cur0=gp(c,p) elseif gp(pawn,p)~=nil then o=pawn cur0=gp(pawn,p) end
    if o then
      if type(cur0)=="boolean" then got[#got+1]=p.."(bool=true)"
      elseif type(cur0)=="number" then got[#got+1]=p.."("..f2(cur0)..")" end
    end
  end
  W("  [探测"..key.."] 命中: "..(#got>0 and table.concat(got,", ") or "未找到（需要换更准的名字）"))
end
local function applyAim(F,pawn,pc)
  local cm=getCM(pc)
  -- 镐子攻速：游戏自带 SetQuadPickAxe（属性探测从来没命中过）
  local wantQ=(tonumber(F.pickaxe_speed) or 0)>1
  if wantQ and quadState~=1 then
    quadState=1
    local ok1=pcall(function() return cm:SetQuadPickAxe(true) end)
    if not ok1 then pcall(function() return cm:SetQuadPickAxe() end) end
    if not ok1 then pcall(cmd,"SetQuadPickAxe",pc) end
    local pr={}
    for _,pn in ipairs({"bQuadPickAxe","QuadPickAxe","bQuadPickAxeActive","bCheatQuadPickAxe","PickAxeMultiplier"}) do
      local v=gp(cm,pn) if v~=nil then pr[#pr+1]=pn.."="..tostring(v) end
    end
    W("  [镐子] 已开启 SetQuadPickAxe（四倍镐）"..(#pr>0 and ("；状态: "..table.concat(pr,", ")) or "；没找到状态字段"))
  elseif (not wantQ) and quadState==1 then
    quadState=0
    local ok1=pcall(function() return cm:SetQuadPickAxe(false) end)
    if not ok1 then pcall(function() return cm:SetQuadPickAxe() end) end
    W("  [镐子] 已关闭 SetQuadPickAxe")
  end
  local c=mov(pawn)
  if not valid(c) then return end
  probeGroup(pawn,c,"瞄准",AIM_CANDS) probeGroup(pawn,c,"镐子",PICK_CANDS)
  local function applyGroup(cands,val,tag)
    local v=tonumber(val) or 0
    if v<=0 then return end
    for _,p in ipairs(cands) do
      local o=nil
      if gp(c,p)~=nil then o=c elseif gp(pawn,p)~=nil then o=pawn end
      if o then
        local cur0=gp(o,p)
        if type(cur0)=="number" and cur0~=0 then
          if sp(o,p,cur0*v) then W("  ["..tag.."] "..p.." 写 "..f2(cur0*v).." 读回="..tostring(gp(o,p))) end
        elseif type(cur0)=="boolean" and v>0 then
          if sp(o,p,true) then W("  ["..tag.."] "..p.." 已置 true") end
        end
      end
    end
  end
  applyGroup(AIM_CANDS,F.aim_range,"瞄准范围")
  applyGroup(AIM_CANDS,F.aim_assist,"辅助瞄准")
  applyGroup(PICK_CANDS,F.pickaxe_speed,"镐子攻速")
end
-- ================= ESP：采集目标屏幕坐标 → 写文件，DLL 侧画框 =================
-- 安全约定（血泪教训）：只在世界稳定 15 秒后开始；每秒最多一次；每轮最多 60 个目标；
-- 每个对象先用 IsValid 校验；所有游戏调用包 pcall；目标距离上限由菜单里的"米"决定。
-- 敌人的类名规律是实测得到的（2026-09-18 进任务那一局）：
--   /Game/Enemies/Spider/...、/Game/Enemies/FlyingBug/...、/Game/Critters/LootBug/...
-- 排除 /Game/GameElements/（BP_Donkey、BP_MiniMule_Salvage 这类不是敌人）。
local ESP_MINE_C = { "ResourceChunk","BP_ResourceChunk_C","BP_Collectable_C","Collectable","BP_Gem_C","BP_Crystal_C" }
local espLast, espHitLogged, espDbg, espRound = 0, false, false, 0
local function wf(path,s)
  local ok,h=pcall(io.open,path,"w")
  if ok and h then pcall(function() h:write(s) h:flush() h:close() end) end
end
local projMode=nil    -- nil=未定；"api"=用游戏自带 API；"calc"=自己算
local projTried=false
local projInfo=false
local vpW,vpH,vpRead = 1920, 1200, 0
-- 通用取字段：对象可能是 table，也可能是 UE4SS 的结构体 userdata；大小写都试
local function fld(o,...)
  if o==nil then return nil end
  for i=1,select("#",...) do
    local k=select(i,...)
    local ok,v=pcall(function() return o[k] end)
    if ok and v~=nil then return v end
  end
  return nil
end
local function readVP(now)
  if now-(vpRead or 0)<5 then return end
  vpRead=now
  local ok,f=pcall(io.open,G.."FSDTrainer_vp.txt","r")
  if ok and f then
    local s=f:read("*a") f:close()
    local a,b=string.match(s or "","(%d+)%s+(%d+)")
    if a and b and tonumber(a)>0 and tonumber(b)>0 then vpW=tonumber(a) vpH=tonumber(b) end
  end
end
-- 游戏自带 API：UE4SS 对"结构体出参"的处理有几种可能，逐个试；第一次把每种形式的返回都写进日志
local function tryApi(pc,x,y,z)
  local forms={
    function() return pc:ProjectWorldLocationToScreen({X=x,Y=y,Z=z},0,false) end,
    function() return pc:ProjectWorldLocationToScreen(x,y,z,0,false) end,
    function() return pc:ProjectWorldLocationToScreen({X=x,Y=y,Z=z}) end,
    function() return pc:ProjectWorldLocationToScreen(x,y,z) end,
    function()
      local gs=StaticFindObject("/Script/Engine.Default__GameplayStatics")
      if not gs then return nil end
      return gs:ProjectWorldToScreen(pc,{X=x,Y=y,Z=z},0,false)
    end,
  }
  local log={}
  for i,fn in ipairs(forms) do
    local ok,a,b=pcall(fn)
    if ok then
      local ret=nil
      if a~=nil and a~=false and a~=true then ret=a end
      if ret==nil and b~=nil and b~=false and b~=true then ret=b end
      local sx,sy=nil,nil
      if ret~=nil then sx=fld(ret,"X","x") sy=fld(ret,"Y","y") end
      if projTried==false then log[#log+1]="["..i.."]ok=true "..type(ret) end
      if type(sx)=="number" and type(sy)=="number" then
        if projTried==false then W("  [ESP调试] 第 "..i.." 种形式可用") end
        return sx,sy,i
      end
    else
      if projTried==false then log[#log+1]="["..i.."]ok=false err="..tostring(a) end
    end
  end
  if not projTried then W("  [ESP调试] API 五种形式结果: "..table.concat(log," | ")) end
  return nil
end
local function rotVecs(pitch,yaw)
  local p,y=pitch*math.pi/180.0,yaw*math.pi/180.0
  local cp,sp,cy,sy=math.cos(p),math.sin(p),math.cos(y),math.sin(y)
  return {cp*cy,cp*sy,sp},{-sy,cy,0},{-sp*cy,-sp*sy,cp}
end
-- 备选：用相机位置/朝向 + FOV + 视口尺寸自己算屏幕坐标（字段全部容错读取）
-- 经验：UFunction 返回的结构体是 table（K2_GetActorLocation 已证实），属性读出来的
--       结构体是 userdata，字段名不一定叫 Pitch/Yaw/x/y；所以两条路都试。
local function vecOf(o)
  local x=fld(o,"X","x"); local y=fld(o,"Y","y"); local z=fld(o,"Z","z")
  if type(x)=="number" and type(y)=="number" and type(z)=="number" then return x,y,z end
  return nil
end
-- 视角朝向：GetControlRotation() 是 UFunction，返回 table，最可靠
local function viewRot(pc)
  local okr,rr=pcall(function() return pc:GetControlRotation() end)
  if okr and type(rr)=="table" then
    local p,y=rr.Pitch or rr.pitch, rr.Yaw or rr.yaw
    if type(p)=="number" and type(y)=="number" then return p,y,"GetControlRotation" end
  end
  local r2=gp(pc,"ControlRotation")
  local p,y=fld(r2,"Pitch","pitch"),fld(r2,"Yaw","yaw")
  if type(p)=="number" and type(y)=="number" then return p,y,"属性ControlRotation" end
  return nil
end
-- 每轮只取一次视角数据（朝向 / 相机位置 / FOV），避免"每个目标都重取一遍"（UFunction 调用要省着用）
local function prepareView(pc)
  readVP(os.time())
  local pitch,yaw,srcRot=viewRot(pc)
  if type(pitch)~="number" or type(yaw)~="number" then return nil end
  local clx,cly,clz,srcCam
  local cam=gp(pc,"PlayerCameraManager")
  if valid(cam) then
    local okc,loc=pcall(function() return cam:GetCameraLocation() end)
    if okc and type(loc)=="table" then clx,cly,clz=loc.X or loc.x,loc.Y or loc.y,loc.Z or loc.z end
    if type(clx)~="number" then
      local cc=gp(cam,"CameraCache")
      local vx,vy,vz=vecOf(fld(fld(cc,"POV","pov"),"Location","location"))
      if vx then clx,cly,clz=vx,vy,vz end
    end
    if type(clx)=="number" then srcCam="PlayerCameraManager" end
  end
  local fov=90
  if valid(cam) then
    local okf,f1=pcall(function() return cam:GetFOVAngle() end)
    if okf and type(f1)=="number" and f1>20 and f1<170 then fov=f1
    else local f0=gp(cam,"DefaultFOV") if type(f0)=="number" and f0>20 and f0<170 then fov=f0 end end
  end
  local v={pitch=pitch,yaw=yaw,fov=fov,clx=clx,cly=cly,clz=clz,srcRot=srcRot,srcCam=srcCam or "玩家+眼睛"}
  v.F,v.R,v.U=rotVecs(pitch,yaw)
  v.tanX=math.tan(fov*math.pi/360.0)
  v.tanY=v.tanX/(vpW/vpH)
  return v
end
local function projWith(v,pawn,x,y,z)
  if v==nil then return nil end
  local clx,cly,clz=v.clx,v.cly,v.clz
  if type(clx)~="number" then
    local ox,oy,oz=locOf(pawn)
    if not ox then return nil end
    clx,cly,clz=ox,oy,oz+60
  end
  local dx,dy,dz=x-clx,y-cly,z-clz
  local yc=dx*v.F[1]+dy*v.F[2]+dz*v.F[3]
  if yc<=1 then return nil end
  local xc=dx*v.R[1]+dy*v.R[2]+dz*v.R[3]
  local zc=dx*v.U[1]+dy*v.U[2]+dz*v.U[3]
  local sx=(xc/(yc*v.tanX))*vpW/2+vpW/2
  local sy=vpH/2-(zc/(yc*v.tanY))*vpH/2
  if not projInfo then
    projInfo=true
    W("  [ESP调试] 自己算：朝向="..v.srcRot.."(pitch="..f2(v.pitch).." yaw="..f2(v.yaw)..")"
      .." 相机="..v.srcCam.." FOV="..f2(v.fov).." 视口="..vpW.."x"..vpH)
  end
  return sx,sy
end
local function projOf(pc,pawn,view,x,y,z)
  if not projTried then
    projTried=true
    local sx,sy,form=tryApi(pc,x,y,z)
    projMode=sx and "api" or "calc"
    W("  [ESP] 投影方式="..tostring(projMode)..(sx and ("（第 "..tostring(form).." 种 API 形式）") or "（自己算）"))
  end
  if projMode=="api" then
    local sx,sy=tryApi(pc,x,y,z)
    if sx then return sx,sy end
  end
  return projWith(view or prepareView(pc),pawn,x,y,z)
end
-- ================= 冲刺加速（60ms）=================
-- 游戏冲刺时会把 MaxWalkSpeed 写成自己的冲刺值（≈走路×2，可能每帧重写），
-- 所以这里用 60ms 的小循环：发现"速度被推到冲刺档"就把 冲刺值×奔跑倍率 写回去。
-- 走路时（速度≈走路基准）完全不碰，保证"改奔跑不影响走路"。
local sprintRef = nil
LoopAsync(60, function()
  if _G.__FSDT_GEN~=MYGEN then return true end
  local ok,e=pcall(function()
    local F=readF()
    local sm=tonumber(F.sprint_speed) or 1
    if sm<=1 then sprintRef=nil return end
    if sm>20 then sm=20 end
    local pc,cm,pawn=ctx()
    if not (valid(pc) and valid(pawn)) then return end
    local c=mov(pawn) if not valid(c) then return end
    local cur=num(gp(c,"MaxWalkSpeed"))
    if not cur then return end
    local wb=walkBase or 300
    if cur > wb*1.25 then                       -- 看起来游戏正在用冲刺速度
      if sprintRef==nil or cur < sprintRef then sprintRef=cur end
      local want=sprintRef*sm
      if want>20000 then want=20000 end
      if math.abs(cur-want)>1 then
        sp(c,"MaxWalkSpeed",want)
        if lastWrote then lastWrote=want end
      end
    else
      sprintRef=nil
    end
  end)
  if not ok then W("  !!! 冲刺循环异常(已继续): "..tostring(e)) end
  return false
end)

)FSDLUA",
R"FSDLUA(-- ================= ESP：独立快循环（150ms ≈ 6.7Hz）+ 每 3 秒重扫目标 =================
-- v40 修"框不跟怪走"：原来 ESP 挂在 300ms 主循环里，而且 os.time() 只有秒级精度，
-- 实际每秒才更新 1 次 → 怪一动框就掉队（1 秒能走好几米）。现在单独一条 150ms 循环，
-- 并把"枚举目标（要 GetClass():GetFullName()，贵）"和"取坐标+投影（纯数学）"拆开：
-- 目标列表每 3 秒刷一次，坐标每轮都取，画框用"头顶/脚底"两个投影点定高度。
local ESP_FILE = "FSDTrainer_esp.txt"
local espTargets, espEnumAt = {}, 0
local espWorldAt, espSelfRound = nil, 0
local oreList, oreProbeDone = {}, false     -- 未挖掘矿脉（进任务后探测一次收集）
-- ---- 类名 → 中文名（ESP 上要显示"是什么怪/什么矿"）----
local ENEMY_NAME = {
  {"Grunt_Attacker","战士虫·攻击者"},{"Grunt_Guard","战士虫·护卫"},{"Grunt","战士虫"},
  {"Swarm","蜂群虫"},{"Swarmer","蜂群虫"},{"Tank","禁卫虫"},{"Praetorian","禁卫虫"},
  {"Explod","自爆虫"},{"Bomber","轰炸虫"},{"Spitball","吐球虫"},{"Leech","洞穴水蛭"},
  {"Mactera_TripleShooter","三连喷吐虫"},{"Mactera_Shooter","喷吐虫"},{"Mactera_Spawn","孕母虫"},
  {"Mactera_Grabber","抓取虫"},{"Mactera_Brundle","飞虫"},{"Mactera","飞行虫"},
  {"Naedocyte","水母"},{"Breeder","巢母虫"},{"Hooli","呼利虫"},{"Qronar","幼兽"},
  {"Menace","威胁"},{"Barrage","弹幕虫"},{"Dreadnought","恐惧战舰"},{"Dreadnaught","恐惧战舰"},
  {"Bulk","暴君虫"},{"Fester","腐败虫"},{"Flea","跳蚤"},{"LootBug","战利虫"},
  {"Hexawing","六翼虫"},{"Stingtail","刺尾虫"},{"Trawler","拖网虫"},{"Nexus","巢穴虫"},
  {"Korlok","科洛克"},{"Sentinel","哨兵"},{"Rockpox","岩痘"},{"Glyphid","异虫"},
  {"Cave","洞穴虫"},{"Spider","异虫"},
}
local ORE_NAME = {
  {"Morkite","墨岩"},{"Nitra","硝石"},{"Bismor","铋矿"},{"Croppa","克洛帕"},{"Jadiz","玉石"},
  {"Magnite","磁铁矿"},{"Umanite","铀铁"},{"Enor","珍珠"},{"Gold","黄金"},{"Dystrum","碱液"},
  {"Bittergem","苦胆石"},{"Aquarq","水银晶"},{"Hollomite","霍洛石"},{"Plagueheart","疫心"},
  {"Crystal","水晶"},{"Gem","宝石"},{"Ore","矿石"},{"Mineral","矿物"},{"Resource","资源"},{"Vein","矿脉"},
}
local function mapName(tbl,s)
  for _,kv in ipairs(tbl) do
    if string.find(s,kv[1],1,true) then return kv[2] end
  end
  return nil
end
local function enemyName(c)
  local n=mapName(ENEMY_NAME,c)
  if n then
    if string.find(c,"Radioactive",1,true) then return n.."·辐射" end
    if string.find(c,"Kursite",1,true) or string.find(c,"Rockpox",1,true) then return n.."·岩痘" end
    return n
  end
  return (string.match(c,"([%w_]+)_C$") or "敌人")
end
local function oreName(c)
  local n=mapName(ORE_NAME,c)
  return n or (string.match(c,"([%w_]+)_C$") or "矿石")
end
-- 只有"长在墙上的矿脉"算矿石；掉在地上可捡的 ResourceChunk/Bit 是用户明确不要的
local function isOreClass(c)
  if string.find(c,"ResourceChunk",1,true) or string.find(c,"_Chunk",1,true) then return false end
  if string.find(c,"Bit",1,true) and string.find(c,"Resource",1,true) then return false end
  local k={"Vein","Deposit","Ore","Mineral","Crystal","Gem","Collectable","Aquarq","Morkite","Nitra",
           "Bismor","Croppa","Jadiz","Magnite","Umanite","Enor","Dystrum","Hollomite","Bittergem"}
  for _,w in ipairs(k) do if string.find(c,w,1,true) then return true end end
  return false
end
-- 进任务后做一次全 Actor 类名探测：把矿脉真名写进日志（DRG 的类名事先不知道，只能实测）
local function oreProbe()
  if oreProbeDone then return end
  oreProbeDone=true
  local ok,l=pcall(FindAllOf,"Actor")
  if not ok or type(l)~="table" then W("  [矿石探测] FindAllOf(Actor) 失败") return end
  local cnt,sample={},{}
  local k={"Resource","Vein","Deposit","Ore","Gem","Crystal","Mineral","Collectab","Chunk",
           "Aquarq","Morkite","Nitra","Gold","Bismor","Croppa","Jadiz","Magnite","Umanite","Enor"}
  for _,a in ipairs(l) do
    if valid(a) then
      local okc,c=pcall(function() return a:GetClass():GetFullName() end)
      if okc and type(c)=="string" then
        for _,w in ipairs(k) do
          if string.find(c,w,1,true) then cnt[c]=(cnt[c] or 0)+1 break end
        end
      end
    end
  end
  local n=0
  for c,v in pairs(cnt) do
    n=n+1
    if n<=45 then W("  [矿石探测] "..v.." × "..c) end
    if isOreClass(c) and #sample<1 then sample[#sample+1]=c end
  end
  W("  [矿石探测] 共 "..n.." 种；Actor 总数 "..#l.."；选了 "..#sample.." 类做矿脉")
  -- 收集矿脉 Actor（位置每轮再读）
  oreList={}
  for _,a in ipairs(l) do
    if #oreList<200 and valid(a) then
      local okc,c=pcall(function() return a:GetClass():GetFullName() end)
      if okc and type(c)=="string" and isOreClass(c) then
        oreList[#oreList+1]={a=a,name=oreName(c)}
      end
    end
  end
  W("  [矿石探测] 已收集未挖掘矿脉 "..#oreList.." 个")
end
local function espBox(view,pawn,x,y,z,hz)
  if view==nil then return nil end
  local tx,ty=projWith(view,pawn,x,y,z+hz)
  local bx,by=projWith(view,pawn,x,y,z-hz)
  if not tx or not bx then return nil end
  local top,bot=ty,by
  if top>bot then top,bot=bot,top end
  local h=bot-top
  if h<4 then return nil end
  local cx=projWith(view,pawn,x,y,z)
  local mx=cx or tx
  local w=h*0.62
  if w<h*0.3 then w=h*0.3 end
  return mx-w/2, top, mx+w/2, bot
end
local function espRescan(dE,dM)
  local list={}
  if dE>0 then
    local ok,l=pcall(FindAllOf,"Pawn")
    if ok and type(l)=="table" then
      for _,p in ipairs(l) do
        if valid(p) then
          local okc,c=pcall(function() return p:GetClass():GetFullName() end)
          -- 只要 /Game/Enemies/ 下的怪；/Game/Critters/（战利虫、跳蚤）不算敌人
          if okc and type(c)=="string" and string.find(c,"/Game/Enemies/",1,true) then
            if not espDbg then espDbg=true W("  [ESP] 敌人类名示例: "..c.." → "..enemyName(c)) end
            list[#list+1]={a=p,tag="E",lim=dE,hz=90,name=enemyName(c)}
          end
        end
      end
    end
  end
  if dM>0 then
    for _,o in ipairs(oreList) do
      if valid(o.a) then list[#list+1]={a=o.a,tag="M",lim=dM,hz=35,name=o.name} end
    end
  end
  return list
end
LoopAsync(150, function()
  if _G.__FSDT_GEN~=MYGEN then return true end
  local ok,e=pcall(function()
    local F=readF()
    if next(F)==nil then return end
    local dE=tonumber(F.esp_enemies) or 0
    local dM=tonumber(F.esp_minerals) or 0
    if dE<=0 and dM<=0 then
      if #espTargets>0 then espTargets={} wf(G..ESP_FILE,"# ESP 关闭\n") end
      return
    end
    if dE>120 then dE=120 end
    if dM>120 then dM=120 end
    local pc,cm,pawn=ctx()
    if not (valid(pc) and valid(pawn)) then return end
    local now=os.time()
    if espWorldAt==nil then espWorldAt=now return end
    if now-espWorldAt<15 then return end          -- 换图/进任务后稳定 15 秒才开工
    local view=prepareView(pc)
    if view==nil then return end
    local px,py,pz=locOf(pawn)
    if not px then return end
    if dM>0 and not oreProbeDone then oreProbe() end
    if now-(espEnumAt or 0)>=3 then
      espEnumAt=now
      espTargets=espRescan(dE,dM)
      W("  [ESP] 目标重扫: "..#espTargets.." 个（敌人 "..dE.."m / 矿物 "..dM.."m）")
    end
    local out,n={},0
    for _,t in ipairs(espTargets) do
      if n>=60 then break end
      if valid(t.a) then
        local x,y,z=locOf(t.a)
        if x then
          local d=math.sqrt((x-px)^2+(y-py)^2+(z-pz)^2)/100.0
          if d<=t.lim then
            local x1,y1,x2,y2=espBox(view,pawn,x,y,z,t.hz)
            if x1 and x2>0 and y2>0 and x1<vpW and y1<vpH then
              out[#out+1]=string.format("%s %.0f %.0f %.0f %.0f %.0f %s\n",t.tag,x1,y1,x2,y2,d,t.name or "?")
              n=n+1
            end
          end
        end
      end
    end
    espSelfRound=espSelfRound+1
    if espSelfRound%20==1 then
      local saw,say=projWith(view,pawn,px+view.F[1]*1000,py+view.F[2]*1000,pz+view.F[3]*1000+60)
      W("  [ESP自检] 方式="..tostring(projMode).." 视口="..vpW.."x"..vpH.." 目标="..#espTargets
        .." 写出="..n.." 正前方10米="..(saw and (math.floor(saw)..","..math.floor(say)) or "失败")
        .."（期望 ≈ "..math.floor(vpW/2)..","..math.floor(vpH/2).."）")
    end
    wf(G..ESP_FILE,"# Carlhand ESP "..os.date("%H:%M:%S").." 目标"..#espTargets.."\n"..table.concat(out))
  end)
  if not ok then W("  !!! ESP 循环异常(已继续): "..tostring(e)) end
  return false
end)
-- ================= 主循环 =================
W("\n>> 功能引擎 v48 已载入 "..os.date("%H:%M:%S").." gen="..MYGEN)
local tick,miss,ready,initDone=0,0,false,false
local lastPawn=nil          -- 用对象地址判断换图/重生（不再用 GetFullName，省一次危险调用）
local pendingReset=false    -- 曾经失去过角色对象（换图/读图中）→ 下次拿到角色必须重置+静默
local graceUntil=0          -- 换图/重生后的静默窗口
local worldSince=0          -- 当前世界稳定的起始时间
local lastRevive=nil
LoopAsync(300, function()
  if _G.__FSDT_GEN~=MYGEN then W("  [引擎] 检测到新版本，旧循环 gen="..MYGEN.." 退出") return true end
  local ok,e=pcall(function()
    local F=readF()
    if next(F)==nil then return end
    tick=tick+1
    if not ready and (tick % 10 ~= 0) then return end
    local pc,cm,pawn=ctx()
    if not (valid(pc) and valid(pawn)) then miss=miss+1 ready=false pendingReset=true return end
    if miss>0 then W("  世界已就绪（跳过 "..miss.." 次未就绪探测）") miss=0 end
    ready=true
    if not initDone then
      for k in pairs(ACT) do last[k]=tostring(F[k] or "0") end
      initDone=true
      W("  首次载入：已按当前状态登记开关，不触发任何命令")
    end
    local now=os.time()
    local pa=addrOf(pawn)
    if pa==nil then miss=miss+1 ready=false return end
    if pa~=lastPawn or pendingReset then
      pendingReset=false
      lastPawn=pa
      lightObj=nil lightLogged=false ccHits={} flyState=0 flyOrig=nil flyLogged=false
      espTargets={} espEnumAt=0 espWorldAt=now      -- ESP 目标列表作废，重新计时
      oreList={} oreProbeDone=false                 -- 矿脉要重新探测
      fastState=0 quadState=0                       -- 重生后游戏自带的开关状态未知，重新确认一次
      lastWrote=nil walkBase=nil sprintBase=nil basesCaptured=false
      sprintRef=nil
      graceUntil=now+5 worldSince=now
      W("  [换图/重生] 检测到新的角色对象: "..nm(pawn).."，已重置缓存并静默 5 秒")
      return                                    -- 这一 tick 什么都不做，避开刚生成时的对象抖动
    end
    if now<graceUntil then return end
    if not basesCaptured then
      local v0=num(gp(mov(pawn),"MaxWalkSpeed"))
      if v0 then if v0<=450 then walkBase=v0 else sprintBase=v0 end end
      basesCaptured=true
      W("  [速度基准] 走路="..tostring(walkBase).." 冲刺="..tostring(sprintBase))
    end
    pcall(applySpeed,F,pawn,pc)
    -- 跳跃高度/跳次：先读再比，只有真的不一致才写（减少每 tick 的对象写操作）
    local jz=tonumber(F.jump_z)
    if jz and jz>0 then
      if jz>5000 then jz=5000 elseif jz<100 then jz=100 end
      local c=mov(pawn)
      local tgt=valid(c) and c or pawn
      local cur=num(gp(tgt,"JumpZVelocity"))
      if not cur or math.abs(cur-jz)>0.5 then sp(tgt,"JumpZVelocity",jz) end
    end
    local jm
    if F.inf_jump=="1" then jm=999 else jm=tonumber(F.jump_max) or 1 end
    if jm<1 then jm=1 elseif jm>999 then jm=999 end
    local jc=num(gp(pawn,"JumpMaxCount"))
    if not jc or math.abs(jc-jm)>0.5 then sp(pawn,"JumpMaxCount",jm) end
    pcall(applyFly,F,pawn,pc)
    pcall(applyAim,F,pawn,pc)
    -- ESP 已改到独立的 150ms 循环里（见上方 LoopAsync(150)），主循环不再管它
    pcall(applyCC,F,pawn,now)
    pcall(applyLight,F,pawn)
    for k,ent in pairs(ACT) do
      local want=tostring(F[k] or "0")
      if last[k]~=want then last[k]=want W("  ["..k.."] -> "..want)
        if want~="0" then
          if type(ent)=="table" then
            if type(ent.onFunc)=="function" then pcall(ent.onFunc,pawn,pc) elseif ent.on then pcall(cmd,ent.on,pc) end
          else pcall(cmd,ent,pc) end
        elseif type(ent)=="table" then
          if type(ent.offFunc)=="function" then pcall(ent.offFunc,pawn,pc) elseif ent.off then pcall(cmd,ent.off,pc) end
        end
      end
    end
    -- ===== 参数型命令（值变化才发一次；0 / 负 = 不发送）=====
    do
      local sh=tonumber(F.set_health) or 0
      if sh>0 then
        if last.set_health~=tostring(sh) then last.set_health=tostring(sh) pcall(cmd,"Cheat_SetHealth "..math.floor(sh),pc) end
      else last.set_health=nil end
      local ks=tonumber(F.kill_slot) or 0
      if ks>0 then
        if last.kill_slot~=tostring(ks) then
          last.kill_slot=tostring(ks)
          pcall(cmd,"C_KillPlayer "..math.floor(ks),pc)     -- 游戏若不支持编号参数，会落到本地玩家身上
        end
      else last.kill_slot=nil end
      local ip=tonumber(F.intox_player) or 0
      local iv=tonumber(F.intox_value) or 0
      if iv>0 or ip>0 then
        local tag=tostring(ip).."|"..tostring(iv)
        if last.intox~=tag then
          last.intox=tag
          if ip>0 then pcall(cmd,"C_Intoxication_Set "..math.floor(ip).." "..math.floor(iv),pc)
          else pcall(cmd,"C_Intoxication_SetAll "..math.floor(iv),pc) end
        end
      else last.intox=nil end
    end
    if F.auto_revive=="1" then
      local dead=false
      local b=gp(pawn,"bIsDead") if type(b)=="boolean" and b then dead=true end
      if not dead then local hc0=gp(pawn,"Health") b=gp(hc0,"bIsDead") if type(b)=="boolean" and b then dead=true end end)FSDLUA",
R"FSDLUA(      if dead or (lastRevive==nil or now-lastRevive>=2) then lastRevive=now pcall(cmd,"C_Revive",pc) end
    end
    if F.killaura=="1" and now-worldSince>=8 then pcall(monsterWipe,F,pc,pawn,now) end
    if tick%40==0 then
      local c=mov(pawn)
      W("  状态: 走速="..tostring(gp(c,"MaxWalkSpeed")).." 跳速="..tostring(gp(c,"JumpZVelocity"))
        .." 移动模式="..tostring(gp(c,"MovementMode")).." 跳次="..tostring(gp(pawn,"JumpMaxCount")))
    end
  end)
  if not ok then W("  !!! 主循环异常(已继续): "..tostring(e)) end
  return false
end)
)FSDLUA",
};
static const int kCheatEngineLuaPartCount = 6;
