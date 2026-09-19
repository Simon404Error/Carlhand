#pragma once
// 游戏内 Lua 引擎（UE4SS 热加载）。DLL 启动时写入 <游戏目录>\FSD\Binaries\Win64\FSDTrainer_code.lua，
// 并在 FSDTrainer_cmd.txt 里写一行 lua 指令加载它。每 300ms 读 FSDTrainer_features.txt。
// v24：飞行含 bCheatFlying（可上下飞）、奔跑属性探测、夜视保守值、怪物全灭（无半径）、卸下喷气靴。
// 注意：MSVC 单个字符串字面量有 16KB 上限（C2026），所以引擎脚本按块存放，写入时依次拼接。
static const char* kCheatEngineLuaParts[] = {
R"FSDLUA(-- Carlhand 功能引擎 v96
-- v68（喷气靴换路：不再调游戏那条必崩的接口，改抄喷气靴箱子的做法）：
--   用户任务里点 v67 的按钮崩了，崩溃报告 `EXCEPTION_ACCESS_VIOLATION reading 0x1c`，
--   PCallStack 与 14:45 的 `cm:C_JetBoots_Enable_MK2()` **9 帧逐帧一致**
--   ⇒ 游戏自己"给喷气靴"的代码在本版就是坏的（RPC / 控制台 MK2 三条入口殊途同归），不是参数或调用方式问题。
--   改法（解包基础 pak 读游戏自己的 BP_JetBootsBox 得来）：玩家穿喷气靴 = 给角色
--   `AddComponentByClass(BP_JetBootsMovement_C, …, false)` 挂一个 JetBootsMovementComponent（箱子就是这么给的）。
--   顺带：`tostring(gp(...))` 抛异常那个坑（v67 修的）保留。
-- v67（喷气靴，照抄已装 mod「全房间喷气靴 / Automatic Equipping Jet Boots」的做法）：
--   解包 FSD-WindowsNoEditor_Mods.pak 读到 mod 蓝图的名称表，里面有 Server_CheatJetBoots /
--   Server_CheatJetBoots_MK2（虚调用＝按名字调）⇒ 装备喷气靴的正路就是**角色上的 Server_CheatJetBoots()**。
--   本次在游戏里用反射实读签名（ForEachFunction + ForEachProperty）：两个函数都是 **0 参数**，
--   flags=2231488（含 FUNC_Net，确是服务端 RPC），outer=PlayerCharacter。
--   ⇒ ① 基础版直呼；② MK2 永久停用（控制台命令与直呼都在游戏自己代码里 AV 读 0x1c）；③ 只在任务里调
--      （CheatManager 有效；太空站里调 Server_* 必崩，v58 那次就是）；④ 顺带修掉自动装备那句 `valid(cm2)`。 
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
-- v49（奔跑真因，已游戏内实测）：游戏的冲刺根本不改 MaxWalkSpeed——冲刺时 MaxWalkSpeed 恒为 300，
--   真正的冲刺速度来自角色属性 FSD.PlayerCharacter.RunningSpeed（默认 435；走路 300）。
--   实测证据：按住 W 走路 V=300；W+Shift 冲刺 V=435 而 MaxWalkSpeed 仍是 300；
--   把 RunningSpeed 改成 1305 后同一秒冲刺速度立刻变 1305（走路仍 300），改回 435 立刻恢复。
--   所以奔跑倍率现在只作用于 RunningSpeed（=冲刺基准×倍率），完全不碰 MaxWalkSpeed：
--   冲刺更快、走路绝对不变；改数值后 60ms 内生效，不用重新按 Shift。
--   同时删掉 v48 那条"把走路值当成冲刺值再乘一遍"的 60ms 逻辑（走路倍率>1 时会把走路也乘快）。
-- v50：自瞄范围真正落地——删掉"猜 Aim 属性乘倍数"的老做法，改成每 30ms 把 pc 的 ControlRotation
--   拧向"范围米内最近的 /Game/Enemies/ 敌人"（相机位置为起点，瞄身体+25cm）。0=关。
if _G.__FSDT_ENGINE_V66 then return end
_G.__FSDT_ENGINE_V66 = true
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
-- ★v67：gp() 读不到属性时返回的是"0 个值"，直接把它的结果塞进 tostring 会抛
--   "bad argument #1 to 'tostring' (value expected)"，把这一 tick 整个废掉（角色没有 CharacterMovement 时必现）。
--   凡是要拼进日志的一律用 ts() 兜。
local function ts(v) if v==nil then return "nil" end return tostring(v) end

-- ★v60 又一个「定义顺序」坑（和 v57 的 aimVisible 同类，但更隐蔽）：ACT 表里的 onFunc 闭包要用
-- cmd / getCM / mov，可它们原来定义在 ACT **之后** —— 那些名字在闭包里就成了全局 nil，
-- pcall(nil,...) 静默失败 → 表现是「点了按钮没反应」（喷气靴/解除抓捕/呼叫补给仓都中招过）。
-- 所以先前置声明，后面改用赋值式定义（不能再写 local function，否则会新建局部把前置声明遮住）。
)FSDLUA",
R"FSDLUA(local cmd, getCM, mov, jbArm
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
  -- ⚠ C_Revive 是坏路（两次崩溃都由它触发）：这里留着只为记录，**功能表里没有任何 key 映射到 revive**，
  --   绝对不要给它接菜单开关（自起已改成纯属性写入，见 applySelfRevive）。
  revive="C_Revive", killall_once="C_KillAll", break_armor="C_BreakAllEnemyArmor",
  -- ★v96：用户要求"所有功能只对自己生效" → 影响队友的映射直接删掉（杀全队/把队友传过来/传送到队友）
  spawn_wave="C_SpawnNormalWave", spawn_scripted="C_SpawnScriptedWave", spawn_rewarder="C_SpawnEvenRewarder",
  spawn_bosco="SpawnBosco", spawn_critters="SpawnCritters", spawn_particles="C_SpawnAllParticles",
  spawn_droppod="C_SpawnDropPodOnSelf", spawn_barrel="C_SpawnBarrelOnPlayer", spawn_dancing="C_SpawnDancingCharacterOnSelf",
  datacore="C_Facility_SpawnDataCore", disable_shields="C_Facility_DisableShields",
  mule_speed="C_Escort_SetMuleSpeed 1000",
  suicide="C_KillPlayer", kill_team="C_KillTeam",
  -- v55：解除抓捕（被水蛭/捕虫抓住时）——把场上敌人+中立怪清掉自然就脱身
  -- ★v96 新增：我要当逃兵 = 立刻回空间站（不判定成功/失败）。
  -- 依据：游戏 pak 里确实有 `/Game/.../Cheat_ReturnToSpacerig.uasset`（和 Cheat_Resupply 同族）。
  -- 偷懒不得：Cheat_* 这一族有的会崩，所以按"候选顺序"逐个试，**每点一次换下一个**，
  -- 每次都把"试的是哪条命令"写进 dump，方便下一轮确定哪条真的可用。
  desert={ onFunc=function(pawn,pc)
      desertIdx=(desertIdx or 0)+1
      local list={ "Cheat_ReturnToSpacerig", "CheatScript ReturnToSpacerig", "C_FailMission" }
      local c=list[((desertIdx-1)%#list)+1]
      W("  [逃兵] 第 "..desertIdx.." 次尝试 → 控制台命令 "..c)
      pcall(cmd,c,pc)
    end },
  unjail={ onFunc=function(pawn,pc)
      pcall(cmd,"C_KillAll",pc)
      pcall(cmd,"C_KillAllNeutral",pc)
      W("  [解除抓捕] 已发 C_KillAll + C_KillAllNeutral")
    end },
  -- ★v87：按用户要求**删掉**该功能（历史：直呼游戏函数崩过一次、连控制台 C_Resupply 也崩过一次 → 2 次 dump）。
  --   这里保留一个只写日志的空节点（菜单里该功能段已删除）。
  resupply={ onFunc=function(pawn,pc)
      W("  [补给仓] 该功能已删除（历史上有 2 次崩溃 dump）")
    end },
  inf_flare={ on="C_FlareInfiniteDuration" }, fast_flares={ on="C_SetFastFlares" },
  inf_ammo={ on="C_SetAmmoCostEnabled" },
  fullbright={ on="C_ToggleFadingEnabled", off="C_ToggleFadingEnabled" },
  -- v74：喷气靴（界面名「装备喷气靴mk2」）——只给自己挂 MK2 组件+补燃料+接管空中推力，实现在下面那一整段。
  jetboots={ onFunc=function(pawn,pc) pcall(jbArm,pawn,pc) end },
  jetboots_off={ onFunc=function(pawn,pc) pcall(jetbootsOff,pawn,pc) end },
}
local last={}
local function readF()
  local t={} local ok,f=pcall(io.open,G.."FSDTrainer_features.txt","r") if not ok or not f then return t end
  for line in f:lines() do local k,v=string.match(line,"^%s*([%w_]+)%s*=%s*([%w%._%-]*)") if k and v~="" then t[k]=v end end
  f:close() return t
end
local kismet=StaticFindObject("/Script/Engine.Default__KismetSystemLibrary")
function cmd(c,pc) if not kismet or not valid(pc) or type(c)~="string" then return end
  local ok=pcall(function() return kismet:ExecuteConsoleCommand(pc,c,nil) end) W("  命令["..c.."] ok="..tostring(ok)) end
local function ctx() local pc=FindFirstOf("PlayerController") if not pc then return end return pc, gp(pc,"CheatManager"), gp(pc,"Pawn") end
function mov(pawn) return gp(pawn,"CharacterMovement") end

-- ================================================================
--  喷气靴 MK2（v74 重写：整段只有这一份实现，前面那些试验性代码全删了）
--  需求：点一次「装备喷气靴mk2」= 只给**自己**当场穿上 MK2 喷气靴，别人不受影响。
--  实现只做四件事（每一步都有实测依据）：
--    ① 给本地玩家的 Pawn 挂一个 `BP_JetBootsMovement_Infinite`（= MK2/无限版；游戏里没有单独的 MK2 蓝图）。
--       必须"延迟完成创建"：先设 Character 再 FinishAddComponent，否则组件的 Character 是空的、内部不工作。
--    ② 把组件燃料补满：`CurrentJetFuel = Settings.MaxJetFuel`（默认是 0，所以光挂上不会喷）。
--    ③ 接管推力输入：组件收不到游戏的跳跃输入（实测空中按跳跃 isUsing 恒 false），
--       但直呼 `OnJumpPressed()`（空中）会让它立刻进"喷气中"，`OnJumpReleased()`（落地）收工。
--       所以开一个 100ms 循环：空中→OnJumpPressed，落地→OnJumpReleased，燃料低了自动续上。
--    ④ 只碰 ctx() 拿到的那个本地 Pawn，绝不遍历/修改其他玩家。
--  禁用项（血泪）：`Server_CheatJetBoots` / `C_JetBoots_Enable_MK2` 在本版游戏里必崩
--    （EXCEPTION_ACCESS_VIOLATION reading 0x1c，崩溃栈与 14:45 那次逐帧一致）→ 永不调用。
-- ================================================================
local JB_CLS_PATH = "/Game/GameElements/JetBoots/Blueprint/BP_JetBootsMovement_Infinite.BP_JetBootsMovement_Infinite_C"
local JB_COMP_INF = "BP_JetBootsMovement_Infinite_C"
local JB_COMP_NAMES = {"BP_JetBootsMovement_Infinite_C", "BP_JetBootsMovement_C"}
-- 角色身上属于"我们的"喷气靴组件（外层 Outer == 角色）；第二个返回值是场上总数（日志用）
function jbMineOf(pawn)
  local mine,total={},0
  for _,cn in ipairs(JB_COMP_NAMES) do
    local inf=(cn==JB_COMP_INF)
    pcall(function()
      local l=FindAllOf(cn)
      if type(l)=="table" then
        total=total+#l
        for _,c in ipairs(l) do
          local o=nil pcall(function() o=c:GetOuter() end)
          if addrOf(o)~=nil and addrOf(o)==addrOf(pawn) then mine[#mine+1]={c=c,inf=inf} end
        end
      end
    end)
  end
  return mine,total
end
-- v76：整套代码**一律不调 GetFullName**（引擎的老崩因：UE4SS.dll+0x52DBB9 读 0x18 = 对已销毁对象取名字）。
-- 判断"是哪种组件"只靠 FindAllOf 的类名（上面的 JB_COMP_NAMES）+ 外层地址比对，日志里只打地址 + MK2/基础版。
function jbLabel(e)
  if type(e)~="table" then return "?" end
  return (e.inf and "MK2" or "基础版").."@"..ts(addrOf(e.c))
end
-- 已经穿的是 MK2/无限版吗
function jbHasMK2(pawn)
  for _,e in ipairs(jbMineOf(pawn)) do if e.inf then return true end end
  return false
end
-- 找组件（优先无限版）
function jbPick(pawn)
  local mine=jbMineOf(pawn)
  for _,e in ipairs(mine) do if e.inf then return e.c,mine end end
  if #mine>0 then return mine[#mine].c,mine end
  return nil,mine
end
-- 把组件调成"能用"：Character 指向角色 + 燃料满；返回是否成功
function jbPrime(c,pawn,quiet)
  if not valid(c) or not valid(pawn) then return false end
  local ch=gp(c,"Character")
  if addrOf(ch)~=addrOf(pawn) then sp(c,"Character",pawn) end
  local mx=50
  local st=gp(c,"Settings")
  local v=gp(st,"MaxJetFuel") if type(v)=="number" and v>0 then mx=v end
  local okf=sp(c,"CurrentJetFuel",mx)
  if not quiet then
    W("  [喷气靴] 组件就绪 @"..ts(addrOf(c)).."；Character="..ts(addrOf(gp(c,"Character"))).."/角色 "..ts(addrOf(pawn))
      .."；燃料写入 ok="..tostring(okf).." 读回="..ts(gp(c,"CurrentJetFuel")).."（上限 "..tostring(mx).."）")
  end
  return true
end
-- 挂 MK2 组件（延迟完成创建）。返回组件
function jbAttach(pawn)
  local cls=StaticFindObject(JB_CLS_PATH)
  if not valid(cls) then W("  [喷气靴] 找不到 "..JB_CLS_PATH.."（游戏更新了？）") return nil end
  local t={Translation={X=0,Y=0,Z=0},Rotation={X=0,Y=0,Z=0,W=1},Scale3D={X=1,Y=1,Z=1}}
  local ok1,c=pcall(function() return pawn:AddComponentByClass(cls,false,t,true) end)
  if ok1 and valid(c) then
    sp(c,"Character",pawn)               -- 关键：注册前就把它指到角色
    sp(c,"CurrentJetFuel",50.0)
    local ok2=pcall(function() pawn:FinishAddComponent(c,false,t) end)
    W("  [喷气靴] 挂 MK2 组件（延迟完成创建）Finish ok="..tostring(ok2).." 组件@"..ts(addrOf(c)))
    if ok2 then return c end
  end
  -- 兜底：普通创建（Character 之后再补，见 jbPrime）
  local ok3,c3=pcall(function() return pawn:AddComponentByClass(cls,false,t,false) end)
  W("  [喷气靴] 延迟创建失败("..tostring(ok1)..")，退回普通创建 ok="..tostring(ok3).." 组件@"..ts(addrOf(c3)))
  if ok3 and valid(c3) then return c3 end
  return nil
end
-- 按钮入口：只给自己穿 MK2
function jbArm(pawn,pc)
  if not valid(pawn) then jbPending=true W("  [喷气靴] 还没拿到角色对象 → 已记下，进游戏后自动补上") return end
  local c,mine,total=jbPick(pawn)
  if not c or not jbHasMK2(pawn) then
    if jbHasMK2(pawn) then
      W("  [喷气靴] 已经穿着 MK2 了（@"..ts(addrOf(c)).."）→ 只补燃料")
    else
      c=jbAttach(pawn) or jbPick(pawn)
      mine,total=jbMineOf(pawn)
    end
  end
  if not valid(c) then W("  [喷气靴] 没能拿到组件（角色身上 "..#mine.." 个 / 场上 "..total.." 个）") return end
  jbPrime(c,pawn)
  _G.__FSDT_JB = true
  _G.__FSDT_JB_COMP = c
  W("  [喷气靴] MK2 已就绪（只对自己）：空中会自动喷（落地自动收）")
end
-- 卸下：只清我们自己那一份
function jetbootsOff(pawn,pc)
  _G.__FSDT_JB=nil
  local c=jbPick(pawn)
  if valid(c) then pcall(function() c:OnJumpReleased() end) end
  local mine,total=jbMineOf(pawn)
  if #mine==0 then W("  [喷气靴] 卸下：角色身上没有我们的组件（场上共 "..total.." 个）") return end
  local k=StaticFindObject("/Script/Engine.Default__KismetSystemLibrary")
  for _,x in ipairs(mine) do
    local r1=pcall(function() k:K2_DestroyComponent(x) end)
    if not r1 then pcall(function() x:DestroyComponent(true) end) end
  end
  _G.__FSDT_JB_COMP=nil
  W("  [喷气靴] 已停止接管并尝试销毁 "..#mine.." 个组件（剩下 "..#(jbMineOf(pawn)).." 个）")
end
-- 驱动循环（100ms）：只在自己按过按钮后工作
-- v77 行为修正（用户反馈"只按一次空格却一直飞"）：喷气靴应该**按住才喷、松开就停**。
--   引擎读不到按键，但 DLL 现在每帧把空格状态写进功能表（jmp=0/1，和 lmb 同一套机制）。
--   · 有 jmp（新 DLL）：空中 + 按住空格 → OnJumpPressed；松开 或 落地 → OnJumpReleased。
--   · 没有 jmp（旧 DLL、还没重启）：退化成"每次离地只喷 0.8 秒"（8 个 tick），至少不会一直飞。
local jbFindAt, jbEngaged, jbBurstTicks, jbWarned = nil, false, nil, false
LoopAsync(100, function()
  if _G.__FSDT_GEN~=MYGEN then return true end
  local ok0,e0=pcall(function()
    -- ★v81：血/盾上限在 100ms 循环里持续重写（游戏每次都改回 110/25，主循环 300ms 太慢、HUD 会闪回）
    local F0=readF()
    local pc0,cm0,pawn0=ctx()
    if valid(pawn0) then
      -- ★v93：血/盾上限已按用户要求**整个删除**（写进去游戏每帧改回，HUD 还是 125/30）→ 这里不再驱动
      -- v84：每个"真角色"只探一次（用 pawn 地址做标记；启动画面的 DefaultPawn 不算）
      if jbDumpFlags.probePawn ~= addrOf(pawn0) then
        local cc=clsOf(pawn0)
        if string.find(cc,"Character",1,true) then
          jbDumpFlags.probePawn=addrOf(pawn0)
          pcall(probeExtras,pawn0)
        end
      end
    end
  end)
  if not ok0 then W("  !!! 血盾持续重写异常(已继续): "..tostring(e0)) end
  if not _G.__FSDT_JB then return false end
  local ok,e=pcall(function()
    local pc,cm,pawn=ctx()
    if not valid(pawn) then return end
    local now=os.time()
    local c=_G.__FSDT_JB_COMP
    if not valid(c) or jbFindAt==nil or now-jbFindAt>=2 then
      jbFindAt=now
      c=jbPick(pawn)
      _G.__FSDT_JB_COMP=c
    end
    if not valid(c) then return end
    local f=gp(c,"CurrentJetFuel")
    if type(f)=="number" and f<8 then sp(c,"CurrentJetFuel",50.0) end
    local jmp=readF().jmp
    if jmp==nil and not jbWarned then
      jbWarned=true
      W("  [喷气靴] 当前 DLL 没有按键状态（jmp）→ 先按「离地小喷 0.8 秒」跑；重启换上新 DLL 后就是按住才喷")
    end
    local mm=num(gp(mov(pawn),"MovementMode"))
    local airborne=(mm==3 or mm==5)
    local want=false
    if jmp=="1" then want=airborne
    elseif jmp=="0" then want=false
    else
      if airborne then jbBurstTicks=(jbBurstTicks or 0)+1 else jbBurstTicks=nil end
      want = airborne and jbBurstTicks~=nil and jbBurstTicks<=8
    end
    if want and not jbEngaged then
      jbEngaged=true
      local ok2=pcall(function() c:OnJumpPressed() end)
      W("  [喷气靴] 开始喷（按住空格，ok="..tostring(ok2).." 燃料="..ts(gp(c,"CurrentJetFuel")).."）")
    elseif (not want) and jbEngaged then
      jbEngaged=false
      pcall(function() c:OnJumpReleased() end)
      W("  [喷气靴] 停止喷（松开/落地，燃料="..ts(gp(c,"CurrentJetFuel")).."）")
    end
  end)
  if not ok then W("  !!! 喷气靴循环异常(已继续): "..tostring(e)) end
  return false
end)


)FSDLUA",
R"FSDLUA(-- ================= 移速 / 奔跑 =================
-- 奔跑（v49 实测结论）：游戏把"冲刺速度"存在角色属性 RunningSpeed 上（走路 300 / 冲刺 435），
-- 冲刺时 MaxWalkSpeed 保持 300 不变。所以奔跑倍率 = 改 RunningSpeed，不去动 MaxWalkSpeed。
local walkBase, sprintBase, lastLogged, basesCaptured, sprintDbg = nil, nil, nil, false, 0
local runBase, runLogged = nil, nil     -- 冲刺基准（RunningSpeed 原值）与最近写入值
local fastState, quadState = 0, 0   -- 游戏自带"快速移动"/"四倍镐"的开关状态
local lastWrote = nil               -- 上一次我们写进 MaxWalkSpeed 的值（用来区分"我们写的"和"游戏自己在冲刺"）
-- 取 CheatManager：只读 pc.CheatManager（安全）。注意：绝不要用 FindAllOf 去试探可能存在/不存在的类名，
-- 也不要用 obj["函数名"] 下标——UE4SS 在这两种情况下会抛出 C++ 异常，pcall 接不住，会直接打死 Lua 线程。
local cmFixed, cmDiagDone = nil, false
function getCM(pc)
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
-- 奔跑：只改角色属性 RunningSpeed（=冲刺基准×倍率）。走路 MaxWalkSpeed 不受影响。
local function applyRun(F,pawn)
  local sm=tonumber(F.sprint_speed) or 1
  if sm<=0 then sm=1 end
  if sm>20 then sm=20 elseif sm<0.1 then sm=0.1 end
  if not valid(pawn) then return end
  if runBase==nil then
    local b=num(gp(pawn,"RunningSpeed"))
    if b and b>=50 and b<=20000 then runBase=b W("  [奔跑] 冲刺基准="..f2(runBase).."（游戏原值）") end
  end
  if runBase==nil then return end
  local want=runBase*sm
  if want<50 then want=50 elseif want>20000 then want=20000 end
  local cur=num(gp(pawn,"RunningSpeed"))
  if cur==nil or math.abs(cur-want)>0.5 then
    sp(pawn,"RunningSpeed",want)
    if runLogged~=want then
      runLogged=want
      W("  [奔跑] 冲刺基准"..f2(runBase).."×"..f2(sm).."="..f2(want).." 读回="..ts(gp(pawn,"RunningSpeed")))
    end
  end
end
local function applySpeed(F,pawn,pc)
  local walkMult=tonumber(F.walk_speed) or 1
  if walkMult<=0 then walkMult=1 end
  if walkMult>20 then walkMult=20 elseif walkMult<0.1 then walkMult=0.1 end
  pcall(applyRun,F,pawn)                  -- 奔跑（冲刺）走 RunningSpeed，见上
  local c=mov(pawn) if not valid(c) then return end)FSDLUA",
R"FSDLUA(  local cur=num(gp(c,"MaxWalkSpeed"))
  if cur and (cur<50 or cur>20000) then cur=nil walkBase=nil W("  [移速] 读到异常速度值，已重置基准") end
  if cur and cur>=120 and cur<=420 then walkBase=cur end
  local base=walkBase or 300
  -- 走路目标 = 走路基准 × 移速倍率，与奔跑倍率无关
  local walkT=base*walkMult
  if walkT<50 then walkT=50 elseif walkT>20000 then walkT=20000 end
  local ours = (lastWrote and cur and math.abs(cur-lastWrote)<=math.max(5,lastWrote*0.05))
  -- ① 残留加速（倍率回默认后还远大于走路值）→ 强制回落
  if walkMult==1 and not ours and cur and cur>base*2.6 then
    sp(c,"MaxWalkSpeed",walkT) lastWrote=walkT
    if lastLogged~=walkT then
      lastLogged=walkT
      W("  [移速] 检测到残留加速，已回落="..f2(walkT).."（读回="..ts(gp(c,"MaxWalkSpeed")).."）")
    end
    return
  end
  -- ② 当前值明显高于走路目标、又不是我们写的 → 游戏自己在用更高值（冲刺/滑铲等），不抢
  if cur and not ours and cur>walkT*1.02 then return end
  sp(c,"MaxWalkSpeed",walkT)
  if lastWrote~=walkT then lastWrote=walkT end
  if lastLogged~=walkT then
    lastLogged=walkT
    local rb=num(gp(c,"MaxWalkSpeed"))
    W("  [移速] 基准="..tostring(walkBase or 300).." 写="..f2(walkT).." 读回="..tostring(rb)..(rb and math.abs(rb-walkT)<1.5 and " ✓生效" or " ✗未生效"))
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
)FSDLUA",
R"FSDLUA(-- ================= 广域手电筒（v96：替代原「夜视/关掉全地图阴影」）=================
-- 实测（dump）：玩家的照明是 pawn 上的 `SpotLight`（SpotLightComponent，
--   例 .../BP_GunnerCharacter_C_xxx.SpotLight），游戏原值 Intensity=50、AttenuationRadius=110。
-- v92~v95 的"关掉全地图阴影"要扫 1092 个灯光组件、上万次属性读写（dump 里有 458/509 这类数字），
--   又卡又危险（UE4SS 内部 AV），**整段删除**。
-- 现在只动**自己这一盏**：半径拉满 + 锥角开到接近全向 + 关阴影 + 提亮；关掉时按记下的原值还原。
-- 全部纯属性写入，不调任何游戏函数；换图/重生后旧灯销毁、新灯是新对象，会自动重新采集原值。
local WL_PROPS = { "Intensity","AttenuationRadius","OuterConeAngle","InnerConeAngle","CastShadows",
                   "bCastShadows","bUseInverseSquaredFalloff","IntensityUnits" }
local WL_NAMES = { "SpotLight","Flashlight","HeadLight","FlashLight","TorchLight","PlayerLight","Light" }
local wlObj, wlOrig, wlOn, wlLogged, wlDumpDone = nil, {}, false, false, false
function applyWideLight(F,pawn,stableFor)
  local want=(F.wide_light=="1")
  if wlObj and not valid(wlObj) then wlObj=nil wlOn=false wlLogged=false wlOrig={} end
  if not want then
    if not wlOn or not valid(wlObj) then return false end
    for p,v in pairs(wlOrig) do if v~=nil then sp(wlObj,p,v) end end       -- 用真原值还原（原来硬编码 50/150 是错的）
    W("  [手电筒] 已还原：Intensity="..ts(gp(wlObj,"Intensity")).." 半径="..ts(gp(wlObj,"AttenuationRadius")))
    wlOn=false wlLogged=false wlOrig={}
    return false
  end
  if not valid(pawn) then return false end
  if stableFor and stableFor<2 then return false end        -- 刚换图/刚生成：先别碰对象
  if not valid(wlObj) then
    for _,n in ipairs(WL_NAMES) do
      local o=gp(pawn,n)
      if valid(o) then
        wlObj=o
        W("  [手电筒] 玩家照明组件="..n.."（原值 Intensity="..ts(gp(o,"Intensity"))
          .." 半径="..ts(gp(o,"AttenuationRadius")).." 外锥="..ts(gp(o,"OuterConeAngle")).."）")
        break
      end
    end
    if not valid(wlObj) then
      if not wlDumpDone then
        wlDumpDone=true
        W("  [手电筒] 玩家身上没找到照明组件 → dump 属性名（只列名）：")
        pcall(dumpNames,pawn,"手电筒-候选",{"Light","Flash","Torch","Lamp","Shadow"},40)
      end
      return false
    end
    for _,p in ipairs(WL_PROPS) do wlOrig[p]=gp(wlObj,p) end
  end
  local o=wlObj
  sp(o,"CastShadows",false)
  sp(o,"bCastShadows",false)
  sp(o,"bUseInverseSquaredFalloff",false)                   -- 关掉平方反比衰减，Intensity 才是线性生效
  sp(o,"IntensityUnits",0)
  sp(o,"Intensity",20000)
  sp(o,"AttenuationRadius",60000)                           -- 半径拉满（基本覆盖整屏）
  sp(o,"OuterConeAngle",89)                                 -- 外锥≈90°：接近全向，等于"照着整屏"
  sp(o,"InnerConeAngle",86)
  wlOn=true
  if not wlLogged then
    wlLogged=true
    W("  [手电筒] 已应用：Intensity="..ts(gp(o,"Intensity")).." 半径="..ts(gp(o,"AttenuationRadius"))
      .." 外锥="..ts(gp(o,"OuterConeAngle")).." 内锥="..ts(gp(o,"InnerConeAngle"))
      .." 阴影="..ts(gp(o,"CastShadows")))
  end
  return true
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
-- ===== 类名缓存 + 实例缓存（v53 建立 / v54 加实例层）=====
-- 血泪教训（两次踩到）：绝不要对"可能正在被销毁"的 Pawn 实例调 GetFullName —— 12:35 与 12:44 两次崩
-- 都是 UE4SS.dll 里 0x18 偏移的 AV（FName::ToString 读已释放对象）。所以：
--   ① 类信息按"类对象地址"缓存（每个类只查一次名字）；
--   ② 实例也缓存（pawnAddr -> 是不是敌人 + 类名）：同一个怪只分类一次，之后每 tick 只做 valid()+取坐标，
--      再也不碰 GetClass/GetFullName。==> 会死人的"命名"动作只发生在"第一次看到这只活着的怪"时。
local clsCache={}
local entCache={}
local function clsInfo(c)
  if c==nil then return nil,false end
  local a=addrOf(c)
  if a==nil then return nil,false end
  local e=clsCache[a]
  if e then return e.n,e.enemy end
  local nm=nil
  local ok,v=pcall(function() return c:GetFullName() end)
  if ok and type(v)=="string" then nm=v end
  if nm==nil then return nil,false end
  local enemy=string.find(nm,"/Game/Enemies/",1,true)~=nil
  clsCache[a]={n=nm,enemy=enemy}
  return nm,enemy
end
local function isEnemy(p)
  local pa=addrOf(p)
  if pa~=nil then
    local e=entCache[pa]
    if e then return e.enemy, e.n end
  end
  local c=nil pcall(function() c=p:GetClass() end)
  local nm,en=clsInfo(c)
  if pa~=nil and nm~=nil then entCache[pa]={enemy=(en==true),n=nm} end
  return (en==true), nm
end
-- 死亡/不可攻击判定：读属性（读不到就当活着），三种字段都兼容
local function isDeadActor(t)
  local b=gp(t,"bIsDead") if type(b)=="boolean" and b then return true end
  local hc=gp(t,"Health")
  if valid(hc) then
    local b2=gp(hc,"bIsDead") if type(b2)=="boolean" and b2 then return true end
    local cur=num(gp(hc,"CurrentHealth")) if cur and cur<=0 then return true end
  end
  local b3=gp(t,"bIsAlive") if type(b3)=="boolean" and not b3 then return true end
  return false
end
local function targets(pawn)
  local ok,l=pcall(FindAllOf,"Pawn")
  if not ok or type(l)~="table" then return {} end
  local out={}
  for _,p in ipairs(l) do
    if p~=pawn and valid(p) then
      local nm=select(2,isEnemy(p))
      local c=nm or ""
      if not (string.find(c,"PlayerCharacter",1,true) or string.find(c,"NavigatorCharacter",1,true)
              or string.find(c,"BP_Player",1,true) or string.find(c,"Bosco",1,true)) then
        out[#out+1]=p
        if c~="" and not seen[c] then seen[c]=true W("  [杀戮光环] 目标类名="..c) end
      end
    end
  end
  return out
end
local lastAuraLog, lastKill = 0, 0
local function monsterWipe(F,pc,pawn,now)
  if now-(lastWipeAt or 0)<1 then return end     -- ★v96：整段限流 1 秒（原来 300ms 就枚举一遍全场）
  lastWipeAt=now
  local list=targets(pawn)
  if now-(lastAuraLog or 0)>=5 then lastAuraLog=now W("  [怪物全灭] 场上目标数="..#list) end
  if now-(lastKill or 0)>=1 then lastKill=now W("  [怪物全灭] 执行清场") pcall(cmd,"C_KillAll",pc) end
end
)FSDLUA",
R"FSDLUA(-- ================= 研发中 / ESP =================
-- ================= 研发中：自瞄（v50 改成真自瞄）/ 镐子攻速 =================
-- 老做法（v11~v49）是把一批猜出来的 "AimRange/AimAssist*" 属性乘倍数——属性探测从来没命中过（FRotator
-- 转储证实游戏里没有这些字段）。v50 改成真自瞄：直接每 30ms 把 pc 的 ControlRotation 拧到敌人身上。
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
-- v56：镐子攻速改成"改角色骨骼网格的动画速率"（=挥镐速度倍率）。
-- 依据：DRG 的挖矿/挥镐伤害是跟着动画通知走的，动画放快 = 打得快；
-- `GlobalAnimRateScale` 是 USkeletalMeshComponent 上的普通 float 属性，写它最稳（能读回验证）。
-- （老的 cm:SetQuadPickAxe / 猜属性乘倍数都试过，游戏不认，保留 quad 作为附带效果。）
local pickApplied=nil
local function applyPickAnim(pawn,mult)
  local mesh=gp(pawn,"Mesh")
  if not valid(mesh) then
    if pickApplied~=nil then pickApplied=nil end
    return
  end
  local want=mult
  if want<0.1 then want=0.1 elseif want>10 then want=10 end
  if pickApplied==want then return end
  local before=num(gp(mesh,"GlobalAnimRateScale"))
  local ok=sp(mesh,"GlobalAnimRateScale",want)
  local rb=num(gp(mesh,"GlobalAnimRateScale"))
  pickApplied=want
  W("  [镐子] 动画速率(攻速) 原="..tostring(before).." 写="..f2(want).." 读回="..tostring(rb)
    ..(rb and math.abs(rb-want)<0.01 and " ✓生效" or " ✗未生效（可能属性名不对）"))
end
local function applyAim(F,pawn,pc)
  local cm=getCM(pc)
  -- 镐子攻速：主路线 = 动画速率倍率；附带 SetQuadPickAxe（四倍镐，很多版本无效）
  local pk=tonumber(F.pickaxe_speed) or 1
  local wantQ=(pk>1)
  applyPickAnim(pawn, pk>0 and pk or 1)
  if wantQ and quadState~=1 then
    quadState=1
    -- ★v81：不再直呼 `cm:SetQuadPickAxe()`（用户点「呼叫补给仓」直呼游戏函数崩过一次：
    --   VCRUNTIME140 AV）→ 只走**控制台命令** SetQuadPickAxe；控制台这条路最安全。
    pcall(cmd,"SetQuadPickAxe",pc)
    W("  [镐子] 只发控制台 SetQuadPickAxe（不再直呼游戏函数）")
  elseif (not wantQ) and quadState==1 then
    quadState=0
    pcall(cmd,"SetQuadPickAxe 0",pc)
    W("  [镐子] 已发控制台 SetQuadPickAxe 0")
  end
  local c=mov(pawn)
  if not valid(c) then return end
  probeGroup(pawn,c,"镐子",PICK_CANDS)
  local function applyGroup(cands,val,tag)
    local v=tonumber(val) or 0
    if v<=0 then return end
    for _,p in ipairs(cands) do
      local o=nil
      if gp(c,p)~=nil then o=c elseif gp(pawn,p)~=nil then o=pawn end
      if o then
        local cur0=gp(o,p)
        if type(cur0)=="number" and cur0~=0 then
          if sp(o,p,cur0*v) then W("  ["..tag.."] "..p.." 写 "..f2(cur0*v).." 读回="..ts(gp(o,p))) end
        elseif type(cur0)=="boolean" and v>0 then
          if sp(o,p,true) then W("  ["..tag.."] "..p.." 已置 true") end
        end
      end
    end
  end
  -- ★v90：镐子攻速功能已按用户要求删除（这里不再应用；同时也不再发 SetQuadPickAxe）
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
-- v49：冲刺速度存在角色属性 RunningSpeed 上（不是 MaxWalkSpeed），所以这里只写 RunningSpeed=基准×倍率。
-- 走路（MaxWalkSpeed）完全不碰 → "冲更快、走不变"；改数值后 60ms 内生效，不用重新按 Shift。
LoopAsync(60, function()
  if _G.__FSDT_GEN~=MYGEN then return true end
  local ok,e=pcall(function()
    local F=readF()
    local pc,cm,pawn=ctx()
    if not (valid(pc) and valid(pawn)) then return end
    pcall(applyRun,F,pawn)
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
)FSDLUA",
R"FSDLUA(-- ===== v55：仿照 DX11 修改器的 ESP/HUD 加料 =====
-- ① 敌人/队友带 生命/护甲（属性读带 0.5s 缓存，避免每帧狂读）
-- ② 队友条目（tag "T"）：距离/生命/护甲/倒地
-- ③ 雷达：R 行 = <相对视角的方位角> <距离米> <类型>
-- ④ 资源统计：S 行（先做探测，属性名不确定——日志里会打印探测结果）
local hpCache={}
local function hpOf(a)
  if not valid(a) then return -1,-1 end
  local pa=addrOf(a)
  if pa==nil then return -1,-1 end
  local c=hpCache[pa]
  local now=os.clock()
  if c and (now-c.t)<0.5 then return c.hp,c.sh end
  local hp,sh=-1,-1
  local hc=gp(a,"Health")
  if valid(hc) then
    local v=num(gp(hc,"CurrentHealth")); if v then hp=v end
  end
  local sc=gp(a,"Shield") or gp(a,"ShieldComponent")
  if valid(sc) then
    local v=num(gp(sc,"CurrentShield")); if v then sh=v end
  end
  hpCache[pa]={hp=hp,sh=sh,t=now}
  return hp,sh
end
local resProbed=false
local function resProbe()
  if resProbed then return end
  resProbed=true
  local cands={}
  local pc,cm,pawn=ctx()
  local ps=valid(pawn) and gp(pawn,"PlayerState") or nil
  local gs=nil
  if valid(pawn) then local w=gp(pawn,"World") end
  for _,src in ipairs({{"PlayerState",ps}}) do
    if valid(src[2]) then
      local c=nil pcall(function() c=src[2]:GetClass() end)
      if valid(c) then
        pcall(function()
          c:ForEachProperty(function(p)
            local ok,n=pcall(function() return p:GetFName():ToString() end)
            if ok and n and (string.find(n,"Res",1,true) or string.find(n,"Mineral",1,true) or string.find(n,"Resource",1,true)) then
              cands[#cands+1]=src[1].."."..n
            end
          end)
        end)
      end
    end
  end
  W("  [资源探测] PlayerState 里像资源的属性: "..(#cands>0 and table.concat(cands,", ") or "没找到"))
end
local function espRescan2(dE,dM,dT)
  local list=espRescan(dE,dM)
  if dT>0 then
    local ok,l=pcall(FindAllOf,"Pawn")
    if ok and type(l)=="table" then
      local pc,cm,myPawn=ctx()
      for _,p in ipairs(l) do
        if valid(p) and p~=myPawn then
          local nm=select(2,isEnemy(p))
          if nm and (string.find(nm,"PlayerCharacter",1,true) or string.find(nm,"BP_Player",1,true)) then
            list[#list+1]={a=p,tag="T",lim=dT,hz=90,team=true,name=(string.match(nm,"([%w_]+)_C$") or "队友")}
          end
        end
      end
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
    local dT=tonumber(F.esp_team) or 0
    local wantHR=(tonumber(F.esp_hp) or 1)>0          -- 生命/护甲条
    local wantRadar=(tonumber(F.esp_radar) or 0)>0
    if dE<=0 and dM<=0 and dT<=0 and not wantRadar then
      if #espTargets>0 then espTargets={} wf(G..ESP_FILE,"# ESP 关闭\n") end
      return
    end
    if dE>120 then dE=120 end
    if dM>120 then dM=120 end
    if dT>120 then dT=120 end
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
      espTargets=espRescan2(dE,dM,dT)
      W("  [ESP] 目标重扫: "..#espTargets.." 个（敌人 "..dE.."m / 矿物 "..dM.."m / 队友 "..dT.."m）")
    end
    local out,n={},0
    local radar={}
    for _,t in ipairs(espTargets) do
      if valid(t.a) then
        local x,y,z=locOf(t.a)
        if x then
          local d=math.sqrt((x-px)^2+(y-py)^2+(z-pz)^2)/100.0
          -- 雷达：相对玩家视角的方位（0=正前方，顺时针为正）+ 距离
          if wantRadar and d<=100 then
            local ang=math.deg(aimAtan2(y-py,x-px))-view.yaw
            while ang>180 do ang=ang-360 end
            while ang<-180 do ang=ang+360 end
            radar[#radar+1]=string.format("R %.0f %.0f %s\n",ang,d,(t.tag=="M" and "M" or (t.tag=="T" and "T" or "E")))
          end
          if d<=t.lim then
            local x1,y1,x2,y2=espBox(view,pawn,x,y,z,t.hz)
            if x1 and x2>0 and y2>0 and x1<vpW and y1<vpH then
              local hps=""
              if wantHR then
                local hp,sh=hpOf(t.a)
                hps=string.format(" %.0f %.0f",hp,sh)
              else
                hps=" -1 -1"
              end
              if t.tag=="T" then
                local hp,sh=hpOf(t.a)
                local down=(hp>=0 and hp<50) and 1 or 0
                out[#out+1]=string.format("T %.0f %.0f %.0f %.0f %.0f%.0f %d %s\n",x1,y1,x2,y2,d,hps,down,t.name or "队友")
              else
                out[#out+1]=string.format("%s %.0f %.0f %.0f %.0f %.0f%.0f %s\n",t.tag,x1,y1,x2,y2,d,hps,t.name or "?")
              end
              n=n+1
            end
          end
        end
      end
    end
    if wantRadar then resProbe() end
    espSelfRound=espSelfRound+1
    if espSelfRound%20==1 then
      local saw,say=projWith(view,pawn,px+view.F[1]*1000,py+view.F[2]*1000,pz+view.F[3]*1000+60)
      W("  [ESP自检] 方式="..tostring(projMode).." 视口="..vpW.."x"..vpH.." 目标="..#espTargets
        .." 写出="..n.." 正前方10米="..(saw and (math.floor(saw)..","..math.floor(say)) or "失败")
        .."（期望 ≈ "..math.floor(vpW/2)..","..math.floor(vpH/2).."）")
    end
    wf(G..ESP_FILE,"# Carlhand ESP "..os.date("%H:%M:%S").." 目标"..#espTargets.."\n"
      ..table.concat(out)..table.concat(radar))
  end)
  if not ok then W("  !!! ESP 循环异常(已继续): "..tostring(e)) end
  return false
end)
-- ================= v78：血盾上限 / 弹药倍率 / 挖掘速度 =================
)FSDLUA",
R"FSDLUA(-- 统一套路：候选属性名 → 写值 → 读回 → 写日志（找不到就在日志里写清楚，我们照日志迭代名字）。
-- 只在"功能表数值变化时"执行一次（不每 tick 读，省性能）。0 = 不改。
-- 参考用户给的 mod（All-in-One / Adjustable Ammo / Kid Assist / Better Digging）：
--   这些 mod 本质就是改这些数值字段；我们用"读字段→写值"的同一套路实现。
local jwLog={}
local function jw(obj,name,val,tag)
  if not valid(obj) then return false end
  local cur=gp(obj,name)
  if type(cur)~="number" then return false end
  if math.abs(cur-val)>0.01 then
    local ok=sp(obj,name,val)
    local k=tag.."."..name
    jwLog[k]=(jwLog[k] or 0)+1
    if jwLog[k]<=3 then
      W("  ["..tag.."] "..name.."："..ts(cur).." → "..ts(val).."（写="..tostring(ok).." 读回="..ts(gp(obj,name)).."）")
    elseif jwLog[k]==4 then
      W("  ["..tag.."] "..name.." 一直被游戏改回 → 继续持续重写（不再刷日志）")
    end
  end
  return true
end
local function jwFirst(obj,names,val,tag)
  for _,n in ipairs(names) do if jw(obj,n,val,tag) then return true end end
  return false
end
-- 一次性把某个对象上"像弹药/挖掘"的属性名列出来（名字即可，不读值），方便迭代
jbDumpFlags = jbDumpFlags or {}
local function dumpNames(o,tag,keys,cap)
  local out={}
  if valid(o) then
    local cls=nil pcall(function() cls=o:GetClass() end)
    local lvl=0
    while valid(cls) and lvl<7 do
      local c0=cls
      pcall(function() c0:ForEachProperty(function(p)
        local n=nil pcall(function() n=p:GetFName():ToString() end)
        if n then
          for _,k in ipairs(keys) do
            if string.find(n,k,1,true) then out[#out+1]=n break end
          end
        end
      end) end)
      local sup=nil pcall(function() sup=c0:GetSuperStruct() end)
      if not valid(sup) then break end
      cls=sup lvl=lvl+1
    end
  end
  local seen,final={},{}
  for _,n in ipairs(out) do if not seen[n] then seen[n]=true final[#final+1]=n end end
  local head={}
  for i=1,math.min(#final,cap or 60) do head[i]=final[i] end
  W("  [探测]["..tag.."] 命中 "..#final.." 条: "..(#head>0 and table.concat(head,", ") or "无（名字差别大，需要再换关键字）"))
end
-- 血量上限（补满当前值）
function applyHpMax(pawn,v)
  local hp=gp(pawn,"Health")
  if not valid(hp) then
    for _,n in ipairs({"HealthComponent","PlayerHealth","HealthComponentBase","FSDHealthComponent"}) do
      local x=gp(pawn,n)
      if valid(x) then
        hp=x
        if not jbDumpFlags.hpUsed then jbDumpFlags.hpUsed=true W("  [血量上限] 血组件= pawn."..n) end
        break
      end
    end
  end
  if not valid(hp) then
    if not jbDumpFlags.hp then
      jbDumpFlags.hp=true
      W("  [血量上限] 没拿到血组件（试过 Health/HealthComponent/PlayerHealth/HealthComponentBase/FSDHealthComponent）")
      dumpNames(pawn,"pawn 属性名(血/盾相关)",{"Health","Shield","Armor","Max"},60)
    end
    return
  end
  if not jwFirst(hp,{"MaxHealth","MaxHealthBase","HealthMax"},v,"血量上限") then
    W("  [血量上限] Health 组件上没有 MaxHealth/MaxHealthBase/HealthMax")
    dumpNames(hp,"Health",{"Health","Max"},40)
  end
  for _,n in ipairs({"CurrentHealth","Health","HealthValue"}) do
    local cur=gp(hp,n)
    if type(cur)=="number" then
      W("  [血量上限] 当前血("..n..")="..ts(cur).." → 补满="..tostring(sp(hp,n,v)))
      break
    end
  end
end
-- 护盾上限（补满当前值）
function applyShieldMax(pawn,v)
  -- v79：实测玩家角色身上没有 Shield/ShieldComponent（只有 ActiveShieldGenerators 之类），
  --   护盾在 **HealthComponent** 上（和 MaxHealth 同一个组件）→ 优先用它。
  local sh=gp(pawn,"Shield") or gp(pawn,"ShieldComponent")
  if not valid(sh) then sh=gp(pawn,"HealthComponent") end
  if not valid(sh) then
    for _,n in ipairs({"PlayerShield","ArmorComponent","ShieldComponentBase","FSDShieldComponent"}) do
      local x=gp(pawn,n) if valid(x) then sh=x W("  [护盾上限] 改用 pawn."..n.." 作为盾组件") break end
    end
  end
  if not valid(sh) then
    if not jbDumpFlags.sh then
      jbDumpFlags.sh=true
      W("  [护盾上限] 没拿到盾组件（试过 Shield/ShieldComponent/HealthComponent/PlayerShield/ArmorComponent）")
      dumpNames(pawn,"pawn 属性名(血/盾相关)",{"Health","Shield","Armor","Max"},60)
    end
    return
  end
  if not jwFirst(sh,{"MaxArmor","MaxShield","ShieldMax","MaxShieldBase"},v,"护盾上限") then
    W("  [护盾上限] 盾组件上没有 MaxArmor/MaxShield/ShieldMax/MaxShieldBase")
    dumpNames(sh,"Shield",{"Shield","Armor","Max","Regen"},40)
  end
  for _,n in ipairs({"Armor","CurrentArmor","CurrentShield","Shield","ArmorValue"}) do
    local cur=gp(sh,n)
    if type(cur)=="number" then
      W("  [护盾上限] 当前盾("..n..")="..ts(cur).." → 补满="..tostring(sp(sh,n,v)))
      break
    end
  end
end
-- 弹药（v92：**只改"手上这把"**，用户要求"实时读取数据然后修改，其他武器不受影响"）：
--   ① 先在玩家身上找"当前武器/物品"指针（CurrentWeapon / ActiveItem / …），这是最准的；
--   ② 再找背包/装备组件里的当前项；
--   ③ 退回：从类名扫出来的对象里挑带"装备中"标志的（bEquipped / bIsActive / …）；
--   ④ 一个都没有就兜底（只有唯一对象时才用），其余情况宁可不写，也绝不乱改别的枪。
--   读回实测值写进日志（例：ClipSize=20, AmmoCount=20），纯属性写入，不调任何游戏函数。
local WPN_CLASSES = { "WPN_AssaultRifle_C","WPN_Autocannon_C","WPN_BurstPistol_C","WPN_ChargeBlaster_C","WPN_CoilGun_C",
  "WPN_CombatShotgun_C","WPN_Crossbow_C","WPN_Cryospray_C","WPN_DoubleDrills_C","WPN_DualMPs_C","WPN_FlameThrower_C",
  "WPN_FlareGun_C","WPN_Gatling_C","WPN_GooCannon_C","WPN_GrapplingGun_C","WPN_GrenadeLauncher_C","WPN_HeavyParticleCannon_C",
  "WPN_LaserPistol_C","WPN_LaserPointer_C","WPN_LineCutter_C","WPN_LockOnRifle_C","WPN_M1000_C","WPN_MicroMissileLauncher_C",
  "WPN_MicrowaveGun_C","WPN_Pickaxe_C","WPN_Pickaxe_Gunner_C","WPN_PlatformGun_C","WPN_Revolver_C","WPN_SMG_C",
  "WPN_Shotgun_C","WPN_Sniper_C","WPN_Subata_C","WPN_Thunderhead_C","WPN_Zhukov_C",
  "Throwable_C","Grenade_C","Flare_C","BP_FlareJugge_S05_C","BP_Throwable_C","BP_Grenade_Axe_Stuck_C","BP_Grenade_IFG_Area_Base_C" }
local function findItems()
  local objs={}
  for _,cn in ipairs(WPN_CLASSES) do
    local n=-1
    pcall(function() local l=FindAllOf(cn) if type(l)=="table" then n=#l end end)
    if n>0 then
      local l=nil pcall(function() l=FindAllOf(cn) end)
      if type(l)=="table" then
        for _,o in ipairs(l) do if valid(o) then objs[#objs+1]={o=o,cn=cn} end end
      end
    end
  end
  return objs
end
local CLIP_KEYS = { "ClipSize","MaxClipSize","MagazineSize","MaxMagazineSize" }
local CUR_KEYS  = { "AmmoCount","ClipCount","Ammo","CurrentAmmo","ClipAmmo" }
local RES_KEYS  = { "ReserveAmmo","MaxReserveAmmo","CarriedAmmo","MaxCarriedAmmo","BackupAmmo","MaxBackupAmmo","TotalAmmo" }
local HAND_PTR_CANDS = { "CurrentWeapon","ActiveWeapon","EquippedWeapon","HeldWeapon","Weapon",
  "CurrentItem","ActiveItem","EquippedItem","HeldItem","CurrentEquipment","ActiveEquipment",
  "CurrentThrowable","ActiveThrowable","CurrentGrenade","ActiveGrenade" }
local HAND_INV_CANDS = { "Inventory","InventoryComponent","ItemInventory","Equipment","Loadout","LoadoutComponent" }
local HAND_FLAG_CANDS = { "bEquipped","Equipped","bIsEquipped","IsEquipped","bIsActive","IsActive","bActive","Active",
  "bInHand","bInHands","bHeld","bIsHeld","Visible","bVisible" }
local handTick, handMissLogged = 0, false
-- 返回：手上武器对象, 说明（怎么找到的）
function handWeapon(pawn)
  if valid(pawn) then
    for _,n in ipairs(HAND_PTR_CANDS) do
      local o=gp(pawn,n)
      if valid(o) then return o,"pawn."..n end
    end
    for _,n in ipairs(HAND_INV_CANDS) do
      local inv=gp(pawn,n)
      if valid(inv) then
        for _,m in ipairs(HAND_PTR_CANDS) do
          local o=gp(inv,m)
          if valid(o) then return o,n.."."..m end
        end
      end
    end
  end
  handTick=handTick+1
  if handTick%20~=1 then return nil end       -- ★v96：兜底扫描很贵（76 个类名 FindAllOf）→ 6 秒最多一次
  local objs=findItems()
  for _,e in ipairs(objs) do
    for _,k in ipairs(HAND_FLAG_CANDS) do
      if gp(e.o,k)==true then return e.o,e.cn.."."..k end
    end
  end
  if #objs==1 then return objs[1].o,objs[1].cn.."(唯一)" end
  if not handDumpDone then
    handDumpDone=true
    W("  [弹药] 没认出【手上这把】（找到 "..#objs.." 个武器对象）→ dump 玩家属性名：")
    pcall(dumpNames,pawn,"玩家",{"Weapon","Item","Equip","Held","Hand","Active","Inventory","Throwable","Grenade"},80)
  end
  return nil
end
-- 每帧调用（300ms）：容量键持续校正；【当前弹药】只在数值变化 / 换枪那一次补满
local function ammoApply(pawn, clip, reserve)
  local o,how=handWeapon(pawn)
  if not valid(o) then
    if not handMissLogged then
      handMissLogged=true
      W("  [弹药] 还没拿到【手上这把】武器（进任务 / 举起武器后自动生效）")
    end
    return false
  end
  local hy=tostring(addrOf(o))
  local tag=tostring(clip).."/"..tostring(reserve)
  local fill=(last.ammo_tag~=tag or last.ammo_hand~=hy)
  local hits={}
  if clip>0 then
    for _,k in ipairs(CLIP_KEYS) do
      local cv=gp(o,k)
      if type(cv)=="number" and (fill or math.abs(cv-clip)>0.5) then
        sp(o,k,clip) hits[#hits+1]=k.."="..ts(gp(o,k))
      end
    end
    if fill then
      for _,k in ipairs(CUR_KEYS) do
        local cv=gp(o,k)
        if type(cv)=="number" then sp(o,k,clip) hits[#hits+1]=k.."="..ts(gp(o,k)) end
      end
    end
  end
  if reserve>0 then
    for _,k in ipairs(RES_KEYS) do
      local cv=gp(o,k)
      if type(cv)=="number" and (fill or math.abs(cv-reserve)>0.5) then
        sp(o,k,reserve) hits[#hits+1]=k.."="..ts(gp(o,k))
      end
    end
  end
  if #hits==0 then
    if fill and not handDumpDone then
      handDumpDone=true
      W("  [弹药] 手上="..how.." 没匹配到弹药字段；dump 属性名（只列名）：")
      pcall(dumpNames,o,"手上武器 "..how,{"Ammo","Clip","Magazine","Reserve","Carried","Count","Amount","Max"},60)
    end
    return false
  end
  last.ammo_tag=tag last.ammo_hand=hy handMissLogged=false
  W("  [弹药] 手上="..how.."："..table.concat(hits,", "))
  return true
end
-- 挖掘速度：v85 用**实测找到的真字段**（镐子对象 WPN_Pickaxe_Gunner_C）：
--   · `QuadDamageCarving`：游戏自带"四倍挖掘"那个开关/倍率（bool 或 数值）
--   · `MiningRange` / `DamageRange` / `DamageRadius`：一次挥镐能挖到的范围 → 乘倍率
--   （`Mesh.GlobalAnimRateScale` 只影响动画，留着当附赠；镐子对象用 FindAllOf("PickaxeItem") 拿）
function applyDig(pawn,v)
  local pk=nil
  pcall(function() local l=FindAllOf("PickaxeItem") if type(l)=="table" and l[1] and valid(l[1]) then pk=l[1] end end)
  if not valid(pk) then pcall(function() local l=FindAllOf("Item") if type(l)=="table" and l[1] and valid(l[1]) then pk=l[1] end end) end
  if valid(pk) then
    local q=gp(pk,"QuadDamageCarving")
    if type(q)=="boolean" then
      if not q then sp(pk,"QuadDamageCarving",true) W("  [挖掘速度] QuadDamageCarving=false → true") end
    elseif type(q)=="number" then
      jwFirst(pk,{"QuadDamageCarving"},v,"挖掘速度(四倍挖掘)")
    end
    for _,n in ipairs({"MiningRange","DamageRadius","DamageRange"}) do
      local cur=gp(pk,n)
      if type(cur)=="number" and cur>0 then
        local base=jbDigBase[n]
        if not base then jbDigBase[n]=cur base=cur end
        local nv=base*v
        if math.abs(cur-nv)>0.01 then
          sp(pk,n,nv)
          W("  [挖掘速度] "..n.."："..ts(cur).." → "..ts(nv).."（基准 "..ts(base).." ×"..tostring(v).." 读回="..ts(gp(pk,n)).."）")
        end
      end
    end
    if not jbDumpFlags.dmg then
      jbDumpFlags.dmg=true
      local dc0=gp(pk,"DamageComponent")
      if valid(dc0) then
        W("  [挖掘速度] DamageComponent="..clsOf(dc0))
        dumpNames(dc0,"镐子 DamageComponent",{"Damage","Carve","Radius","Range","Amount","Multiplier","Rate","Size","Volume","Strength"},60)
      end
    end
    -- v85b：真正"挖得快"的关键在伤害组件上（实测字段：Damage / RadialDamage / DamageRadius / MaxDamageRadius）
    local dc=gp(pk,"DamageComponent")
    if valid(dc) then
      for _,n in ipairs({"Damage","RadialDamage","DamageRadius","MaxDamageRadius"}) do
        local cur=gp(dc,n)
        if type(cur)=="number" and cur>0 then
          local base=jbdmgBase[n]
          if not base then jbdmgBase[n]=cur base=cur end
          local nv=base*v
          if math.abs(cur-nv)>0.01 then
            sp(dc,n,nv)
            W("  [挖掘速度] 伤害组件."..n.."："..ts(cur).." → "..ts(nv).."（基准 "..ts(base).." ×"..tostring(v).." 读回="..ts(gp(dc,n)).."）")
          end
        end
      end
    end
  else
    W("  [挖掘速度] 没找到镐子对象（FindAllOf PickaxeItem/Item）")
  end
  -- ★v89（**又一次崩溃，这条路彻底删掉**）：用户 18:55 在任务里点「挖掘速度=3」→
  --   crash_2026_09_19_10_55_00.dmp，异常在 **UE4SS.dll+0x3A6A11（AV reading 0x70）**，
  --   即崩在 UE4SS 里，元凶就是"对 MaterialInstanceDynamic 调 SetScalarParameterValue"这类**游戏函数调用**。
  --   参考 mod（Better Digging）能做是因为它在 UE 内部走 BP；我们经 UE4SS 调会被打崩。
  --   ⇒ **绝不再从 Lua 调材质函数**；「挖掘速度」现在只做纯属性写入（镐子 + DamageComponent，实测不崩）。
  jwFirst(gp(pawn,"Mesh"),{"GlobalAnimRateScale"},v,"挖掘速度(挥镐动画速率)")
end
jbDigBase = jbDigBase or {}
jbdmgBase = jbdmgBase or {}
-- ================= v82：找"真正生效的位置"的探测（一次性）=================
)FSDLUA",
R"FSDLUA(-- 已确认的问题：血/盾上限写进 HealthComponent 会被游戏每帧改回（HUD 还是 125/30）；
--   挖掘速度写动画速率/发 SetQuadPickAxe 都不变。⇒ 真正生效的字段在别处，先把它们"照出来"。
function clsOf(o)
  if not valid(o) then return "?" end
  local s=nil pcall(function() s=o:GetClass():GetFName():ToString() end)
  return s or "?"
end
function probeExtras(pawn)
  W("  [探测][v2] ==== 开始（角色="..clsOf(pawn).."）====")
  -- ① 角色上"升级/护甲/加成"相关的属性名
  dumpNames(pawn,"pawn(升级/护甲/加成相关)",{"Upgrade","Armor","Bonus","Modifier","Perk","Attribute","Data","Save","Loadout","Max"},90)
  -- ② 血组件上更宽的关键字（找"当前值/加成"字段）
  local hp=gp(pawn,"HealthComponent")
  if valid(hp) then dumpNames(hp,"HealthComponent 宽关键字",{"Health","Armor","Shield","Current","Value","Max","Bonus","Modif","Percent","Regen","Damage","Rate"},90) end
  -- ③ 镐子对象（实测 FindAllOf("PickaxeItem")/("Item") 能拿到：WPN_Pickaxe_Gunner_C）
  local pk=nil
  pcall(function() local l=FindAllOf("PickaxeItem") if type(l)=="table" and l[1] and valid(l[1]) then pk=l[1] end end)
  if not valid(pk) then pcall(function() local l=FindAllOf("Item") if type(l)=="table" and l[1] and valid(l[1]) then pk=l[1] end end) end
  if valid(pk) then
    W("  [探测] 镐子对象 = "..clsOf(pk).." @"..ts(addrOf(pk)))
    dumpNames(pk,"镐子属性",{"Dig","Pick","Damage","Radius","Strength","Speed","Carve","Mine","Rate","Efficiency","Range","Fuel","Charge"},90)
  else
    W("  [探测] 没找到镐子对象")
  end
  -- ④ 武器/物品基类名再多试几个（弹匣容量要用）
  for _,cn in ipairs({"FSDWeapon","AWeapon","Weapon","Gun","AmmoDrivenItem","ItemAmmo","ItemWeapon"}) do
    local n=-1
    pcall(function() local l=FindAllOf(cn) if type(l)=="table" then n=#l end end)
    local head=""
    if n>0 then local l=nil pcall(function() l=FindAllOf(cn) end) if type(l)=="table" and l[1] and valid(l[1]) then head=" 例="..clsOf(l[1]) end end
    W("  [探测][FindAllOf] "..cn.." = "..tostring(n)..head)
  end
  W("  [探测][v2] ==== 结束 ====")
end
-- ================= 无敌（v93 新增；纯属性写入，不调任何游戏函数）=================
-- 崩溃教训：老「无敌+复活」的 key 其实是 auto_revive（菜单名与字段错位），一点就每 2 秒发 C_Revive
-- → 21:12:29 崩掉：EXCEPTION_ACCESS_VIOLATION reading 0xffffffffffffffff，PCallStack 全在
--   FSD-Win64-Shipping 里（没有 UE4SS 帧）→ 游戏自己的复活作弊路径是坏的，**绝不再调**。
-- 现在：把"能不能受伤"的开关属性关掉。字段名按候选表射一遍（写成功会立刻读回并打日志），
-- 一个都匹不上就把 pawn / Health / DamageComp 的属性名 dump 一次（只列名），方便下轮锁定真字段。
-- ★真字段（从 dump 实测挖出来，第 17733 行那条 HealthComponent 宽关键字探测）：
--   `canTakeDamage`（小写 c！）+ 事件 `OnCanTakeDamageChanged`。UE4SS 的 SetPropertyValue 大小写敏感，
--   所以候选表里必须**原样**带上小写那个。
local GOD_CAN_PROPS = { "canTakeDamage","CanTakeDamage","bCanTakeDamage","bCanBeDamaged","CanBeDamaged",
                        "bCanReceiveDamage","bDamageable" }
local GOD_IMM_PROPS = { "bInvincible","bIsInvincible","bGodMode","bDamageImmune","DamageImmune",
                        "bImmuneToDamage","bImmune","bTakeNoDamage","bNoDamage","bIgnoreDamage" }
local godOrig, godLogged, godDumpDone = nil, nil, false
-- 取 pawn / Health / DamageComponent 三个写入目标
local function godTargets(pawn)
  local t={}
  if valid(pawn) then
    t[#t+1]={o=pawn,n="pawn"}
    local hc=gp(pawn,"Health")
    if not valid(hc) then hc=gp(pawn,"HealthComponent") end
    if valid(hc) then t[#t+1]={o=hc,n="Health"} end   -- ★v96：不再每帧 clsOf()（FName::ToString 是崩因之一）
    local dm=gp(pawn,"DamageComponent")
    if valid(dm) then t[#t+1]={o=dm,n="DamageComponent"} end
  end
  return t
end
function applyGodMode(pawn, on)
  local targets=godTargets(pawn)
  if not on then
    godLogged=nil
    if not godOrig then return false end
    -- ★v95 修：原来按"目标名"存原值，换图/重生之后 rec.o 已经失效 → 新的血组件永远收不到还原，
    --   而且下次开启时 rec.v 已有值、不再重新采集 ⇒ **一旦还原失败就永久无敌**（用户就是这个现象）。
    --   现在：① 记录按"对象地址"分开存，每个实例各自采集；② 关闭时对**当前身上**的对象**强制**写回
    --   「可受伤」（can 类 → true，免疫类 → false），并读回打日志，保证一定还原得掉。
    local n,forced,fb={}, {}, {}
    for _,rec in pairs(godOrig) do
      if valid(rec.o) then
        for p,v in pairs(rec.v) do if v~=nil then sp(rec.o,p,v) end end
        n[#n+1]=tostring(addrOf(rec.o))
      end
    end
    for _,t in ipairs(targets) do
      local hits={}
      for _,p in ipairs(GOD_CAN_PROPS) do
        if type(gp(t.o,p))=="boolean" then
          sp(t.o,p,true)
          if gp(t.o,p)==true then hits[#hits+1]=p.."=true(读回 "..ts(gp(t.o,p))..")" end
        end
      end
      for _,p in ipairs(GOD_IMM_PROPS) do
        if type(gp(t.o,p))=="boolean" then
          sp(t.o,p,false)
          if gp(t.o,p)==false then hits[#hits+1]=p.."=false" end
        end
      end
      if #hits>0 then forced[#forced+1]=t.n.."."..table.concat(hits,",") fb[#fb+1]=t.n end
    end
    W("  [无敌] 已关闭：旧记录还原 "..#n.." 个对象；当前对象强制还原 "..#forced.." 处 → "
      ..table.concat(forced, " | "))
    godOrig=nil
    return false
  end
  if not godOrig then godOrig={} end
  local hits={}
  for _,t in ipairs(targets) do
    local key=t.n.."#"..tostring(addrOf(t.o))          -- ★v95：按对象实例分开存
    local rec=godOrig[key]
    if not rec then rec={o=t.o,v={}} godOrig[key]=rec end
    for _,p in ipairs(GOD_CAN_PROPS) do
      local cv=gp(t.o,p)
      if type(cv)=="boolean" then
        if rec.v[p]==nil then rec.v[p]=cv end
        if cv then sp(t.o,p,false) if gp(t.o,p)==false then hits[#hits+1]=t.n.."."..p.."=false" end end
      end
    end
    for _,p in ipairs(GOD_IMM_PROPS) do
      local cv=gp(t.o,p)
      if type(cv)=="boolean" then
        if rec.v[p]==nil then rec.v[p]=cv end
        if not cv then sp(t.o,p,true) if gp(t.o,p)==true then hits[#hits+1]=t.n.."."..p.."=true" end end
      end
    end
  end
  if #hits>0 then
    if godLogged~=#hits then godLogged=#hits W("  [无敌] 已写："..table.concat(hits,", ")) end
    return true
  end
  if not godDumpDone then
    godDumpDone=true
    W("  [无敌] 没匹配到【能否受伤】字段 → dump 属性名（只列名）：")
    for _,t in ipairs(targets) do pcall(dumpNames,t.o,t.n,{"Damage","Invinc","Immune","God","Health","CanBe","Dead"},60) end
  end
  return false
end
-- ================= 自起（v94 新增；和「无敌」同一个开关）=================
-- 自起 = 死亡时自动复活。**绝不再用 C_Revive**：证据是两次崩溃现场 dump 的最后一行
--   都是 `命令[C_Revive] ok=true`（19:09 起每 2 秒刷屏，19:10:01 崩一次；21:12:29 又崩一次）
--   → 游戏自带的复活作弊路径是坏的，什么时候崩只是时间问题，跟"是否真死"无关。
-- 现在的做法：**纯属性写入**——检测到"死了/倒地了"就把血量写回去、把死亡/倒地标志清掉。
--   死亡状态下第一次运行会把 pawn / Health / PlayerState 的"死亡相关"属性名 dump 一次（只列名），
--   方便下一轮直接锁定真字段。
local RSRV_DEAD_PROPS = { "bIsDead","IsDead","bDead","bIsDowned","IsDowned","Downed","bDowned" }
local RSRV_ALIVE_PROPS = { "bIsAlive","IsAlive","bAllowRevive","bIsRevivable","bCanBeRevived" }
local RSRV_HP_PROPS = { "Health","CurrentHealth","HealthValue","CurrentHealthValue" }
local rsrvLogged, rsrvDumpDone = nil, false
function selfReviveDead(pawn)
  local n,hp = 0, nil
  local function chk(o)
    if not valid(o) then return end
    for _,p in ipairs(RSRV_DEAD_PROPS) do
      if gp(o,p)==true then n=n+1 end
    end
    if hp==nil then
      for _,p in ipairs(RSRV_HP_PROPS) do
        local v=num(gp(o,p))
        if v then hp=v break end
      end
    end
  end
  chk(pawn)
  local hc=gp(pawn,"Health") if not valid(hc) then hc=gp(pawn,"HealthComponent") end
  chk(hc)
  if n>0 then return true, hc end
  if hp~=nil and hp<=0.01 then return true, hc end
  return false, hc
end
function applySelfRevive(pawn)
  local dead,hc = selfReviveDead(pawn)
  if not dead then rsrvLogged=nil return false end
  local hits={}
  local function fix(o,tag)
    if not valid(o) then return end
    local mc=nil
    for _,p in ipairs({"MaxHealth","MaxArmor"}) do
      local v=num(gp(o,p)) if v and v>0 then mc=v break end
    end
    for _,p in ipairs(RSRV_HP_PROPS) do
      local v=num(gp(o,p))
      if v and v<=0.01 and mc then
        sp(o,p,mc)
        if (num(gp(o,p)) or 0)>0 then hits[#hits+1]=tag.."."..p.."="..ts(gp(o,p)) end
      end
    end
    for _,p in ipairs(RSRV_DEAD_PROPS) do
      if gp(o,p)==true then
        sp(o,p,false)
        if gp(o,p)==false then hits[#hits+1]=tag.."."..p.."=false" end
      end
    end
    for _,p in ipairs(RSRV_ALIVE_PROPS) do
      if gp(o,p)==false then
        sp(o,p,true)
        if gp(o,p)==true then hits[#hits+1]=tag.."."..p.."=true" end
      end
    end
  end
  fix(pawn,"pawn")
  fix(hc,"Health")
  local ps=gp(pawn,"PlayerState")
  fix(ps,"PlayerState")
  if #hits>0 then
    if rsrvLogged~=#hits then rsrvLogged=#hits W("  [自起] 检测到死亡 → 已自救："..table.concat(hits,", ")) end
    return true
  end
  if not rsrvDumpDone then
    rsrvDumpDone=true
    W("  [自起] 死了但没匹配到可写的死亡/复活字段 → dump 属性名（只列名）：")
    pcall(dumpNames,pawn,"自起-pawn",{"Dead","Down","Revive","Alive","Health","Respawn"},60)
    if valid(hc) then pcall(dumpNames,hc,"自起-Health",{"Dead","Down","Revive","Alive","Health"},60) end
    if valid(ps) then pcall(dumpNames,ps,"自起-PlayerState",{"Dead","Down","Revive","Alive","Respawn"},60) end
  end
  return false
end
-- ================= 主循环 =================
)FSDLUA",
R"FSDLUA(W("\n>> 功能引擎 v96 已载入 "..os.date("%H:%M:%S").." gen="..MYGEN)
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
      wlObj=nil wlOn=false wlLogged=false wlOrig={}
      ccHits={} flyState=0 flyOrig=nil flyLogged=false
      espTargets={} espEnumAt=0 espWorldAt=now      -- ESP 目标列表作废，重新计时
      oreList={} oreProbeDone=false                 -- 矿脉要重新探测
      fastState=0 quadState=0                       -- 重生后游戏自带的开关状态未知，重新确认一次
      lastWrote=nil walkBase=nil sprintBase=nil basesCaptured=false
      runBase=nil runLogged=nil                     -- 新角色的 RunningSpeed 基准要重新读
      pickApplied=nil                               -- 新角色/新网格：镐子动画速率要重新写一次
      -- jbPending 不清：换图/重生后仍要把用户点过的那次补上
      graceUntil=now+5 worldSince=now
      -- ★v96：这里原来打 nm(pawn)=GetFullName()，而"新角色刚出现"正是旧对象在销毁的时刻，
      --   正是 UE4SS+0x52DBB9 读 0x18 那个崩因 → 改成只打地址，不再取名字。
      W("  [换图/重生] 检测到新的角色对象（addr="..ts(pa).."），已重置缓存并静默 5 秒")
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
    -- ★v95：霸体（cc_immune）功能已按用户要求删除 → 不再调用 applyCC
    pcall(applyWideLight,F,pawn,now-worldSince)
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
      -- ★v93：血/盾上限、设置血量三个功能已按用户要求**删除**（菜单里不再有）→ 这里全部不再驱动
      -- v90/v91：精确弹药（弹匣容量 / 备弹容量，写绝对值，不做倍率）
      -- 坑：在空间站/菜单里设值的时候武器还没 spawn → 那次写失败；旧逻辑写失败也算"已处理"，
      --     进了任务就不再重试 ⇒ 用户会觉得"不可控"。现在改成：**写失败就下一帧继续重试**，
      --     成功之后只在数值变化时再写（避免变成每 300ms 补满的无限弹药）。
      -- ★v92：改成**每帧都调**（300ms），由 applyAmmo 自己判断要不要写：
      --     容量键持续校正；当前弹药只在"改数值 / 换枪"那一次补满 → 实时、且只影响手上这把。
      local clip=tonumber(F.clip_size) or 0
      local resv=tonumber(F.reserve_ammo) or 0
      if clip>0 or resv>0 then
        last.ammo_off=nil
        pcall(applyAmmo,pawn,clip,resv)
      else
        -- 关掉后清状态：下次再开视为"新设置"（会补满一次当前弹药）
        if not last.ammo_off then last.ammo_off=true last.ammo_tag=nil last.ammo_hand=nil end
      end
      local dgs=tonumber(F.dig_speed) or 0
      if dgs>0 then
        if last.dig_speed~=tostring(dgs) then last.dig_speed=tostring(dgs) pcall(applyDig,pawn,dgs) end
      else last.dig_speed=nil end
      -- ★v93：「设置血量」功能已删除（不再发 Cheat_SetHealth）
      -- ★v96：删掉「杀某个队友」(kill_slot) 与「醉酒」(intox_player/intox_value，后者还会 SetAll 影响全队)
      --   两段驱动；用户要求所有功能只对自己生效（要自杀用「自杀」按钮即可）。
    end
    -- ★v93：无敌（god）。老实现是 auto_revive → 每 2 秒发 C_Revive，一点就崩
    --        （21:12 那次：EXCEPTION_ACCESS_VIOLATION reading 0xffffffffffffffff，栈全在 FSD-Win64-Shipping），
    --        **C_Revive 的自动驱动整段删除**。现在只做纯属性写入。
)FSDLUA",
R"FSDLUA(    if F.god=="1" then
      pcall(applyGodMode,pawn,true)
      -- ★v94：自起（死亡自动复活）——和无敌同一个开关，见下面的 applySelfRevive
      pcall(applySelfRevive,pawn)
    else
      pcall(applyGodMode,pawn,false)
    end
    -- v75：喷气靴**没有开关**，只有「装备喷气靴mk2」这个按钮。唯一会自动补一次的情况：
    -- 按钮是在"还没角色/加载中"时按的（jbPending）→ 进游戏 8 秒后替他把那次补上。
    if jbPending and now-worldSince>=8 and valid(pawn) then
      jbPending=false
      pcall(jbArm,pawn,pc)
      W("  [喷气靴] 补上之前按下的那次：已给自己穿 MK2")
    end    if F.killaura=="1" and now-worldSince>=8 then pcall(monsterWipe,F,pc,pawn,now) end
    if tick%40==0 then
      local c=mov(pawn)
      W("  状态: 走速="..ts(gp(c,"MaxWalkSpeed")).." 跳速="..ts(gp(c,"JumpZVelocity"))
        .." 移动模式="..ts(gp(c,"MovementMode")).." 跳次="..ts(gp(pawn,"JumpMaxCount")))
    end
  end)
  if not ok then W("  !!! 主循环异常(已继续): "..tostring(e)) end
  return false
end)
-- ================= 自瞄（30ms 跟枪）=================
-- 实测依据（v50/v51 会话）：pc:GetControlRotation() 返回 table{Pitch,Yaw,Roll}（注意 Pitch 可能是 337.7 这种
-- 未归一化值）；PlayerCameraManager:GetCameraLocation() 返回 table{X,Y,Z}；敌人是 /Game/Enemies/ 下的 Pawn
-- （例：ENE_Spider_Grunt_Ice_C），locOf() 能取到坐标。做法：把 ControlRotation 拧向"最近的、在范围米内的敌人"。
-- 0=关；>0 = 自瞄距离（米）。目标每 0.45 秒重扫一次（枚举 Pawn 贵），坐标每 30ms 取一次。
-- v51 新增三个开关（都在功能表里）：
--   aim_vis        =1 只锁"看得见"的单位（**默认开**） / 0 隔着墙也锁（用 pc:LineOfSightTo(t,{X=0,Y=0,Z=0},false)）
--                  ※ 实测：LineOfSightTo 只传 1 个参数会报错，必须传满 3 个参数；ViewPoint 全 0 = 用相机位置。
--   aim_auto       =1 一直自动锁（默认） / 0 只在你按住鼠标左键时锁（左键状态由 DLL 每帧探测后写进功能表 lmb=0/1，
--                  引擎侧绝不要自己去调 IsInputKeyDown —— 实测那个调用会直接把游戏打崩）
--   aim_show_range =1 显示范围（DLL 在屏幕中心画白色空心细圈，半径按自瞄范围算）+ 高亮锁定目标
-- v59：喷气靴回退成"只发控制台命令"——v58 直呼 Server_CheatJetBoots_MK2() 把游戏打崩了（14:18 dump），
--   结论：**绝对不要从 Lua 直呼 Server_* 前缀的 RPC**（网络参数校验过不了，C++ 异常直接死进程）。
-- v58：喷气靴改成直接调角色/CheatManager 上的 Server_CheatJetBoots_MK2 / Server_CheatJetBoots
--   （这两个函数名是从 Pawn 类链转储里实打实看到的，附带属性 JetBootsComponentMK2Spawnable）。
-- v57：修 aimVisible 定义顺序（"隔墙瞄准=开"时自瞄整个循环抛 nil 调用 → 目标不换、隔墙怪照锁）。
-- v56：① 镐子攻速改成写角色 Mesh 的 GlobalAnimRateScale（挥镐动画速率，能读回验证）；
--   ② 喷气靴一次把所有已知命令变体都发一遍并读回状态，方便定位哪个有效。
-- v55：抄 DX11 修改器的 HUD：敌人/队友带 生命/护甲、队友条目(T)、雷达(R 行)、解除抓捕动作。
-- v54：① 实例级敌人缓存（entCache）：同一只怪只分类一次，之后每 tick 只 valid()+取坐标，
--   把"给对象命名"（最危险的动作）压到"第一次看到这只活怪"那一刻。崩溃报告实锤：12:44 那次崩在
--   UE4SS.dll+0x52DBB9（读 0x18 偏移）——就是 GetFullName/FName::ToString 读已释放对象。
--   ② 自瞄的贵检查（死亡/距离/可见性/瞄点）限频到 0.15s 一次，每 tick 只做 valid+坐标+写朝向。
-- v53：① 类名缓存（不再对每个 Pawn 实例调 GetFullName → 修"自瞄+怪物全灭 闪退"）；
--   ② 目标每 tick 复检（死了/出范围/被遮挡立刻换目标）③ 瞄身体中心（胶囊体）不再打胯下；
--   ④ 间隔墙检测加"最近被渲染过"这道保险。
-- v52：① 新增自瞄总开关 aim_on（默认关）；② 三个开关都改成菜单里的开关按键、默认关闭，并把 aim_auto 改名"镇魂机枪"；
--   ③ 显示范围改名"显示范围"，圈改成屏幕中心的白色空心细圈（DLL 画）；④ 新增 aim_smooth 平滑度（0=瞬间锁）。
local aimTarget, aimEnumAt, aimReady, aimLockLogged = nil, -99, false, nil
local aimName, aimSkipLogged, aimOverlayOn, aimLmb = nil, false, false, false
local aimCheckAt, aimOffZ = -99, 55              -- v54：贵检查限频时间戳 / 瞄点相对脚底的高度差（默认胸口 55cm）
local AIM_FILE = "FSDTrainer_aim.txt"
local function aimAtan2(y,x)
  if x>0 then return math.atan(y/x) end
  if x<0 then if y>=0 then return math.atan(y/x)+math.pi else return math.atan(y/x)-math.pi end end
  if y>0 then return math.pi/2 elseif y<0 then return -math.pi/2 end
  return 0
end
-- ★v57 修一个隐蔽 bug：aimPick 里要调 aimVisible，但 aimVisible 原来定义在 aimPick **之后**——
-- Lua 的 `local function X` 只对**后面**的代码可见，写在前面时 X 就成了全局（nil），
-- 于是"隔墙瞄准=开"时 aimPick 每 tick 抛 attempt to call a nil value，整个自瞄循环被 pcall 吞掉、
-- 目标再也不会换 → 用户看到的"隔着墙还被锁住"就是这个。视线判定必须定义在前面。
local function aimVisible(pc,t)
  local ok,v=pcall(function() return pc:LineOfSightTo(t,{X=0,Y=0,Z=0},false) end)
  if ok and v ~= true then return false end
  local ok2,v2=pcall(function() return t:WasRecentlyRendered(0.25) end)
  if ok2 and v2 == false then return false end
  return true
end
local function aimPick(pc,lim,px,py,pz,needLOS)
  local ok,l=pcall(FindAllOf,"Pawn")
  if not ok or type(l)~="table" then return nil end
  local best,bd,bn=nil,nil,nil
  for _,p in ipairs(l) do
    if valid(p) then
      local isE,nm=isEnemy(p)                 -- v53：走类名缓存，不再对实例调 GetFullName
      if isE and not isDeadActor(p) then      -- v53：死掉/正在消失的怪不再瞄
        local x,y,z=locOf(p)
        if x then
          local d=math.sqrt((x-px)^2+(y-py)^2+(z-pz)^2)/100.0
          if d<=lim and (bd==nil or d<bd) then
            -- v80：needLOS=1（「只锁可见单位」开，默认）时只收"看得见"的（逐个做视线判定，取最近的可见目标）
            if (not needLOS) or aimVisible(pc,p) then best,bd,bn=p,d,enemyName(nm or "") end
          end
        end
      end
    end
  end
  return best,bd,bn
end
-- 瞄准点：优先用敌人胶囊体的世界坐标（=身体中心，通常比 Actor 原点高），再往上偏一点打胸口；
-- 拿不到胶囊就退化成"原点 +55cm"。（用户反馈老的 +25cm 打在胯下）
local function aimPointOf(t)
  local cap=gp(t,"CapsuleComponent")
  if valid(cap) then
    local ok,v=pcall(function() return cap:K2_GetComponentLocation() end)
    if ok and type(v)=="table" then
      local x,y,z=v.X or v.x, v.Y or v.y, v.Z or v.z
      if type(x)=="number" and type(y)=="number" and type(z)=="number" then
        local hh=num(gp(cap,"CapsuleHalfHeight")) or 45
        return x,y,z+hh*0.35
      end
    end
  end
  local x,y,z=locOf(t)
  if x then return x,y,z+55 end
  return nil
end
-- 锁定目标框：写文件给 DLL 画（DLL 侧读 FSDTrainer_aim.txt 的 A 行：x1 y1 x2 y2 距离 名字）。
-- v52：索敌圈改成由 DLL 自己画（屏幕中心白色空心细圈，半径由"自瞄范围"算），引擎不再写 R 行。
local function aimOverlay(view,pawn,px,py,pz,lim)
  local out={"# Carlhand 自瞄 范围="..f2(lim).."米\n"}
  if valid(aimTarget) then
    local tx,ty,tz=locOf(aimTarget)
    if tx then
      local d=math.sqrt((tx-px)^2+(ty-py)^2+(tz-pz)^2)/100.0
      local x1,y1,x2,y2=espBox(view,pawn,tx,ty,tz,90)
      if x1 then
        out[#out+1]=string.format("A %.0f %.0f %.0f %.0f %.0f %s\n",x1,y1,x2,y2,d,aimName or "目标")
      end
    end
  end
  local ok,f=pcall(io.open,G..AIM_FILE,"w")
  if ok and f then pcall(function() f:write(table.concat(out)) f:flush() f:close() end) end
end
local function aimEye(pc,pawn)
  local cam=gp(pc,"PlayerCameraManager")
  if valid(cam) then
    local ok,loc=pcall(function() return cam:GetCameraLocation() end)
    if ok and type(loc)=="table" then
      local x,y,z=loc.X or loc.x,loc.Y or loc.y,loc.Z or loc.z
      if type(x)=="number" and type(y)=="number" and type(z)=="number" then return x,y,z,"camera" end
    end
  end
  local ox,oy,oz=locOf(pawn)
  if ox then return ox,oy,oz+60,"pawn+60" end
  return nil
end
LoopAsync(30, function()
  if _G.__FSDT_GEN~=MYGEN then return true end
  local ok,e=pcall(function()
    local F=readF()
    local on=(tonumber(F.aim_on) or 0)>0          -- 自瞄总开关（顶部那个）
    local lim=tonumber(F.aim_range) or 0
    local showRange=(tonumber(F.aim_show_range) or 0)>0
    if (not on) or lim<=0 then
      if aimTarget then aimTarget=nil aimLockLogged=nil W("  [自瞄] 已关闭") end
      if aimOverlayOn then aimOverlayOn=false wf(G..AIM_FILE,"# 自瞄叠层关闭\n") end
      aimLmb=false
      return
    end
    if lim>500 then lim=500 end
    local pc,cm,pawn=ctx()
    if not (valid(pc) and valid(pawn)) then aimTarget=nil return end
    if not aimReady then
      aimReady=true
      W("  [自瞄] 已就绪（范围="..f2(lim).."米，平滑度="..f2(tonumber(F.aim_smooth) or 0).."）")
    end
    -- 触发方式：“镇魂机枪”=1 → 一直自动锁；=0 → 只按住鼠标左键时锁（左键状态由 DLL 写进功能表）
    local auto=(tonumber(F.aim_auto) or 0)~=0
    local lmb=(tostring(F.lmb)=="1")
    if (not auto) and (lmb~=aimLmb) then
      aimLmb=lmb
      W("  [自瞄] 左键"..(lmb and "按下 → 开始锁" or "松开 → 暂停锁"))
    end
    -- 索敌圈：DLL 自己按"自瞄范围"画（这里只负责锁定目标框；没目标时也写一次，让框消失）
    if (not showRange) and aimOverlayOn then
      aimOverlayOn=false wf(G..AIM_FILE,"# 自瞄叠层关闭\n")
    end
    if showRange then
      local ox,oy,oz=locOf(pawn)
      if ox then aimOverlayOn=true pcall(aimOverlay,prepareView(pc),pawn,ox,oy,oz,lim) end
    end
    if (not auto) and (not lmb) then
      if aimTarget then aimTarget=nil W("  [自瞄] 松开左键，暂停锁定") end
      return
    end
    local now=os.clock()
    -- v80：功能表里这个开关改名成「只锁可见单位」（key=aim_vis，**默认开**）：
    --   开 = 只锁"看得见"的（视线 LineOfSightTo + 最近渲染过）；关 = 隔着墙也能锁。
    --   （兼容老功能表：没有 aim_vis 时退回旧 key aim_wall；两个都没有就按新默认=只锁可见。）
    local needLOS=((tonumber(F.aim_vis) or tonumber(F.aim_wall) or 1)>0)
    -- ★v54：把"贵检查"（死亡判定/距离/可见性/瞄点）限频到 0.15 秒一次；每 tick 只做
    --   valid() + 取一次坐标 + 写朝向。理由：12:44 那次崩在 UE4SS.dll（读已释放对象），
    --   每 30ms 对刚被打死的怪连做 4~5 次属性/UFunction 访问风险太大。
    if aimTarget ~= nil then
      local bad=false
      if not valid(aimTarget) then bad=true
      elseif now-(aimCheckAt or -99)>0.15 then
        aimCheckAt=now
        if isDeadActor(aimTarget) then bad=true W("  [自瞄] 目标已死亡 → 换目标")
        else
          local px0,py0,pz0=locOf(pawn)
          local tx0,ty0,tz0=locOf(aimTarget)
          if not (px0 and tx0) then bad=true
          else
            local d0=math.sqrt((tx0-px0)^2+(ty0-py0)^2+(tz0-pz0)^2)/100.0
            if d0>lim then bad=true
            elseif needLOS and (not aimVisible(pc,aimTarget)) then bad=true W("  [自瞄] 目标被遮挡 → 换目标")
            else
              local ax,ay,az=aimPointOf(aimTarget)      -- 顺便刷新"胸口"高度差（每 0.15s 一次就够）
              if ax then aimOffZ=az-tz0 end
            end
          end
        end
      end
      if bad then aimTarget=nil aimEnumAt=-99 aimLockLogged=nil end
    end
    if now-(aimEnumAt or -99)>0.45 then
      aimEnumAt=now
      local px,py,pz=locOf(pawn)
      if px then
        local t,d,nm=aimPick(pc,lim,px,py,pz,needLOS)
        if (not t) and needLOS and (not aimSkipLogged) then
          aimSkipLogged=true W("  [自瞄] 只锁看得见的：当前范围内没有可见目标（被墙挡住或没怪）")
        end
        if t~=aimTarget then
          aimTarget=t aimName=nm
          if t then W("  [自瞄] 锁定最近目标 距离="..f2(d).."米 "..tostring(nm)) else aimLockLogged=nil end
        end
      end
    end
    if not valid(aimTarget) then
      if aimTarget then aimTarget=nil W("  [自瞄] 目标消失，等重扫") end
      return
    end
    local tx,ty,tz=locOf(aimTarget)              -- v54：每 tick 只取一次坐标
    if not tx then aimTarget=nil return end
    tz=tz+(aimOffZ or 55)                        -- 瞄胸口（高度差由"贵检查"每 0.15s 刷新一次）
    local ex,ey,ez=aimEye(pc,pawn)
    if not ex then return end
    local dx,dy,dz=tx-ex,ty-ey,tz-ez            -- tz 已经是"胸口"（aimPointOf 给的身体中心）
    local horiz=math.sqrt(dx*dx+dy*dy)
    if horiz<1 and math.abs(dz)<1 then return end
    local yaw=aimAtan2(dy,dx)*180.0/math.pi
    local pitch=aimAtan2(dz,horiz)*180.0/math.pi
    if pitch>89 then pitch=89 elseif pitch<-89 then pitch=-89 end
    -- 平滑度（0~100）：0=不平滑（瞬间锁）；越大越柔和。原理：读出当前朝向，按比例往目标方向插值。
    local sm=tonumber(F.aim_smooth) or 0
    if sm<0 then sm=0 elseif sm>100 then sm=100 end
    if sm>0 then
      local okc,cr=pcall(function() return pc:GetControlRotation() end)
      local cp,cy2=fld(cr,"Pitch"),fld(cr,"Yaw")
      if type(cp)=="number" and type(cy2)=="number" then
        local dp=pitch-cp
        while dp>180 do dp=dp-360 end
        while dp<-180 do dp=dp+360 end
        local dyy=yaw-cy2
        while dyy>180 do dyy=dyy-360 end
        while dyy<-180 do dyy=dyy+360 end
        local k=1.0-(sm/100.0)*0.92        -- sm=0 → k=1；sm=100 → k=0.08（很慢）
        pitch=cp+dp*k
        yaw=cy2+dyy*k
        -- 归一化到 [-180,180]：否则从 337.9(=-22°) 往 -20° 靠时中间值会落在 340 这种位置，
        -- 被游戏的俯仰限位（±89.9）一夹就会突然抬头
        while pitch>180 do pitch=pitch-360 end
        while pitch<-180 do pitch=pitch+360 end
        while yaw>180 do yaw=yaw-360 end
        while yaw<-180 do yaw=yaw+360 end
        if pitch>89 then pitch=89 elseif pitch<-89 then pitch=-89 end
      end
    end
    pcall(function() pc:SetControlRotation({Pitch=pitch,Yaw=yaw,Roll=0}) end)
    if aimLockLogged~=aimTarget then
      aimLockLogged=aimTarget
      local okr,rr=pcall(function() return pc:GetControlRotation() end)
      local rp,ry=fld(rr,"Pitch"),fld(rr,"Yaw")
      W("  [自瞄] 已开始跟枪：目标方向 pitch="..f2(pitch).." yaw="..f2(yaw)
        .."；读回 pitch="..tostring(rp).." yaw="..tostring(ry))
    end
  end)
  if not ok then W("  !!! 自瞄循环异常(已继续): "..tostring(e)) end
  return false
end)
)FSDLUA",
};
static const int kCheatEngineLuaPartCount = 12;
// ★v96 隐患修复：这个常量一旦和数组实际长度不一致，dllmain 的 for 循环就会越界读
//   kCheatEngineLuaParts[n]（strlen 一个野指针 → 直接崩进程）。加编译期断言：
//   以后块数变了忘了改常量，**编译就会失败**，不会再带着隐患出厂。
static_assert(kCheatEngineLuaPartCount == sizeof(kCheatEngineLuaParts) / sizeof(kCheatEngineLuaParts[0]),
              "Lua 分块数常量与数组长度不一致：请把 kCheatEngineLuaPartCount 改成实际块数");

