# 第三方组件声明 (NOTICE)

## UE4SS (RE-UE4SS) — MIT License
本仓库的 `out\Carlhand.exe` **内嵌**了 UE4SS v3.0.1（`UE4SS.dll`）以及 `UE4SS-settings.ini`，
程序运行时会把它们释放到游戏目录，并从中加载 Lua 对象层。

- 项目主页：https://github.com/UE4SS-RE/RE-UE4SS
- 许可证：MIT
- 版权归 UE4SS / RE-UE4SS 作者所有

如需重新分发本仓库的 exe，请保留本声明。若你自行构建，可把
`build\loader.bat` 里的 `UE4DIR` 指向你自己的 UE4SS 安装目录。

## 游戏内作弊命令清单
`reference\cheats\FSDCheatManager_全命令表.txt` 是从**本机运行中的游戏进程**里导出的
`FSDCheatManager` 命令名列表，仅作为功能对照参考，不含游戏代码或资源。

## 其它参考文件
`reference\zDRG\*.lua`、`reference\FSDTrainerMod_main_v7.lua` 为本项目自用脚本/示例，
来源为本地参考资料，未包含第三方受版权保护的代码。
