#pragma once
#include "common.h"

namespace fsd {
    // 模块基址 / 大小
    uintptr_t ModuleBase(const wchar_t* moduleName, size_t* outSize = nullptr);
    uintptr_t MainBase(size_t* outSize = nullptr);

    // 特征码扫描：pattern 形如 "48 8B 05 ?? ?? ?? ??" 或 "488B05???????"
    uintptr_t FindPattern(uintptr_t start, size_t size, const char* pattern);
    uintptr_t FindPatternInMain(const char* pattern);          // 找不到返回 0
    std::vector<uintptr_t> FindAllInMain(const char* pattern, size_t maxHits = 16);

    // 内存读写（自动改页保护）
    bool MemRead(uintptr_t addr, void* buf, size_t size);
    bool MemWrite(uintptr_t addr, const void* buf, size_t size);
    bool MemProtect(uintptr_t addr, size_t size, DWORD prot, DWORD* old = nullptr);

    // 为某个地址生成 AOB 特征码（把看起来像指针的 8 字节段替换成 ??，便于游戏更新后仍能定位）
    std::string MakeSignature(uintptr_t addr, size_t before = 16, size_t after = 24);

    // 在可写内存里找“指向该地址（或该地址 ± tolerance 内）”的指针（一级指针扫描）
    std::vector<uintptr_t> FindPointersTo(uintptr_t target, size_t maxHits = 64, size_t tolerance = 0x100);

    template <typename T> inline T Read(uintptr_t addr) { T v{}; MemRead(addr, &v, sizeof(T)); return v; }
    template <typename T> inline bool Write(uintptr_t addr, T v) { return MemWrite(addr, &v, sizeof(T)); }
}
