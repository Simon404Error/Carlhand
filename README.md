# 卡尔的大手 (Carlhand)

深岩银河（Deep Rock Galactic, UE 4.27）的**单文件开箱即用修改器**：
DLL 注入 + 自绘 D3D12/D3D11 覆盖层菜单 + 游戏内 Lua 引擎（UE4SS）。

> **希望各位大佬一起完善它**——特别是下面「待完善」清单里的功能（奔跑/镐子/飞行/补给仓/杀指定队友）。
> 接手前请先读 [`交接文档.md`](交接文档.md)（现状 / 架构 / 未完成 / 坑 / 开工步骤），
> 功能清单看 [`功能对照表.md`](功能对照表.md) 与 [`作弊功能表.md`](作弊功能表.md)。

---

## 快速开始（玩家）

1. 启动《深岩银河》，**进到太空站（大厅）**。
2. 双击 `out\Carlhand.exe`（单文件，**不需要**自己装 UE4SS——exe 内嵌了 UE4SS v3.0.1，
   会自动释放到游戏目录并在当前这局就 `LoadLibrary` 加载）。
3. 游戏里按 `INSERT` 呼出菜单；`END` 卸载；`HOME` 把菜单复位到屏幕中央（拖到屏幕外也能救回来）。

- 菜单：拖动标题栏移动、**拖边框缩放**、滚轮上下滚动、输入框支持 `回车`/`Tab` 应用（Tab 会跳到下一个输入框）。
- `设置` 页：显示高级功能（扫描/冻结）、背景不透明（默认 100%）、重新载入 ini、菜单大小。
- `常规` 页右上角 **「回归善良人格」**：所有开关关闭 + 数值恢复默认 + 恢复游戏原状。

## 构建（开发者）

需要 **Visual Studio Build Tools**（默认装在 `C:\BuildTools`）与 Windows SDK（`fxc`/`rc`）。
```bat
build\loader.bat            :: 编译 out\FSDTrainer.dll 并打包 out\FSDTrainer.exe（内嵌 DLL+ini+UE4SS）
copy out\FSDTrainer.exe out\Carlhand.exe
build\test.bat              :: 编译 out\verify\Dx12Test.exe / D3D11Test.exe（不启动游戏就能看界面）
build\restore_ue4ss.bat     :: 游戏目录被别的工具包换掉 UE4SS 时，一键恢复可用的 UE4SS v3.0.1
```
> 注意：**游戏占着 `out\FSDTrainer.dll` 时无法编译**，先在游戏里按 `END` 或退出游戏。

### 界面自检（不用进游戏）
```powershell
cmd /c build\test.bat
$env:FSDTRAINER_DUMP_UI='1'; $env:FSDTRAINER_UI_TAB='0'   # 页签序号：0=常规 … 8=设置 9=扫描 10=冻结
start out\verify\Dx12Test.exe
start out\Carlhand.exe --target Dx12Test.exe --dll out\FSDTrainer.dll --silent --no-launch
# 出来后 out\verify\FSDTrainer_ui.bmp 就是界面截图
```

---

## 目录结构
```
src\        修改器 DLL 源码（C++17，仅 Windows SDK + D3D12/D3D11/GDI）
  cheat_engine.h   ★ 游戏内 Lua 引擎（分 6 块存放，MSVC 单字面量 16KB 上限）
  dllmain / overlay / ui / patches / scanner / mem / config / log / hook
loader\     单文件启动器（找进程 / 释放内嵌 UE4SS+配置 / 注入 / 提权）
build\      构建脚本（build / loader / repack / test / restore_ue4ss / gen_blob / convert_features）
config\     FSDTrainer.ini（UTF-16LE 功能表）、UE4SS-settings.ini（内嵌用）
test\       Dx12Test / D3D11Test（自建交换链，用于离线验证覆盖层与界面）
tools\      排查脚本（minidump 解析、UE 崩溃报告解析、PE 导出表、Lua 分块/语法检查、补丁脚本、游戏监控）
reference\  266 条游戏作弊命令表、命令映射、执行器脚本、示例 Lua
out\        产物：Carlhand.exe（单文件）与界面截图
```

## 架构（一图流）
```
菜单 UI(DLL 覆盖层) → FSDTrainer_features.txt
        ↓ 每 300ms/60ms/150ms
游戏内 Lua 引擎(v48, UE4SS) → 属性读写 / CheatManager 调用 / ExecuteConsoleCommand("C_XXX")
        ↓
游戏内 FSDCheatManager / CharacterMovement
```
启动器把内嵌的 `UE4SS.dll` + `Mods\FSDTrainerMod`(执行器) + `Mods\CheatManagerEnablerMod`(创建 FSDCheatManager)
部署到游戏目录，然后注入 DLL；DLL 直接 `LoadLibrary(UE4SS.dll)` —— 所以**不用重启游戏**。

---

## 功能状态

| 状态 | 内容 |
|---|---|
| ✅ 实测可用（约 35 项） | 移速、跳跃高度、无限跳跃、怪物全灭、无敌+复活、装备喷气靴二代、设置血量、自杀、杀全队、醉酒、解锁类（武器/皮肤/特长/商店/超频/图纸/深潜/设施/啤酒/姿势/镐子）、资源/经验/代币、传送、扫描/冻结（内存） |
| 🟡 接了但没效果（**欢迎大佬接手**） | 奔跑速度（仅冲刺，待确认）、镐子攻速（`SetQuadPickAxe` 无反应）、飞行模式（`C_SetFlyMode` 无反应）、呼叫补给仓（`InstantDropResuppyPod` 游戏不认，建议改 SpawnActor）、杀某个队友（`C_KillPlayer <编号>` 编号无效） |
| 🔒 已封印待捡起 | 显示敌人 / 矿物透视（ESP 链路已实现，见交接文档 7.3）、夜视、霸体、自瞄范围 |
| ❌ 未实现 | 武器/职业页各功能（游戏无对应命令，需要 AOB 补丁或属性定位）、显示队友/显示雷达/随机名字 |

**下一步建议**：`交接文档.md` 第 7 节（含每个未完成项的排查思路），以及 `功能对照表.md` 第二节
（约 45 条游戏命令可以直接接上，加功能只需改 ini + 引擎 `ACT` 表一行）。

---

## 已知坑（改代码前务必看 `交接文档.md` 第 9 节）
1. **不能用 `GetFullName()` 做对象有效性校验** —— 换图时对已销毁对象调用会直接崩进程（用 `IsValid()`）。
2. **不能用 `FindAllOf/FindFirstOf` 试可能不存在的类名，也不能 `obj["名字"]` 下标** ——
   会抛 C++ 异常**打死 UE4SS 的 Lua 线程**（pcall 接不住），症状是"所有功能突然全不生效"。
3. `GetPropertyValue` 对不存在的属性也会返回对象 → 必须读回校验。
4. 读属性返回的 UObject 每次都是新 userdata → 比较对象要用 `GetAddress()`。
5. `config\FSDTrainer.ini` 是 **UTF-16LE**；改了功能表要把 `ini_ver=` 加一（否则老用户的功能表不会更新）。
6. 引擎 Lua 单块不得超 16KB（MSVC 限制）→ 用 `tools\partscheck.py` 检查。

---

## 致谢 / 依赖
- [UE4SS (RE-UE4SS)](https://github.com/UE4SS-RE/RE-UE4SS) —— MIT，游戏内 Lua 对象层（本仓库的 `out\Carlhand.exe` 内嵌了 v3.0.1），详见 [`NOTICE.md`](NOTICE.md)。
- 游戏内作弊命令清单来自深岩银河自身的 `FSDCheatManager`（`reference\cheats\`）。

## 免责声明
仅供**单机 / 私人房间**学习研究使用。联机使用可能被主机拒绝、被服务器封禁或影响存档，
一切后果自负；请勿用于商业用途。
