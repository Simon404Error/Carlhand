#include "hook.h"
#include "log.h"

namespace fsd {

    bool HookVtable(void* obj, int index, void* hookFn, void** origFn) {
        if (!obj || !hookFn) return false;
        void** vtbl = *(void***)obj;
        if (!vtbl) return false;
        DWORD old = 0;
        if (!VirtualProtect(&vtbl[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
            Log("HookVtable: VirtualProtect 失败 idx=%d", index);
            return false;
        }
        if (origFn) *origFn = vtbl[index];
        vtbl[index] = hookFn;
        DWORD tmp = 0;
        VirtualProtect(&vtbl[index], sizeof(void*), old, &tmp);
        FlushInstructionCache(GetCurrentProcess(), &vtbl[index], sizeof(void*));
        Log("HookVtable: 已挂钩 idx=%d 原函数=%p 钩子=%p", index, origFn ? *origFn : nullptr, hookFn);
        return true;
    }

    bool UnhookVtable(void* obj, int index, void* origFn) {
        if (!obj || !origFn) return false;
        void** vtbl = *(void***)obj;
        DWORD old = 0;
        if (!VirtualProtect(&vtbl[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
        vtbl[index] = origFn;
        DWORD tmp = 0;
        VirtualProtect(&vtbl[index], sizeof(void*), old, &tmp);
        return true;
    }
}
