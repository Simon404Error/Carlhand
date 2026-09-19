# 武器 / 角色属性「高度自定义」功能设计（参考用户给的 5 个 mod）

> 结论先放前面：**我们已经具备做出这些功能的三条路**——① 控制台命令（266 条）② 属性读写（含写入后读回校验）
> ③ 只读反射（找类/属性/组件）。**唯一要克制的是"直呼游戏函数"**（今天已经用两条人命价试出来：
> `Server_CheatJetBoots` 在本版必崩、`IsInputKeyDown` 会崩）。
> 所以下面每个功能都按"**先把数值/开关找出来（探针），再写属性/发命令**"来设计。

---

## 0. 我们参考的 mod（用户给的链接）

| mod | 它做什么 | 对我们的价值 |
|---|---|---|
| All-in-One: More Ammo and Grenades, Better Tools | 弹匣/备弹/手雷容量 + 工具（镐/钻机/照明）增强 | **一整套"容量类"数值**的字段清单 |
| Adjustable Ammo | 弹药倍率可调（**本地已装：pak 里的 `ammopercent`**） | 现成的"可调倍率"实现结构 |
| Kid Assist（300 Health/Shield） | 血量/护盾拉高到 300 那档 | **血/盾上限与回复**的字段 |
| Host Tools | 房主侧一整套（时间/难度/刷怪/踢人/服务器设置） | 我们「特殊功能页」的空白区 |
| Better Digging | 挖掘更快/更大 | 镐子/地形破坏参数 |

> 本地 mods pak 现在有 13 个 mod 目录（741 条目），其中与上面直接相关的是 **`ammopercent`**；
> 其余参考 mod 没装，需要时用 mint/mod.io 单独下载，或让我解 `ammopercent` 学结构。

---

## 1. 三类"改法"（每个功能先判断它属于哪类）

1. **改资产数值（最稳，不用碰运行时对象）**：DRG 把大量平衡数值放在 `GD_*` / `*Settings` 这类 DataAsset 里
   （例：喷气靴的 `GD_JetBoots_Infinite`：`MaxJetFuel=50 / JetFuelCost=12 / JetFuelRechargeRate=45`）。
   我们要做的就是**在游戏里读到这个资产对象、改它的属性**（我们已验证"属性可写可读回"）。
   ⚠ 缺点：DataAsset 常被 CDO 共享 → 改它会影响所有同类型对象（对"全局倍率"类需求正好）。
2. **改运行时对象属性（最常用）**：角色/武器/组件实例上的属性。
   已知可用的：`CharacterMovement.MaxWalkSpeed / JumpZVelocity / GravityScale`、
   `PlayerCharacter.RunningSpeed`、`Mesh.GlobalAnimRateScale`、`JetBootsMovementComponent.CurrentJetFuel` …
3. **发控制台命令（最省事）**：266 条里已经有一大批容量/资源/难度相关的（`C_Resupply`、`C_SetAmmoCostEnabled`、
   `C_UnlockAll*`、`C_SpawnScriptedWave`、`C_FixedMissionSeed` …）。**新功能优先在这里找现成的。**

---

## 2. 逐个功能的设计

### 2.1 弹药 / 手雷容量（All-in-One、Adjustable Ammo）
**目标**：弹匣容量、备弹量、手雷/支援工具数量，都能按倍率或绝对值自定义。
**DRG 里的位置（要探针确认名称）**：
- 武器是 `AItem` 派生 + `UAmmoDrivenWeapon` 组件：典型字段 `ClipSize / AmmoCount / MaxAmmo / ReserveAmmo /
  MaxReserveAmmo / AmmoCost / MaxAmmoCount`（不同版本名字不同，必须实读）。
- 手雷/支援工具在 `Inventory` 上的 `Throwable`/`SupportTool` 槽位，有 `Amount`/`MaxAmount`。
**我们怎么做**
1. 探针：拿到本地 Pawn → `Inventory`（属性）→ 遍历武器 Actor → `FindAllOf("AmmoDrivenWeapon")` 之类，
   逐个打印属性名 + 类型 + 当前值（**只读**，注意别一次遍历太多类，见 §4 的坑）。
2. 得到字段后：写"倍率 × 原值"（保存原值，关闭时回滚），或者写绝对值。
3. 功能表设计（ini）：每个字段一行 `ui=value`，`unit=x倍/发`，`def=0`（0=不改）。
**风险**：低（纯属性）。**优先级：P0。**

### 2.2 血量 / 护盾（Kid Assist）
**目标**：最大血量/护盾、回复速度、是否无敌。
**位置**：角色上的 `HealthComponent` / `ShieldComponent`（我们的 ESP 已经在读它们的 `Health/Shield/...`），
典型字段：`MaxHealth / Health / MaxShield / Shield / ShieldRegenRate / RegenDelay / bIsDead`。
**做法**：直接写 `MaxHealth/MaxShield` + 把当前值补满；「无敌」我们已经用 `C_GodMode` 走命令。
**注意**：多人游戏里血量是**服务端权威**——自己开房有效；当客机在别人局里改了会被同步回去（要在 UI 上标注"仅主机有效"）。
**优先级：P0。**

### 2.3 工具增强（All-in-One 的 Better Tools）
**目标**：照明亮度/范围、护盾发生器、平台枪/滑索/钻机等工具的参数。
**位置**：`WeaponsNTools/<工具名>` 下的 Actor/组件属性（数值面板大多在上面）。
**做法**：沿用「武器属性面板」的同一套扫描 + 写值机制，只是换一组类名。
**优先级：P1。**

### 2.4 挖掘更快（Better Digging）
**目标**：镐子伤害/范围/动画速率、地形破坏半径。
**我们已经有的**：`Mesh.GlobalAnimRateScale`（挥镐动画速率，实测可写可读回 ✓）、`C_FastMovement`/`C_QuadPickaxe`
（游戏自带的"快速移动/四倍镐"）。
**要补的**：镐子 Actor 上的 `PickaxeDamage / DigRadius / DigStrength / TerrainDamage`（**需探针确认**），
以及地形破坏用到的 `Carve` 参数。
**做法**：探针找字段 → 写值 → 读回；和"快速镐"命令互相独立（都要给开关）。
**优先级：P0（用户点名）。**

### 2.5 房主工具（Host Tools）
**目标**（房主/主机侧）：任务时间/波次控制、刷怪、难度、踢人、暂停、服务器设置、作弊命令授权等。
**我们已有的基础**：266 条命令里一大批就是这条线（`C_SpawnNormalWave / C_SpawnScriptedWave / C_SetAmmoCostEnabled /
C_FixedMissionSeed / C_ForceMissionWarning / C_KillAll* / C_DisableDreadnoughts …`）+ `CheatManager` 有效性判断。
**新增**：
- 会话控制：暂停（`pc:SetPause` 属"游戏函数"，**先别碰**）、踢人（`KickPlayer` 同风险）→ 先用命令/只读探测确认有没有 `C_` 命令对应。
- 显示/日志类：把"当前波次/剩余时间/难度/GD_ 设置对象名"只读打印出来（低风险、信息量大）。
**优先级：P1（先把能只读的信息做成面板）。**

---

## 3. 统一的落地流程（每个功能都照这个走）

1. **只读探针**先跑（`tools/probe_jb*.lua` 那套：`FindAllOf` + `ForEachProperty` + 读值 → 写日志文件）。
   ⚠ 一次只遍历**一个类**、只读需要的属性；把类/实例缓存起来。
2. 找到字段后写引擎代码：**保存原值 → 写新值 → 读回校验 → 日志**（我们所有功能都是这个模板）。
3. ini 加 `[feature.xxx]`（`cat=` 新分类、`ui=toggle/value`、`def=0`）+ `ini_ver` 加一 + 同步 loader 检查值。
4. 改完必跑：`extract_lua.py` + `partscheck.py` + luaparser（块上限 16380，现第 1 块已 12688）。
5. 我自己起游戏注入验证（**不许谎报**：没验证就写"未验证"）。

---

## 4. 坑（今天新增的，务必遵守）

1. **循环里绝不用 `GetFullName()`**：今天 16:25 崩过一次 `UE4SS.dll+0x52DBB9` 读 0x18 = 对已销毁对象取名字。
   识别对象一律用"类名查询（`FindAllOf`）+ `GetAddress()` 比对"，日志打地址标签。
2. **探针别一次遍历类的全部属性**：v5/v6 那种写法会把**执行器 Lua 线程弄死**（心跳停、cmd 热载不再响应；
   游戏本身不死）。分批、按名字白名单读。
3. **属性读不到时 `gp()` 返回 0 个值** → 直接 `tostring(gp(...))` 会抛异常废掉整个 tick（已用 `ts()` 兜住）。
4. **服务端权威**：血量/弹药/刷怪这类改动，**只有当房主（自己开房/单人）才稳**；客机要标注"仅主机"。
5. **DataAsset/Cdo 共享**：改 `GD_*` 会影响所有同类对象，改前记原值、提供一键还原。
6. **uasset 解析器**：老的 mod 资产（如 `ammopercent`）头部带 5 条 CustomVersion，长度和喷气靴那批不同 →
   `tools/ueasset_parse.py` 的 `find_tables()` 对这类文件还找不到表（**TODO**：按 version 字段正经解析头部）。
   暂时读 mod 资产可以让我在游戏里用反射读（更省事）。

---

## 5. 建议的排期

| 阶段 | 内容 | 依据 |
|---|---|---|
| **P0** | 弹药/弹匣/手雷容量倍率；血盾上限与补满；挖掘（镐子参数 + 已有动画速率）；工具参数 | 用户点名，且都是"读字段→写值"低风险 |
| P1 | 武器属性总面板（每把武器的数值一屏）；房主信息面板（波次/时间/难度/设置对象） | 需要一轮探针 |
| P2 | Host Tools 的动作用户（刷怪脚本、任务参数、服务器设置） | 依赖命令清单核对 + 只读探测确认 |
