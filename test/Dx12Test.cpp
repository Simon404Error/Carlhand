// Dx12Test.cpp - 验证用最小 D3D12 呈现程序（模拟游戏交换链，加载 FSDTrainer.dll）
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <string>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

static LRESULT CALLBACK WP(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

// 模拟游戏状态：血量浮点 / 备用弹药整数（每帧递减，用于验证修改器的“搜索-缩小-锁定”向导）
volatile float g_fakeHealth = 1000.0f;
volatile int   g_fakeAmmo = 900;
volatile int   g_magic = 0x5A5A1234;   // 自检用常量

static void LogLine(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, 1024, _TRUNCATE, fmt, ap);
    va_end(ap);
    wprintf(L"%s\n", buf);
    fflush(stdout);
}

int wmain(int argc, wchar_t** argv) {
    int frames = (argc > 1) ? _wtoi(argv[1]) : 200;
    bool usePresent1 = (argc > 2) && (_wtoi(argv[2]) != 0);
    bool noSelfLoad = (argc > 3) && (_wtoi(argv[3]) != 0);   // 由外部启动器注入时使用
    bool enableDebug = false;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WP;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Dx12TestWnd";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Dx12Test", WS_OVERLAPPEDWINDOW,
                                40, 40, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { LogLine(L"窗口创建失败"); return 1; }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    if (enableDebug) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(dbg.GetAddressOf())))) dbg->EnableDebugLayer();
    }

    ComPtr<ID3D12Device> dev;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(dev.GetAddressOf())))) {
        LogLine(L"D3D12 设备创建失败"); return 1;
    }
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(queue.GetAddressOf())))) return 1;

    ComPtr<IDXGIFactory2> fac;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(fac.GetAddressOf())))) return 1;
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = 1280; sd.Height = 720;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swap;
    if (FAILED(fac->CreateSwapChainForHwnd(queue.Get(), hwnd, &sd, nullptr, nullptr, swap.GetAddressOf()))) {
        LogLine(L"交换链创建失败"); return 1;
    }
    ComPtr<IDXGISwapChain3> swap3;
    swap->QueryInterface(IID_PPV_ARGS(swap3.GetAddressOf()));

    {
        ComPtr<IUnknown> u;
        ComPtr<ID3D12CommandQueue> q2;
        ComPtr<IDXGIDevice> dg;
        HRESULT h1 = swap->GetDevice(__uuidof(ID3D12CommandQueue), (void**)q2.GetAddressOf());
        HRESULT h2 = swap->GetDevice(__uuidof(IUnknown), (void**)u.GetAddressOf());
        HRESULT h3 = swap->GetDevice(__uuidof(IDXGIDevice), (void**)dg.GetAddressOf());
        LogLine(L"GetDevice: D3D12Queue=0x%X IUnknown=0x%X IDXGIDevice=0x%X (queue=%p same=%d)",
                (unsigned)h1, (unsigned)h2, (unsigned)h3, (void*)q2.Get(),
                (int)(q2.Get() == (ID3D12CommandQueue*)nullptr ? 0 : (q2.Get() == (ID3D12CommandQueue*)queue.Get())));
    }

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 2;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(rtvHeap.GetAddressOf()));
    UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE h0 = rtvHeap->GetCPUDescriptorHandleForHeapStart();

    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(alloc.GetAddressOf()));
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(list.GetAddressOf()));
    list->Close();
    ComPtr<ID3D12Fence> fence;
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT64 fv = 0;

    HMODULE dll = nullptr;
    LogLine(L"开始呈现 %d 帧 (Present1=%d)", frames, (int)usePresent1);

    // 用于在第 80 帧回读后备缓冲，验证覆盖层是否真的画进了屏幕画面
    ComPtr<ID3D12Resource> readback;
    UINT   rbPitch = 0;
    bool   rbDone = false;

    for (int i = 0; i < frames; ++i) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (i > 5) { g_fakeHealth -= 1.0f; g_fakeAmmo -= 1; }   // 模拟掉血/掉弹药
        if ((i % 30) == 0) {                                    // 便于外部验证“锁定是否生效”
            char b[128];
            sprintf_s(b, "frame=%d health=%.1f ammo=%d\r\n", i, (double)g_fakeHealth, (int)g_fakeAmmo);
            HANDLE f = CreateFileW(L"fake.txt", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) { DWORD w = 0; WriteFile(f, b, (DWORD)strlen(b), &w, nullptr); CloseHandle(f); }
        }

        if (i == 3 && !noSelfLoad) {
            // 载入修改器（真实场景为 Xenos 手动映射注入）
            void** vt = *(void***)queue.Get();
            LogLine(L"注入前 queue=%p vtable[12]=%p", (void*)queue.Get(), vt[12]);
            SetEnvironmentVariableW(L"FSDTRAINER_DUMP_UI", L"1");
            dll = LoadLibraryW(L"FSDTrainer.dll");
            LogLine(L"LoadLibraryW(FSDTrainer.dll) = %p", (void*)dll);
        }
        if (i == 40 && !noSelfLoad) {
            void** vt = *(void***)queue.Get();
            void** vs = *(void***)swap.Get();
            LogLine(L"注入后 queue=%p vtable[12]=%p  swap=%p vtable[8]=%p", (void*)queue.Get(), vt[12], (void*)swap.Get(), vs[8]);
        }
        if (i == 41 && !noSelfLoad) {
            // 直接通过 vtable 槽调用，验证钩子是否真的被调用
            void** vt = *(void***)queue.Get();
            auto fn = (void(STDMETHODCALLTYPE*)(void*, UINT, ID3D12CommandList* const*))vt[12];
            ID3D12CommandList* l2[] = { list.Get() };
            LogLine(L"直接调用 vtable[12]=%p", vt[12]);
            fn(queue.Get(), 1, l2);
            LogLine(L"直接调用返回");
        }

        UINT idx = swap3 ? swap3->GetCurrentBackBufferIndex() : (i % 2);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = h0; rtv.ptr += (SIZE_T)idx * inc;

        alloc->Reset();
        list->Reset(alloc.Get(), nullptr);
        ComPtr<ID3D12Resource> bb;
        swap->GetBuffer(idx, IID_PPV_ARGS(bb.GetAddressOf()));
        dev->CreateRenderTargetView(bb.Get(), nullptr, rtv);

        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = bb.Get();
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1, &b);

        // 第 80 帧：把该缓冲上一轮被覆盖层绘制过的内容拷贝出来，稍后回读统计
        if (i == 80) {
            D3D12_RESOURCE_DESC bd = bb->GetDesc();
            rbPitch = (UINT)((bd.Width * 4 + 255) & ~255u);
            D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC bufd = {};
            bufd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufd.Width = (UINT64)rbPitch * bd.Height;
            bufd.Height = 1; bufd.DepthOrArraySize = 1; bufd.MipLevels = 1;
            bufd.Format = DXGI_FORMAT_UNKNOWN;
            bufd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            bufd.SampleDesc.Count = 1;
            if (SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bufd,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf())))) {
                D3D12_RESOURCE_BARRIER cb = {};
                cb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                cb.Transition.pResource = bb.Get();
                cb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                cb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                cb.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                list->ResourceBarrier(1, &cb);
                D3D12_TEXTURE_COPY_LOCATION dst = {};
                dst.pResource = readback.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Footprint.Format = bd.Format;
                dst.PlacedFootprint.Footprint.Width = (UINT)bd.Width;
                dst.PlacedFootprint.Footprint.Height = (UINT)bd.Height;
                dst.PlacedFootprint.Footprint.Depth = 1;
                dst.PlacedFootprint.Footprint.RowPitch = rbPitch;
                D3D12_TEXTURE_COPY_LOCATION src = {};
                src.pResource = bb.Get();
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = 0;
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                D3D12_RESOURCE_BARRIER cb2 = cb;
                cb2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                cb2.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                list->ResourceBarrier(1, &cb2);
            }
        }

        const float col[4] = { 0.10f, 0.12f, 0.16f, 1.0f };
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list->ClearRenderTargetView(rtv, col, 0, nullptr);

        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1, &b);
        list->Close();
        ID3D12CommandList* lists[] = { list.Get() };
        queue->ExecuteCommandLists(1, lists);
        fv++;
        queue->Signal(fence.Get(), fv);

        if (usePresent1 && swap3) {
            DXGI_PRESENT_PARAMETERS pp = {};
            ComPtr<IDXGISwapChain1> s1;
            swap->QueryInterface(IID_PPV_ARGS(s1.GetAddressOf()));
            s1->Present1(1, 0, &pp);
        } else {
            swap->Present(1, 0);
        }
        Sleep(8);

        // 第 81 帧：回读并统计覆盖层像素
        if (i == 81 && readback) {
            if (fence->GetCompletedValue() < fv) {
                fence->SetEventOnCompletion(fv, ev);
                WaitForSingleObject(ev, 5000);
            }
            void* p = nullptr;
            D3D12_RANGE rr = { 0, 0 };
            if (SUCCEEDED(readback->Map(0, &rr, &p))) {
                UINT d3dW = 0, d3dW2 = 0; { DXGI_SWAP_CHAIN_DESC dd = {}; swap->GetDesc(&dd); d3dW = dd.BufferDesc.Height; d3dW2 = dd.BufferDesc.Width; }
                int diff = 0, total = 0;
                for (UINT y = 0; y < (UINT)d3dW; ++y) {
                    const uint8_t* row = (const uint8_t*)p + (size_t)y * rbPitch;
                    for (UINT x = 0; x < (UINT)d3dW2; ++x) {
                        const uint8_t* px = row + (size_t)x * 4;
                        int db = px[0] - 26, dg = px[1] - 31, dr = px[2] - 41;   // 清屏色约 (0.10,0.12,0.16)
                        if (db < 0) db = -db; if (dg < 0) dg = -dg; if (dr < 0) dr = -dr;
                        if (db + dg + dr > 30) diff++;
                        total++;
                    }
                }
                LogLine(L"后备缓冲回读：与清屏色不同的像素 %d / %d (%.1f%%) —— 覆盖层%s",
                        diff, total, total ? diff * 100.0 / total : 0.0, diff > total / 50 ? L"已画入画面" : L"未出现");
                D3D12_RANGE wr = { 0, 0 };
                readback->Unmap(0, &wr);
                rbDone = true;
            }
        }
    }
    if (!rbDone) LogLine(L"后备缓冲回读：未执行");
    if (fence->GetCompletedValue() < fv) {
        fence->SetEventOnCompletion(fv, ev);
        WaitForSingleObject(ev, 5000);
    }
    LogLine(L"呈现结束，未崩溃即为通过");
    return 0;
}
