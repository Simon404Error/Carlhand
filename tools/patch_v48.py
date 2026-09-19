# v48：奔跑只影响冲刺（走路恢复原速）+ 回归善良人格（含输入框值复位）+ 按钮改名
import io
root = r'D:\Deepseek-Harness\work\260918_143041'

# ---------- 1) 引擎：奔跑只作用于冲刺 ----------
p = root + r'\src\cheat_engine.h'
s = io.open(p, encoding='utf-8').read()
def rep(old, new, n=1):
    global s
    assert old in s, 'anchor: ' + old[:90]
    s = s.replace(old, new, n)

rep('R"FSDLUA(-- Carlhand 功能引擎 v47', 'R"FSDLUA(-- Carlhand 功能引擎 v48')
rep('''if _G.__FSDT_ENGINE_V47 then return end
_G.__FSDT_ENGINE_V47 = true''',
'''-- v48：奔跑倍率只作用于"冲刺"（之前直接乘进走路目标 → 走路也变快，用户要的是冲刺更快、走路不变）。
--   做法：走路目标恢复成 base×移速倍率；另开一条 60ms 小循环，检测到游戏把速度推到冲刺值(≈2×走路)时，
--   把"游戏冲刺值×奔跑倍率"写回去（游戏可能每帧重写，所以用更快的循环去抢）。
if _G.__FSDT_ENGINE_V48 then return end
_G.__FSDT_ENGINE_V48 = true''')
rep('W("\\n>> 功能引擎 v47 已载入 "', 'W("\\n>> 功能引擎 v48 已载入 "')
rep('''  -- 奔跑：本版本 SetFastMovement 无效，所以直接把"奔跑倍率"乘进速度目标里（移速已验证这条路有效）
  local walkT=base*walkMult*sprintMult''',
'''  -- 走路目标只跟"移速倍率"有关；奔跑倍率交给下面那条 60ms 冲刺循环（只影响冲刺）
  local walkT=base*walkMult''')

# 冲刺快循环（插在 ESP 循环之前，方便共用 mid 变量）
anchor = '-- ================= ESP 文件 ================='
if anchor not in s:
    anchor = ')FSDLUA",\nR"FSDLUA(-- ================= ESP：独立快循环'
assert anchor in s
add = '''-- ================= 冲刺加速（60ms）=================
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

'''
rep(anchor, add + anchor)

# 换图时清掉冲刺基准
rep('      lastWrote=nil walkBase=nil sprintBase=nil basesCaptured=false',
    '      lastWrote=nil walkBase=nil sprintBase=nil basesCaptured=false\n      sprintRef=nil')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('engine v48 patched')

# ---------- 2) AllOff：数值型也恢复默认值 ----------
p = root + r'\src\patches.cpp'
s = io.open(p, encoding='utf-8').read()
old = '''    void FeatureTable::AllOff() {
        for (size_t i = 0; i < list.size(); ++i) if (list[i].on) Toggle(i);
        WriteFeaturesFile();
    }'''
new = '''    // 回归善良人格：开关全关 + 数值型全部恢复默认值（否则输入框还留着旧值、游戏里也还在生效）
    void FeatureTable::AllOff() {
        for (size_t i = 0; i < list.size(); ++i) {
            Feature& f = list[i];
            if (f.kind == "cheat") {
                f.cur = f.def;          // 数值型/动作型都回到默认
                f.on = false;
                continue;
            }
            if (f.on) Toggle(i);
        }
        WriteFeaturesFile();
        Log("已回归善良人格：所有开关关闭、数值恢复默认");
    }'''
assert old in s
s = s.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('patches.cpp AllOff patched')

# ---------- 3) UI：按钮改名 + 清输入框缓存 ----------
p = root + r'\src\ui.cpp'
s = io.open(p, encoding='utf-8').read()
old = '''        if (Button(x + w - S(170), cy - S(4), S(160), LineH() + S(8), L"全部关闭（恢复）")) {
            g_features.AllOff(); RestoreGameState(); note = L"已全部关闭并恢复原状";
        }'''
new = '''        if (Button(x + w - S(170), cy - S(4), S(160), LineH() + S(8), L"回归善良人格")) {
            g_features.AllOff(); RestoreGameState();
            valBuf.clear();                      // 输入框缓存清掉，下一帧按默认值重新填充
            note = L"已回归善良人格（开关关闭、数值恢复默认）";
        }'''
assert old in s
s = s.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ui.cpp patched')
