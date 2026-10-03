# 卡尔的大手 (Carlhand)

深岩银河（Deep Rock Galactic, UE 4.27）的**单文件开箱即用修改器**：
DLL 注入 + 自绘 D3D12/D3D11 覆盖层菜单（**不是 ImGui，是自研 GDI 位图内绘**）+ 游戏内 Lua 引擎（UE4SS）。

> ## ⚠️ 暂时不维护了
>
> **作者暂时不打算继续维护这个项目。**
> 找工作捏，没时间搞了，功能做的差不多了，但是不打算开源了
>
> 想接着做的话，源码、构建脚本、`交接文档.md`（含每个未完成项的排查思路和所有已知坑）都留在这里了，
> 欢迎直接 fork 接手。仓库里**只保留能跑起来所必需的文件**，构建中间产物、临时脚本、大量 pak 解包
> 参考物都不在仓库里。

---

## 快速开始（玩家）

1. 启动《深岩银河》，**进到太空站（大厅）**。
2. 双击 `out\Carlhand.exe`（单文件，**不需要**自己装 UE4SS —— exe 内嵌了 UE4SS v3.0.1，
   会自动释放到游戏目录并在当前这局就 `LoadLibrary` 加载）。
3. 游戏里按 `INSERT` 呼出菜单；`END` 卸载；`HOME` 把菜单复位到屏幕中央（拖到屏幕外也能救回来）。

- 菜单：拖动标题栏移动、拖边框缩放、滚轮上下滚动、输入框支持 `回车`/`Tab` 应用。
- `设置` 页：显示高级功能（扫描/冻结）、背景不透明度、重新载入 ini、菜单大小。
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
> 被占用时也可以把整棵树拷到 `_build_vNN\` 里编译，产物再拷出来。

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
src\        修改器 DLL 源码（C++17，仅 Windows SDK + D3D12/D3D11/GDI，无第三方库）
  cheat_engine.h   ★ 游戏内 Lua 引擎（分 12 块存放，MSVC 单字面量 16KB 上限，有 static_assert 校验块数）
  dllmain / overlay / ui / patches / scanner / mem / config / log / hook
loader\     单文件启动器（找进程 / 释放内嵌 UE4SS+配置 / 注入 / 提权）
build\      构建脚本（build / loader / repack / test / restore_ue4ss / gen_blob / convert_features）
config\     FSDTrainer.ini（UTF-16LE 功能表）、UE4SS-settings.ini（内嵌用）
test\       Dx12Test / D3D11Test（自建交换链，用于离线验证覆盖层与界面）
tools\      当前版本实际用到的脚本（Lua 分块/语法检查、minidump 与 UE 崩溃报告解析、PE 导出表、监控）
out\        产物：Carlhand.exe（单文件）
```

## 架构（一图流）
```
菜单 UI(DLL 覆盖层，GDI 位图 → D3D12/D3D11 贴图) → FSDTrainer_features.txt
        ↓ 每 300ms / 150ms / 100ms
游戏内 Lua 引擎(UE4SS) → 属性读写 / CheatManager 调用 / ExecuteConsoleCommand("C_XXX")
        ↓
游戏内 FSDCheatManager / CharacterMovement
```
启动器把内嵌的 `UE4SS.dll` + `Mods\FSDTrainerMod`(执行器) + `Mods\CheatManagerEnablerMod`(创建 FSDCheatManager)
部署到游戏目录，然后注入 DLL；DLL 直接 `LoadLibrary(UE4SS.dll)` —— 所以**不用重启游戏**。

**渲染方式**：没有用 ImGui。`overlay.cpp` 挂钩 D3D12 的
`ID3D12CommandQueue::ExecuteCommandLists`（失败则回退挂钩 D3D11 的 `Present/Present1/ResizeBuffers`），
`ui.cpp` 用 GDI（`CreateFontIndirectW` + `TextOut`/`FillRect`）把界面画进内存位图，
再由自编译的顶点/像素着色器当纹理贴到屏幕上 —— 好处是零第三方依赖、中文直接支持；
代价是动画/圆角/模糊这类效果都得手写。

---

## 致谢 / 依赖
- [UE4SS (RE-UE4SS)](https://github.com/UE4SS-RE/RE-UE4SS) —— MIT，游戏内 Lua 对象层（本仓库的
  `out\Carlhand.exe` 内嵌了 v3.0.1），详见 [`NOTICE.md`](NOTICE.md)。
- 游戏内作弊命令清单来自深岩银河自身的 `FSDCheatManager`。

## 免责声明
仅供**单机 / 私人房间**学习研究使用。联机使用可能被主机拒绝、被服务器封禁或影响存档，
一切后果自负；请勿用于商业用途。
