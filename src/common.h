// common.h - FSDTrainer (Deep Rock Galactic DX12 Trainer) 公共定义
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

using Microsoft::WRL::ComPtr;

#define FSDTRAINER_NAME L"FSDTrainer"
#define FSDTRAINER_VER  L"1.0 (DX12)"

namespace fsd {
    // 目标进程
    constexpr const wchar_t* kGameExe = L"FSD-Win64-Shipping.exe";
    constexpr const wchar_t* kGameWin = L"Deep Rock Galactic";
}
