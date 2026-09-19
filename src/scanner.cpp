#include "scanner.h"
#include "mem.h"
#include "log.h"
#include <cmath>
#include <cstdlib>

namespace fsd {

    const wchar_t* TypeName(ValType t) {
        static const wchar_t* n[] = { L"字节", L"2字节", L"4字节", L"8字节", L"浮点", L"双浮点" };
        return (t >= 0 && t < VT_COUNT) ? n[t] : L"?";
    }
    size_t TypeSize(ValType t) {
        switch (t) { case VT_I8: return 1; case VT_I16: return 2; case VT_I32: return 4;
                     case VT_I64: return 8; case VT_F32: return 4; case VT_F64: return 8; }
        return 4;
    }
    const wchar_t* CmpName(CmpMode m) {
        static const wchar_t* n[] = { L"等于", L"已变化", L"未变化", L"增加", L"减少" };
        return (m >= 0 && m < CMP_MODE_COUNT) ? n[m] : L"?";
    }

    bool ReadTypedValue(uintptr_t addr, ValType t, double& out) {
        switch (t) {
        case VT_I8:  { int8_t v;  if (!MemRead(addr, &v, 1)) return false; out = (double)v; return true; }
        case VT_I16: { int16_t v; if (!MemRead(addr, &v, 2)) return false; out = (double)v; return true; }
        case VT_I32: { int32_t v; if (!MemRead(addr, &v, 4)) return false; out = (double)v; return true; }
        case VT_I64: { int64_t v; if (!MemRead(addr, &v, 8)) return false; out = (double)v; return true; }
        case VT_F32: { float v;   if (!MemRead(addr, &v, 4)) return false; out = (double)v; return true; }
        case VT_F64: { double v;  if (!MemRead(addr, &v, 8)) return false; out = v; return true; }
        }
        return false;
    }

    bool WriteTypedValue(uintptr_t addr, ValType t, double v) {
        switch (t) {
        case VT_I8:  { int8_t x  = (int8_t)v;  return MemWrite(addr, &x, 1); }
        case VT_I16: { int16_t x = (int16_t)v; return MemWrite(addr, &x, 2); }
        case VT_I32: { int32_t x = (int32_t)v; return MemWrite(addr, &x, 4); }
        case VT_I64: { int64_t x = (int64_t)v; return MemWrite(addr, &x, 8); }
        case VT_F32: { float x   = (float)v;   return MemWrite(addr, &x, 4); }
        case VT_F64: { double x  = v;          return MemWrite(addr, &x, 8); }
        }
        return false;
    }

    void Scanner::SetStatus(const std::string& s) { std::lock_guard<std::mutex> lk(mtx); status = s; }
    std::string Scanner::Status() { std::lock_guard<std::mutex> lk(mtx); return status; }
    size_t Scanner::Count() { std::lock_guard<std::mutex> lk(mtx); return hits.size(); }

    void Scanner::Clear() {
        std::lock_guard<std::mutex> lk(mtx);
        hits.clear(); status.clear();
    }

    // 自身模块范围（避免扫描/锁定时误伤自己）
    static void CollectRegions(bool moduleOnly, std::vector<std::pair<uintptr_t, size_t>>& out);
    static bool SelfModuleRange(uintptr_t& base, size_t& size) {
        HMODULE h = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCWSTR)(const void*)&CollectRegions, &h) || !h) return false;
        base = (uintptr_t)h; size = 0;
        auto dos = (PIMAGE_DOS_HEADER)h;
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            auto nt = (PIMAGE_NT_HEADERS64)((uintptr_t)h + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE) size = nt->OptionalHeader.SizeOfImage;
        }
        return size != 0;
    }

    // 收集可扫描内存区域
    static void CollectRegions(bool moduleOnly, std::vector<std::pair<uintptr_t, size_t>>& out) {
        out.clear();
        uintptr_t selfBase = 0; size_t selfSize = 0;
        SelfModuleRange(selfBase, selfSize);
        if (moduleOnly) {
            size_t sz = 0;
            uintptr_t b = MainBase(&sz);
            if (b && sz) out.push_back({ b, sz });
            return;
        }
        SYSTEM_INFO si; GetSystemInfo(&si);
        uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
        uintptr_t maxAddr = (uintptr_t)si.lpMaximumApplicationAddress;
        MEMORY_BASIC_INFORMATION mbi{};
        while (addr < maxAddr && VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
            uintptr_t base = (uintptr_t)mbi.BaseAddress;
            if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
                !(mbi.Protect & PAGE_NOACCESS) && mbi.Type != MEM_MAPPED &&
                (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
                bool writable = (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
                bool self = selfSize && base < selfBase + selfSize && selfBase < base + mbi.RegionSize;
                if (writable && !self) out.push_back({ base, mbi.RegionSize });
            }
            uintptr_t next = base + mbi.RegionSize;
            if (next <= addr) break;
            addr = next;
        }
    }

    void Scanner::FirstScan(ValType t, double value, bool moduleOnly_) {
        if (busy.exchange(true)) return;
        type = t; moduleOnly = moduleOnly_;
        cancel = false; progress = 0;
        std::thread([this, value]() {
            ScanPass(value, CMP_EXACT, true);
            busy = false;
        }).detach();
    }

    void Scanner::NextScan(double value, CmpMode mode) {
        if (busy.exchange(true)) return;
        cancel = false; progress = 0;
        std::thread([this, value, mode]() {
            ScanPass(value, mode, false);
            busy = false;
        }).detach();
    }

    // 未知初始值扫描：只保留“看起来合理”的数值（用于向导里搜血量/弹药）
    void Scanner::FirstScanUnknown(ValType t, bool moduleOnly_, double lo, double hi) {
        if (busy.exchange(true)) return;
        type = t; moduleOnly = moduleOnly_;
        cancel = false; progress = 0;
        std::thread([this, lo, hi]() {
            ScanPassUnknown(lo, hi);
            busy = false;
        }).detach();
    }

    void Scanner::ScanPassUnknown(double lo, double hi) {
        ULONGLONG t0 = GetTickCount64();
        std::vector<std::pair<uintptr_t, size_t>> regions;
        CollectRegions(moduleOnly, regions);
        uint64_t total = 0;
        for (auto& r : regions) total += r.second;

        const size_t CAP = 8000000;             // 结果上限，避免占用过多内存
        const size_t CHUNK = 4 * 1024 * 1024;
        const size_t tsz = TypeSize(type);
        const size_t step = tsz >= 4 ? 4 : tsz; // 按对齐步长扫描：候选数大约减少 4 倍（真实数据基本对齐）
        std::vector<uint8_t> buf(CHUNK);
        std::vector<ScanHit> local;
        local.reserve(1 << 20);
        uint64_t done = 0;

        for (auto& r : regions) {
            if (cancel) break;
            uintptr_t base = r.first;
            size_t remain = r.second;
            while (remain >= tsz && !cancel) {
                size_t want = remain > CHUNK ? CHUNK : remain;
                if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)base, buf.data(), want, nullptr)) {
                    for (size_t off = 0; off + tsz <= want; off += step) {
                        const uint8_t* p = buf.data() + off;
                        bool ok = false; double v = 0;
                        switch (type) {
                        case VT_F32: { float f; memcpy(&f, p, 4); if (f == f && fabs((double)f) >= 1e-6 && fabs((double)f) <= hi) { ok = true; v = f; } break; }
                        case VT_F64: { double d; memcpy(&d, p, 8); if (d == d && fabs(d) >= 1e-6 && fabs(d) <= hi) { ok = true; v = d; } break; }
                        case VT_I32: { int32_t i; memcpy(&i, p, 4); if (i != 0 && (int64_t)llabs((int64_t)i) <= (int64_t)hi) { ok = true; v = i; } break; }
                        case VT_I64: { int64_t i; memcpy(&i, p, 8); if (i != 0 && (double)llabs(i) <= hi) { ok = true; v = (double)i; } break; }
                        case VT_I16: { int16_t i; memcpy(&i, p, 2); if (i != 0) { ok = true; v = i; } break; }
                        case VT_I8:  { int8_t  i; memcpy(&i, p, 1); if (i != 0) { ok = true; v = i; } break; }
                        }
                        if (ok) {
                            local.push_back({ base + off, v, v });
                            if (local.size() >= CAP) { cancel = true; break; }
                        }
                    }
                }
                base += want; remain -= want; done += want;
                progress = (int)(total ? (done * 100 / total) : 0);
            }
        }
        {
            std::lock_guard<std::mutex> lk(mtx);
            hits.swap(local);
            char b[256];
            if (cancel) sprintf_s(b, "未知初始值扫描完成（到达上限/被取消）：%zu 条候选", hits.size());
            else sprintf_s(b, "未知初始值扫描完成：%zu 条候选（区间 %.3g ~ %.3g）", hits.size(), lo, hi);
            status = b;
        }
        progress = 100;
        elapsedMs = GetTickCount64() - t0;
        Log("%s", Status().c_str());
    }

    void Scanner::ScanPass(double value, CmpMode mode, bool first) {
        ULONGLONG t0 = GetTickCount64();
        std::vector<std::pair<uintptr_t, size_t>> regions;
        CollectRegions(moduleOnly, regions);
        uint64_t total = 0;
        for (auto& r : regions) total += r.second;

        std::vector<ScanHit> local;
        const size_t tsz = TypeSize(type);
        const uint8_t* vp = (const uint8_t*)&value;
        int32_t  pat32 = (int32_t)value; float  patf = (float)value;
        int64_t  pat64 = (int64_t)value; double patd = value;
        int8_t   pat8  = (int8_t)value;  int16_t pat16 = (int16_t)value;
        uint64_t done = 0;
        const size_t CHUNK = 4 * 1024 * 1024;
        std::vector<uint8_t> buf(CHUNK);

        // 已有结果集合（再次扫描）
        std::vector<ScanHit> prevHits;
        if (!first) { std::lock_guard<std::mutex> lk(mtx); prevHits = hits; }

        if (!first) {
            // 再次扫描：只遍历已有结果，速度极快
            const size_t n = prevHits.empty() ? 1 : prevHits.size();
            for (size_t i = 0; i < prevHits.size(); ++i) {
                if (cancel) break;
                uintptr_t addr = prevHits[i].addr;
                double cur = 0;
                if (ReadTypedValue(addr, type, cur)) {
                    double prev = prevHits[i].cur;
                    bool ok = false;
                    switch (mode) {
                    case CMP_EXACT:     ok = (type == VT_F32 || type == VT_F64) ? (fabs(cur - value) < 1e-6) : (cur == value); break;
                    case CMP_CHANGED:   ok = (cur != prev); break;
                    case CMP_UNCHANGED: ok = (cur == prev); break;
                    case CMP_INCREASED: ok = (cur > prev); break;
                    case CMP_DECREASED: ok = (cur < prev); break;
                    }
                    if (ok) local.push_back({ addr, cur, prev });
                }
                done++;
                if ((done & 0x3FF) == 0) progress = (int)(done * 100 / n);
            }
            progress = 100;
        } else {
            for (auto& r : regions) {
                if (cancel) break;
                uintptr_t base = r.first;
                size_t remain = r.second;
                while (remain >= tsz && !cancel) {
                    size_t want = remain > CHUNK ? CHUNK : remain;
                    if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)base, buf.data(), want, nullptr)) {
                        for (size_t off = 0; off + tsz <= want; off += 1) {
                            uintptr_t addr = base + off;
                            uint8_t* p = buf.data() + off;
                            bool hit = false;
                            switch (type) {
                            case VT_I8:  hit = (*(int8_t*)p  == pat8);  break;
                            case VT_I16: hit = (*(int16_t*)p == pat16); break;
                            case VT_I32: hit = (*(int32_t*)p == pat32); break;
                            case VT_I64: hit = (*(int64_t*)p == pat64); break;
                            case VT_F32: hit = (memcmp(p, &patf, 4) == 0); break;
                            case VT_F64: hit = (memcmp(p, &patd, 8) == 0); break;
                            }
                            if (hit) {
                                local.push_back({ addr, value, value });
                                if (local.size() >= 4000000) { cancel = true; break; }
                            }
                        }
                    }
                    base += want; remain -= want; done += want;
                    progress = (int)(total ? (done * 100 / total) : 0);
                }
            }
            progress = 100;
        }

        {
            std::lock_guard<std::mutex> lk(mtx);
            hits.swap(local);
            char b[256];
            if (cancel) sprintf_s(b, "扫描已取消（结果 %zu 条）", hits.size());
            else if (first) sprintf_s(b, "首次扫描完成：%zu 条命中", hits.size());
            else sprintf_s(b, "再次扫描(%S)完成：%zu 条命中", CmpName(mode), hits.size());
            status = b;
        }
        elapsedMs = GetTickCount64() - t0;
        Log("扫描结束: %s", status.c_str());
    }

    void Scanner::Refresh() {
        std::lock_guard<std::mutex> lk(mtx);
        for (auto& h : hits) {
            double v = 0;
            if (ReadTypedValue(h.addr, type, v)) { h.prev = h.cur; h.cur = v; }
        }
    }

    // ---------------- Freezer ----------------
    void Freezer::Start() {
        if (run.exchange(true)) return;
        th = std::thread([this]() {
            while (run) {
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    for (auto& e : list) if (e.enabled) WriteTypedValue(e.addr, e.type, e.value);
                }
                Sleep(15);
            }
        });
    }
    void Freezer::Stop() { if (run.exchange(false)) { if (th.joinable()) th.join(); } }

    void Freezer::Set(uintptr_t addr, ValType t, double v) {
        std::lock_guard<std::mutex> lk(mtx);
        for (auto& e : list) if (e.addr == addr) { e.value = v; e.type = t; e.enabled = true; return; }
        FreezeEntry e{};
        e.addr = addr; e.type = t; e.value = v; e.enabled = true; e.orig = 0; e.hasOrig = false;
        double cur = 0;
        if (ReadTypedValue(addr, t, cur)) { e.orig = cur; e.hasOrig = true; }   // 记住原值，便于恢复
        list.push_back(e);
    }
    void Freezer::Remove(size_t idx) {
        std::lock_guard<std::mutex> lk(mtx);
        if (idx < list.size()) list.erase(list.begin() + idx);
    }
    bool Freezer::Has(uintptr_t addr) {
        std::lock_guard<std::mutex> lk(mtx);
        for (auto& e : list) if (e.addr == addr) return true;
        return false;
    }
    void Freezer::Restore(size_t idx) {
        std::lock_guard<std::mutex> lk(mtx);
        if (idx >= list.size()) return;
        auto& e = list[idx];
        if (e.enabled && e.hasOrig) WriteTypedValue(e.addr, e.type, e.orig);   // 写回原始值
        e.enabled = false;
    }
    void Freezer::RestoreAll() {
        std::lock_guard<std::mutex> lk(mtx);
        for (auto& e : list) {
            if (e.enabled && e.hasOrig) WriteTypedValue(e.addr, e.type, e.orig);
            e.enabled = false;
        }
    }
    bool Freezer::DisableAddr(uintptr_t addr) {
        std::lock_guard<std::mutex> lk(mtx);
        for (auto& e : list) {
            if (e.addr == addr) {
                if (e.enabled && e.hasOrig) WriteTypedValue(e.addr, e.type, e.orig);
                e.enabled = false;
                return true;
            }
        }
        return false;
    }

    size_t Freezer::EnabledCount() {
        std::lock_guard<std::mutex> lk(mtx);
        size_t n = 0;
        for (auto& e : list) if (e.enabled) ++n;
        return n;
    }
}
