#pragma once
#include "common.h"

namespace fsd {
    // 交换虚表函数指针（用于 DXGI / D3D12 对象），返回原函数
    bool HookVtable(void* obj, int index, void* hookFn, void** origFn);
    bool UnhookVtable(void* obj, int index, void* origFn);
}
