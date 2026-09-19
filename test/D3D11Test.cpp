// D3D11Test.cpp - 验证用最小 D3D11 呈现程序（模拟 DX11 模式游戏的交换链）
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdint>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

static LRESULT CALLBACK WP(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static void LogLine(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, 1024, _TRUNCATE, fmt, ap);
    va_end(ap);
    wprintf(L"%s\n", buf);
    fflush(stdout);
}

static int RunLoop(IDXGISwapChain* swap, ID3D11Device* dev, ID3D11DeviceContext* ctx,
                   ID3D11RenderTargetView* rtv, ID3D11Texture2D* bb,
                   int frames, bool noSelfLoad, volatile int* outFrame) {
    HMODULE dll = nullptr;
    ID3D11Texture2D* staging = nullptr;
    bool readbackDone = false;
    __try {
        for (int i = 0; i < frames; ++i) {
            *outFrame = i;
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            if (i == 3 && !noSelfLoad) {
                dll = LoadLibraryW(L"FSDTrainer.dll");
                LogLine(L"LoadLibraryW(FSDTrainer.dll) = %p", (void*)dll);
            }
            // 第 81 帧：回读上一轮（覆盖层绘制过）的后备缓冲
            if (i == 81 && !readbackDone) {
                readbackDone = true;
                D3D11_TEXTURE2D_DESC td = {};
                bb->GetDesc(&td);
                td.Usage = D3D11_USAGE_STAGING;
                td.BindFlags = 0;
                td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                td.MiscFlags = 0;
                if (SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &staging)) && staging) {
                    ctx->CopyResource(staging, bb);
                    D3D11_MAPPED_SUBRESOURCE ms = {};
                    if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &ms))) {
                        int diff = 0, total = 0;
                        for (UINT y = 0; y < td.Height; ++y) {
                            const uint8_t* row = (const uint8_t*)ms.pData + (size_t)y * ms.RowPitch;
                            for (UINT x = 0; x < td.Width; ++x) {
                                const uint8_t* px = row + (size_t)x * 4;
                                int db = px[0] - 26, dg = px[1] - 31, dr = px[2] - 41;
                                if (db < 0) db = -db; if (dg < 0) dg = -dg; if (dr < 0) dr = -dr;
                                if (db + dg + dr > 30) diff++;
                                total++;
                            }
                        }
                        LogLine(L"后备缓冲回读：与清屏色不同的像素 %d / %d (%.1f%%) —— 覆盖层%s",
                                diff, total, total ? diff * 100.0 / total : 0.0,
                                diff > total / 50 ? L"已画入画面" : L"未出现");
                        ctx->Unmap(staging, 0);
                    }
                }
            }
            const float col[4] = { 0.10f, 0.12f, 0.16f, 1.0f };
            ctx->OMSetRenderTargets(1, &rtv, nullptr);
            ctx->ClearRenderTargetView(rtv, col);
            swap->Present(1, 0);
            Sleep(8);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogLine(L"!!! 未捕获异常 code=0x%X 在帧 %d", GetExceptionCode(), *outFrame);
        return 2;
    }
    if (staging) staging->Release();
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    int frames = (argc > 1) ? _wtoi(argv[1]) : 200;
    bool noSelfLoad = (argc > 2) && (_wtoi(argv[2]) != 0);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WP;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"D3D11TestWnd";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"D3D11Test", WS_OVERLAPPEDWINDOW,
                                40, 40, 640, 480, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { LogLine(L"窗口创建失败"); return 1; }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 640;
    sd.BufferDesc.Height = 480;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGISwapChain> swap;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                               &fl, 1, D3D11_SDK_VERSION, &sd, swap.GetAddressOf(),
                                               dev.GetAddressOf(), nullptr, ctx.GetAddressOf());
    if (FAILED(hr)) { LogLine(L"D3D11 创建失败 0x%X", (unsigned)hr); return 1; }

    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11Texture2D> bb;
    swap->GetBuffer(0, IID_PPV_ARGS(bb.GetAddressOf()));
    dev->CreateRenderTargetView(bb.Get(), nullptr, rtv.GetAddressOf());

    volatile int frame = 0;
    LogLine(L"开始呈现 %d 帧 (D3D11)", frames);
    int rc = RunLoop(swap.Get(), dev.Get(), ctx.Get(), rtv.Get(), bb.Get(), frames, noSelfLoad, &frame);
    if (rc == 0) LogLine(L"呈现结束，未崩溃即为通过");
    return rc;
}
