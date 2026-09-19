#pragma once
#include "common.h"

namespace fsd {

    enum ValType { VT_I8 = 0, VT_I16, VT_I32, VT_I64, VT_F32, VT_F64, VT_COUNT };
    const wchar_t* TypeName(ValType t);
    size_t TypeSize(ValType t);
    bool   ReadTypedValue(uintptr_t addr, ValType t, double& out);
    bool   WriteTypedValue(uintptr_t addr, ValType t, double v);

    enum CmpMode { CMP_EXACT = 0, CMP_CHANGED, CMP_UNCHANGED, CMP_INCREASED, CMP_DECREASED, CMP_MODE_COUNT };
    const wchar_t* CmpName(CmpMode m);

    struct ScanHit { uintptr_t addr; double cur; double prev; };

    class Scanner {
    public:
        std::vector<ScanHit> hits;
        std::mutex           mtx;
        std::atomic<bool>    busy{ false };
        std::atomic<int>     progress{ 0 };
        std::atomic<bool>    cancel{ false };
        std::atomic<uint64_t> elapsedMs{ 0 };
        ValType              type = VT_I32;
        bool                 moduleOnly = false;
        std::string          status;

        void FirstScan(ValType t, double value, bool moduleOnly_);
        void FirstScanUnknown(ValType t, bool moduleOnly_, double lo, double hi);   // 未知初始值（向导用）
        void NextScan(double value, CmpMode mode);
        void Refresh();                        // 重新读取所有结果当前值
        void Clear();
        void SetStatus(const std::string& s);
        std::string Status();
        size_t Count();

    private:
        void ScanPass(double value, CmpMode mode, bool first);
        void ScanPassUnknown(double lo, double hi);
    };

    // 冻结器
    struct FreezeEntry {
        uintptr_t addr;
        ValType   type;
        double    value;
        bool      enabled;
        double    orig;          // 加入冻结时的原始值（关闭时恢复用）
        bool      hasOrig;
    };
    class Freezer {
    public:
        std::vector<FreezeEntry> list;
        std::mutex mtx;
        void Start();
        void Stop();
        void Set(uintptr_t addr, ValType t, double v);      // 存在则更新（并记住原值）
        void Remove(size_t idx);
        bool Has(uintptr_t addr);
        void Restore(size_t idx);                           // 关闭该项并写回原值
        void RestoreAll();                                  // 全部关闭并写回原值（恢复游戏原状）
        bool DisableAddr(uintptr_t addr);                   // 关闭指定地址的锁定并写回原值
        size_t EnabledCount();
    private:
        std::thread th;
        std::atomic<bool> run{ false };
    };

    // 一键恢复游戏原状：撤销所有补丁 + 解除全部冻结（写回原值）
    void RestoreGameState();
}
