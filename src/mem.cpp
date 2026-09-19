#include "mem.h"
#include "log.h"

namespace fsd {

    uintptr_t ModuleBase(const wchar_t* moduleName, size_t* outSize) {
        HMODULE h = GetModuleHandleW(moduleName);
        if (!h) return 0;
        if (outSize) {
            auto dos = (PIMAGE_DOS_HEADER)h;
            size_t sz = 0;
            if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
                auto nt = (PIMAGE_NT_HEADERS64)((uintptr_t)h + dos->e_lfanew);
                if (nt->Signature == IMAGE_NT_SIGNATURE)
                    sz = nt->OptionalHeader.SizeOfImage;
            }
            *outSize = sz;
        }
        return (uintptr_t)h;
    }

    uintptr_t MainBase(size_t* outSize) { return ModuleBase(nullptr, outSize); }

    static bool ParsePattern(const char* pat, std::vector<int>& out) {
        out.clear();
        for (const char* p = pat; *p; ) {
            while (*p == ' ') ++p;
            if (!*p) break;
            if (*p == '?') {
                out.push_back(-1);
                while (*p == '?') ++p;
            } else {
                int hi = -1, lo = -1;
                auto hexv = [](char c)->int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                hi = hexv(*p); if (hi < 0) return false; ++p;
                if (*p) { lo = hexv(*p); if (lo >= 0) ++p; }
                out.push_back(lo < 0 ? hi : (hi << 4) | lo);
            }
        }
        return !out.empty();
    }

    uintptr_t FindPattern(uintptr_t start, size_t size, const char* pattern) {
        std::vector<int> pat;
        if (!ParsePattern(pattern, pat) || size < pat.size()) return 0;
        const uint8_t* b = (const uint8_t*)start;
        const size_t n = pat.size();
        const int first = pat[0];
        for (size_t i = 0; i + n <= size; ++i) {
            if (first >= 0 && b[i] != (uint8_t)first) continue;
            bool ok = true;
            for (size_t j = 1; j < n; ++j) {
                if (pat[j] >= 0 && b[i + j] != (uint8_t)pat[j]) { ok = false; break; }
            }
            if (ok) return start + i;
        }
        return 0;
    }

    uintptr_t FindPatternInMain(const char* pattern) {
        size_t sz = 0;
        uintptr_t base = MainBase(&sz);
        if (!base || !sz) return 0;
        return FindPattern(base, sz, pattern);
    }

    std::vector<uintptr_t> FindAllInMain(const char* pattern, size_t maxHits) {
        std::vector<uintptr_t> out;
        std::vector<int> pat;
        size_t sz = 0;
        uintptr_t base = MainBase(&sz);
        if (!base || !ParsePattern(pattern, pat) || sz < pat.size()) return out;
        const uint8_t* b = (const uint8_t*)base;
        const size_t n = pat.size();
        for (size_t i = 0; i + n <= sz && out.size() < maxHits; ++i) {
            if (pat[0] >= 0 && b[i] != (uint8_t)pat[0]) continue;
            bool ok = true;
            for (size_t j = 1; j < n; ++j) {
                if (pat[j] >= 0 && b[i + j] != (uint8_t)pat[j]) { ok = false; break; }
            }
            if (ok) out.push_back(base + i);
        }
        return out;
    }

    bool MemProtect(uintptr_t addr, size_t size, DWORD prot, DWORD* old) {
        DWORD o = 0;
        if (!VirtualProtect((LPVOID)addr, size, prot, &o)) return false;
        if (old) *old = o;
        return true;
    }

    bool MemRead(uintptr_t addr, void* buf, size_t size) {
        SIZE_T got = 0;
        if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, buf, size, &got) && got == size)
            return true;
        // 退化路径：直接读取（同进程）
        __try { memcpy(buf, (const void*)addr, size); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool MemWrite(uintptr_t addr, const void* buf, size_t size) {
        // 快速路径：目标页本身可写时不改页保护（冻结上千个地址时开销差异巨大）
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi) &&
            mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
            (uintptr_t)buf && addr + size <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
            memcpy((void*)addr, buf, size);
            return true;
        }
        DWORD old = 0;
        if (!VirtualProtect((LPVOID)addr, size, PAGE_EXECUTE_READWRITE, &old)) {
            SIZE_T wr = 0;
            return WriteProcessMemory(GetCurrentProcess(), (LPVOID)addr, buf, size, &wr) && wr == size;
        }
        memcpy((void*)addr, buf, size);
        DWORD tmp = 0;
        VirtualProtect((LPVOID)addr, size, old, &tmp);
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)addr, size);
        return true;
    }

    // ---- 特征码生成：把“像指针”的 8 字节段通配成 ??，其余保留精确字节 ----
    std::string MakeSignature(uintptr_t addr, size_t before, size_t after) {
        std::vector<uint8_t> buf(before + after);
        if (!MemRead(addr - before, buf.data(), buf.size())) return std::string();
        std::string out;
        size_t i = 0;
        while (i < buf.size()) {
            if (i + 8 <= buf.size()) {
                uint64_t q = 0;
                memcpy(&q, buf.data() + i, 8);
                bool ptrLike = q >= 0x10000ULL && q <= 0x00007FFFFFFFFFFFULL && ((q & 0xF) == 0);
                if (ptrLike) {
                    out += "?? ?? ?? ?? ?? ?? ?? ?? ";
                    i += 8;
                    continue;
                }
            }
            char b[8];
            sprintf_s(b, "%02X ", buf[i]);
            out += b;
            ++i;
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }

    // ---- 一级指针扫描：找指向 target（或 target ± tolerance 内）的指针 ----
    std::vector<uintptr_t> FindPointersTo(uintptr_t target, size_t maxHits, size_t tolerance) {
        std::vector<uintptr_t> out;
        SYSTEM_INFO si; GetSystemInfo(&si);
        uintptr_t a = (uintptr_t)si.lpMinimumApplicationAddress;
        uintptr_t maxA = (uintptr_t)si.lpMaximumApplicationAddress;
        MEMORY_BASIC_INFORMATION mbi{};
        const size_t CH = 4 * 1024 * 1024;
        std::vector<uint8_t> buf(CH);
        while (a < maxA && out.size() < maxHits) {
            if (VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
            uintptr_t base = (uintptr_t)mbi.BaseAddress;
            bool usable = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
                          (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
            if (usable) {
                size_t remain = mbi.RegionSize;
                while (remain >= 8 && out.size() < maxHits) {
                    size_t want = remain > CH ? CH : remain;
                    if (MemRead(base, buf.data(), want)) {
                        for (size_t off = 0; off + 8 <= want; off += 8) {
                            uint64_t v = 0; memcpy(&v, buf.data() + off, 8);
                            uintptr_t diff = (v > target) ? (uintptr_t)(v - target) : (uintptr_t)(target - v);
                            if (v >= 0x10000 && diff <= tolerance) {
                                out.push_back(base + off);
                                if (out.size() >= maxHits) break;
                            }
                        }
                    }
                    base += want; remain -= want;
                }
            }
            uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            if (next <= a) break;
            a = next;
        }
        return out;
    }
}
