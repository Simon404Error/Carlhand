# 卡尔的大手 (Carlhand)

深岩银河（Deep Rock Galactic, UE 4.27）的**单文件开箱即用修改器**：
DLL 注入 + 自绘 D3D12/D3D11 覆盖层菜单（**不是 ImGui，是自研 GDI 位图内绘**）+ 游戏内 Lua 引擎（UE4SS）

> ## ⚠️ 暂时不维护了
>
> **完整版没放出来，目前仓库里的是丐版**
> 
> 有兴趣的话各位可以自己改着玩玩

---

## 构建

需要 **Visual Studio Build Tools**
```bat
build\loader.bat            :: 编译 out\FSDTrainer.dll 并打包 out\FSDTrainer.exe（内嵌 DLL+ini+UE4SS）
copy out\FSDTrainer.exe out\Carlhand.exe
build\test.bat              :: 编译 out\verify\Dx12Test.exe / D3D11Test.exe（不启动游戏就能看界面）
build\restore_ue4ss.bat     :: 游戏目录被别的工具包换掉 UE4SS 时，一键恢复可用的 UE4SS v3.0.1
```

### 界面自检
```powershell
cmd /c build\test.bat
$env:FSDTRAINER_DUMP_UI='1'; $env:FSDTRAINER_UI_TAB='0'   # 页签序号：0=常规 … 8=设置 9=扫描 10=冻结
start out\verify\Dx12Test.exe
start out\Carlhand.exe --target Dx12Test.exe --dll out\FSDTrainer.dll --silent --no-launch
# out\verify\FSDTrainer_ui.bmp 就是界面截图
```

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

## 依赖
- [UE4SS (RE-UE4SS)](https://github.com/UE4SS-RE/RE-UE4SS) —— MIT，游戏内 Lua 对象层（本仓库的
  `out\Carlhand.exe` 内嵌了 v3.0.1），详见 [`NOTICE.md`](NOTICE.md)
- 游戏内作弊命令清单来自深岩银河自身的 `FSDCheatManager`

## 免责声明
仅供**单机 / 私人房间**学习研究使用。联机使用可能被主机拒绝、被服务器封禁或影响存档

一切后果自负；请勿用于商业用途
