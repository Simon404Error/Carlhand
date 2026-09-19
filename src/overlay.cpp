// overlay.cpp - DXGI 交换链挂钩（DX12/DX11 双后端）+ 覆盖层渲染 + 输入捕获
#include "overlay.h"
#include "hook.h"
#include "mem.h"
#include "log.h"
#include "shader_blob.h"

#include <d3d11.h>
#include <windowsx.h>
#pragma comment(lib, "d3d11.lib")

namespace fsd {

    OverlayInfo g_ovl;
    Overlay     g_overlay;
    TrainerDeps g_deps;
    std::atomic<bool> g_unloading{ false };

    namespace {

        typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
        typedef HRESULT(STDMETHODCALLTYPE* Present1Fn)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
        typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffersFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

        PresentFn       oPresent = nullptr;
        Present1Fn      oPresent1 = nullptr;
        ResizeBuffersFn oResize = nullptr;
        WNDPROC         oWndProc = nullptr;
        HWND            g_hwnd = nullptr;

        // 通过挂钩 ID3D12CommandQueue::ExecuteCommandLists 捕获游戏命令队列
        typedef void(STDMETHODCALLTYPE* ExecuteCommandListsFn)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
        ExecuteCommandListsFn oExecute = nullptr;
        std::atomic<ID3D12CommandQueue*> g_gameQueue{ nullptr };
        std::atomic<int>  g_eclCount{ 0 };

        std::mutex        g_inMtx;
        FrameInput        g_in;
        std::atomic<bool> g_inputBlock(false);

        // 哑交换链（用于取得 vtable）
        ComPtr<IDXGIFactory2>        g_dummyFactory;
        ComPtr<ID3D12Device>         g_dummyDev;
        ComPtr<ID3D12CommandQueue>   g_dummyQueue;
        ComPtr<IDXGISwapChain1>      g_dummySwap;
        HWND                         g_dummyWnd = nullptr;

        LRESULT CALLBACK DummyProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

        void STDMETHODCALLTYPE ExecuteCommandListsHook(ID3D12CommandQueue* q, UINT num, ID3D12CommandList* const* lists) {
            int n = ++g_eclCount;
            if (n <= 3) Log("ECL 钩子触发 #%d q=%p 哑队列=%p", n, (void*)q, (void*)g_dummyQueue.Get());
            if (q && q != g_dummyQueue.Get() && !g_gameQueue.load()) g_gameQueue.store(q);
            if (oExecute) oExecute(q, num, lists);
        }

        static HWND SwapchainHwnd(IDXGISwapChain* sc) {
            ComPtr<IDXGISwapChain1> s1;
            if (sc && SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)s1.GetAddressOf()))) {
                HWND h = nullptr;
                if (SUCCEEDED(s1->GetHwnd(&h))) return h;
            }
            return nullptr;
        }

        // ---------------- 输入 ----------------
        // 菜单显示时：吞掉全部键鼠消息（含原始输入 WM_INPUT），游戏只当没收到 —— 键盘/鼠标仅用于操作菜单
        LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
            bool block = g_inputBlock.load();
            if (block) {
                switch (msg) {
                case WM_MOUSEMOVE:
                    { std::lock_guard<std::mutex> lk(g_inMtx); g_in.mx = GET_X_LPARAM(lp); g_in.my = GET_Y_LPARAM(lp); }
                    return 0;
                case WM_LBUTTONDOWN:
                    { std::lock_guard<std::mutex> lk(g_inMtx); g_in.mx = GET_X_LPARAM(lp); g_in.my = GET_Y_LPARAM(lp); g_in.ldown = true; g_in.lclick = true; }
                    return 0;
                case WM_LBUTTONUP:
                    { std::lock_guard<std::mutex> lk(g_inMtx); g_in.ldown = false; }
                    return 0;
                case WM_MOUSEWHEEL:
                    { std::lock_guard<std::mutex> lk(g_inMtx); g_in.wheel += GET_WHEEL_DELTA_WPARAM(wp); }
                    return 0;
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                    { std::lock_guard<std::mutex> lk(g_inMtx); g_in.keys.push_back((UINT)wp); }
                    return 0;
                case WM_CHAR:
                    { std::lock_guard<std::mutex> lk(g_inMtx); g_in.chars.push_back((wchar_t)wp); }
                    return 0;
                case WM_INPUT:                       // 原始输入（UE4 鼠标视角走这里）
                    DefWindowProcW(h, msg, wp, lp);  // 让系统清理缓冲，但不交给游戏
                    return 0;
                case WM_SETCURSOR:
                    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));   // 32512 = IDC_ARROW
                    return TRUE;
                // 其余键鼠消息一律吞掉
                case WM_RBUTTONDOWN: case WM_RBUTTONUP:
                case WM_MBUTTONDOWN: case WM_MBUTTONUP:
                case WM_XBUTTONDOWN: case WM_XBUTTONUP:
                case WM_MOUSEHWHEEL:
                case WM_KEYUP: case WM_SYSKEYUP:
                case WM_SYSCHAR: case WM_DEADCHAR:
                case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK:
                    return 0;
                default: break;
                }
            }
            return oWndProc ? CallWindowProcW(oWndProc, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
        }

        void InstallWndProc(HWND h) {
            if (!h || g_hwnd) return;
            g_hwnd = h;
            oWndProc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)WndProc);
            Log("已接管窗口过程 hwnd=%p old=%p", (void*)h, (void*)oWndProc);
        }

        // ---------------- 渲染器 ----------------
        struct Renderer {
            int   api = 0;                 // 0=未初始化 1=D3D12 2=D3D11 -1=失败
            bool  ownQueue = false;        // 使用自建命令队列（未捕获到游戏队列）
            UINT  width = 0, height = 0;
            UINT  texW = 0, texH = 0;
            bool  ready = false;

            // D3D12
            ComPtr<ID3D12Device>              dev;
            ComPtr<ID3D12CommandQueue>        queue;
            ComPtr<ID3D12DescriptorHeap>      rtvHeap, srvHeap;
            UINT                              rtvInc = 0;
            ComPtr<ID3D12RootSignature>       rootSig;
            ComPtr<ID3D12PipelineState>       pso;
            ComPtr<ID3D12Resource>            tex, upload[2];
            ComPtr<ID3D12CommandAllocator>    alloc[4];
            ComPtr<ID3D12GraphicsCommandList> list;
            ComPtr<ID3D12Fence>               fence;
            HANDLE                            fenceEvent = nullptr;
            UINT64                            fenceVal = 0, allocFence[4] = { 0,0,0,0 };
            UINT64                            uploadFence[2] = { 0,0 };
            int                               upIdx = 0;
            bool                              texFirst = true;
            ComPtr<ID3D12Resource>            bb[8];
            UINT                              bbCount = 0;
            UINT64                            upSize = 0;
            UINT                              upPitch = 0;
            DXGI_FORMAT                       fmt = DXGI_FORMAT_UNKNOWN;
            UINT                              bbIndex = 0;
            ComPtr<IDXGISwapChain3>           sc3;

            // D3D11
            ComPtr<ID3D11Device>              d11dev;
            ComPtr<ID3D11DeviceContext>       d11ctx;
            ComPtr<ID3D11Texture2D>           d11tex;
            ComPtr<ID3D11ShaderResourceView>  d11srv;
            ComPtr<ID3D11RenderTargetView>    d11rtv;
            ComPtr<ID3D11Resource>            d11rtvRes;
            ComPtr<ID3D11VertexShader>        d11vs;
            ComPtr<ID3D11PixelShader>         d11ps;
            ComPtr<ID3D11SamplerState>        d11smp;
            ComPtr<ID3D11BlendState>          d11blend;
            ComPtr<ID3D11RasterizerState>     d11rast;
        } R;

        UINT64 WaitFence(UINT64 v) {
            if (!R.fence || v == 0) return 0;
            if (R.fence->GetCompletedValue() < v) {
                R.fence->SetEventOnCompletion(v, R.fenceEvent);
                WaitForSingleObject(R.fenceEvent, 2000);
            }
            return R.fence->GetCompletedValue();
        }

        // ---------- D3D12 ----------
        bool InitD3D12(IDXGISwapChain* sc) {
            // 策略 1：使用 ExecuteCommandLists 钩子捕获到的游戏命令队列（最稳妥，指令顺序与游戏一致）
            ID3D12CommandQueue* gq = g_gameQueue.load();
            if (gq) R.queue = gq;

            // 策略 2：DXGI GetDevice（部分系统返回 E_NOINTERFACE）
            if (!R.queue) {
                HRESULT hrd = sc->GetDevice(__uuidof(ID3D12CommandQueue), (void**)R.queue.GetAddressOf());
                if (FAILED(hrd) || !R.queue) Log("GetDevice(ID3D12CommandQueue) 不可用 0x%X", (unsigned)hrd);
            }

            // 策略 3：从后备缓冲资源反查 ID3D12Device，自建命令队列
            if (!R.queue) {
                ComPtr<ID3D12Resource> bb0;
                if (SUCCEEDED(sc->GetBuffer(0, IID_PPV_ARGS(bb0.GetAddressOf()))) && bb0) {
                    if (SUCCEEDED(bb0->GetDevice(__uuidof(ID3D12Device), (void**)R.dev.GetAddressOf()))) {
                        D3D12_COMMAND_QUEUE_DESC qd0 = {};
                        qd0.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                        if (SUCCEEDED(R.dev->CreateCommandQueue(&qd0, IID_PPV_ARGS(R.queue.GetAddressOf())))) {
                            R.ownQueue = true;
                            Log("未捕获到游戏命令队列，改用自建命令队列");
                        }
                    }
                }
            }
            if (!R.queue) { Log("无法取得 D3D12 命令队列，覆盖层不可用"); return false; }
            R.dev.Reset();
            ID3D12CommandQueue* qraw = R.queue.Get();
            if (FAILED(qraw->GetDevice(__uuidof(ID3D12Device), (void**)R.dev.GetAddressOf()))) {
                Log("queue->GetDevice(ID3D12Device) 失败");
                return false;
            }

            sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)R.sc3.GetAddressOf());

            DXGI_SWAP_CHAIN_DESC d = {};
            sc->GetDesc(&d);
            R.fmt = d.BufferDesc.Format;
            R.width = d.BufferDesc.Width; R.height = d.BufferDesc.Height;
            R.bbCount = d.BufferCount > 8 ? 8 : (d.BufferCount ? d.BufferCount : 2);
            g_ovl.bbW = (int)R.width; g_ovl.bbH = (int)R.height;

            D3D12_DESCRIPTOR_RANGE range = {};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = 0;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = 0;

            D3D12_STATIC_SAMPLER_DESC ss = {};
            ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            ss.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            ss.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            ss.MaxLOD = D3D12_FLOAT32_MAX;
            ss.ShaderRegister = 0;
            ss.RegisterSpace = 0;
            ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_PARAMETER rp = {};
            rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rp.DescriptorTable.NumDescriptorRanges = 1;
            rp.DescriptorTable.pDescriptorRanges = &range;
            rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rs = {};
            rs.NumParameters = 1; rs.pParameters = &rp;
            rs.NumStaticSamplers = 1; rs.pStaticSamplers = &ss;
            rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> sig, err;
            HRESULT hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, sig.GetAddressOf(), err.GetAddressOf());
            if (FAILED(hr)) { Log("D3D12SerializeRootSignature 失败 0x%X", (UINT)hr); return false; }
            hr = R.dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(R.rootSig.GetAddressOf()));
            if (FAILED(hr)) { Log("CreateRootSignature 失败 0x%X", (UINT)hr); return false; }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC ps = {};
            ps.pRootSignature = R.rootSig.Get();
            ps.VS = { g_VS, sizeof(g_VS) };
            ps.PS = { g_PS, sizeof(g_PS) };
            ps.BlendState.RenderTarget[0].BlendEnable = TRUE;
            ps.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
            ps.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            ps.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            ps.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            ps.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            ps.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            ps.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            ps.SampleMask = UINT_MAX;
            ps.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            ps.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            ps.RasterizerState.DepthClipEnable = TRUE;
            ps.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            ps.NumRenderTargets = 1;
            ps.RTVFormats[0] = R.fmt;
            ps.SampleDesc.Count = 1;
            hr = R.dev->CreateGraphicsPipelineState(&ps, IID_PPV_ARGS(R.pso.GetAddressOf()));
            if (FAILED(hr)) { Log("CreateGraphicsPipelineState 失败 0x%X fmt=%d", (UINT)hr, (int)R.fmt); return false; }

            D3D12_DESCRIPTOR_HEAP_DESC hd = {};
            hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            hd.NumDescriptors = 8;
            hr = R.dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(R.rtvHeap.GetAddressOf()));
            if (FAILED(hr)) { Log("CreateDescriptorHeap(RTV) 失败 0x%X", (UINT)hr); return false; }

            R.rtvInc = R.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            hd.NumDescriptors = 1;
            hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            hr = R.dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(R.srvHeap.GetAddressOf()));
            if (FAILED(hr)) { Log("CreateDescriptorHeap(SRV) 失败 0x%X", (UINT)hr); return false; }
            for (int i = 0; i < 4; ++i)
                if (FAILED(R.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(R.alloc[i].GetAddressOf())))) { Log("CreateCommandAllocator 失败"); return false; }

            hr = R.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, R.alloc[0].Get(), nullptr, IID_PPV_ARGS(R.list.GetAddressOf()));
            if (FAILED(hr)) { Log("CreateCommandList 失败 0x%X", (UINT)hr); return false; }
            R.list->Close();
            if (FAILED(R.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(R.fence.GetAddressOf())))) return false;
            R.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

            D3D12_CPU_DESCRIPTOR_HANDLE h0 = R.rtvHeap->GetCPUDescriptorHandleForHeapStart();
            for (UINT i = 0; i < R.bbCount; ++i) {
                if (FAILED(sc->GetBuffer(i, IID_PPV_ARGS(R.bb[i].GetAddressOf())))) { Log("GetBuffer(%u) 失败", i); return false; }
                D3D12_CPU_DESCRIPTOR_HANDLE hh = h0; hh.ptr += (SIZE_T)i * R.rtvInc;
                R.dev->CreateRenderTargetView(R.bb[i].Get(), nullptr, hh);
            }
            R.api = 1;
            g_ovl.backend = 1;
            Log("D3D12 后端就绪 %ux%u 缓冲=%u fmt=%d%s", R.width, R.height, R.bbCount, (int)R.fmt, R.ownQueue ? "（自建队列）" : "");
            return true;
        }

        bool EnsureUiTexture12(Ui* ui) {
            if (!ui || ui->width <= 0 || !R.dev) return false;
            if (R.tex && R.texW == (UINT)ui->width && R.texH == (UINT)ui->height) return true;
            R.tex.Reset();
            for (int i = 0; i < 2; ++i) { R.upload[i].Reset(); R.uploadFence[i] = 0; }
            R.texW = (UINT)ui->width; R.texH = (UINT)ui->height;
            R.upPitch = (UINT)((R.texW * 4 + 255) & ~255u);
            R.upSize = (UINT64)R.upPitch * R.texH;

            D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC td = {};
            td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            td.Width = R.texW; td.Height = R.texH; td.DepthOrArraySize = 1;
            td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            if (FAILED(R.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST,
                                                      nullptr, IID_PPV_ARGS(R.tex.GetAddressOf())))) return false;

            D3D12_HEAP_PROPERTIES hu = {}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC ud = {};
            ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            ud.Width = R.upSize; ud.Height = 1; ud.DepthOrArraySize = 1;
            ud.MipLevels = 1; ud.Format = DXGI_FORMAT_UNKNOWN;
            ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ud.SampleDesc.Count = 1;
            for (int i = 0; i < 2; ++i)
                if (FAILED(R.dev->CreateCommittedResource(&hu, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                          nullptr, IID_PPV_ARGS(R.upload[i].GetAddressOf())))) return false;

            D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
            sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sv.Texture2D.MipLevels = 1;
            R.dev->CreateShaderResourceView(R.tex.Get(), &sv, R.srvHeap->GetCPUDescriptorHandleForHeapStart());
            R.texFirst = true;
            Log("D3D12 UI 纹理 %ux%u", R.texW, R.texH);
            return true;
        }

        // ---------- D3D11 ----------
        bool InitD3D11(IDXGISwapChain* sc) {
            if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)R.d11dev.GetAddressOf())) || !R.d11dev) return false;
            R.d11dev->GetImmediateContext(R.d11ctx.GetAddressOf());
            DXGI_SWAP_CHAIN_DESC d = {};
            sc->GetDesc(&d);
            R.width = d.BufferDesc.Width; R.height = d.BufferDesc.Height;
            g_ovl.bbW = (int)R.width; g_ovl.bbH = (int)R.height;

            if (FAILED(R.d11dev->CreateVertexShader(g_VS, sizeof(g_VS), nullptr, R.d11vs.GetAddressOf()))) return false;
            if (FAILED(R.d11dev->CreatePixelShader(g_PS, sizeof(g_PS), nullptr, R.d11ps.GetAddressOf()))) return false;

            D3D11_SAMPLER_DESC sd = {};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            if (FAILED(R.d11dev->CreateSamplerState(&sd, R.d11smp.GetAddressOf()))) return false;

            D3D11_BLEND_DESC bd = {};
            bd.RenderTarget[0].BlendEnable = TRUE;
            bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(R.d11dev->CreateBlendState(&bd, R.d11blend.GetAddressOf()))) return false;

            D3D11_RASTERIZER_DESC rd = {};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.DepthClipEnable = TRUE;
            if (FAILED(R.d11dev->CreateRasterizerState(&rd, R.d11rast.GetAddressOf()))) return false;

            R.api = 2;
            g_ovl.backend = 2;
            Log("D3D11 后端就绪 %ux%u", R.width, R.height);
            return true;
        }

        bool EnsureUiTexture11(Ui* ui) {
            if (!ui || ui->width <= 0 || !R.d11dev) return false;
            if (R.d11tex && R.texW == (UINT)ui->width && R.texH == (UINT)ui->height) return true;
            R.d11tex.Reset(); R.d11srv.Reset();
            R.texW = (UINT)ui->width; R.texH = (UINT)ui->height;
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = R.texW; td.Height = R.texH; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DYNAMIC;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(R.d11dev->CreateTexture2D(&td, nullptr, R.d11tex.GetAddressOf()))) return false;
            D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
            sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sv.Texture2D.MipLevels = 1;
            if (FAILED(R.d11dev->CreateShaderResourceView(R.d11tex.Get(), &sv, R.d11srv.GetAddressOf()))) return false;
            Log("D3D11 UI 纹理 %ux%u", R.texW, R.texH);
            return true;
        }

        void ReleaseBackbuffers() {
            for (UINT i = 0; i < 8; ++i) R.bb[i].Reset();
            R.d11rtv.Reset();
            R.d11rtvRes.Reset();
        }

        void RecreateBackbuffers(IDXGISwapChain* sc) {
            if (R.api == 1 && R.rtvHeap) {
                D3D12_CPU_DESCRIPTOR_HANDLE h0 = R.rtvHeap->GetCPUDescriptorHandleForHeapStart();
                for (UINT i = 0; i < R.bbCount; ++i) {
                    if (FAILED(sc->GetBuffer(i, IID_PPV_ARGS(R.bb[i].GetAddressOf())))) break;
                    D3D12_CPU_DESCRIPTOR_HANDLE hh = h0; hh.ptr += (SIZE_T)i * R.rtvInc;
                    R.dev->CreateRenderTargetView(R.bb[i].Get(), nullptr, hh);
                }
            }
        }

        // ---------- 绘制 ----------
        void UploadUI_D12(Ui* ui, ID3D12GraphicsCommandList* cl) {
            int i = R.upIdx;
            WaitFence(R.uploadFence[i]);
            void* p = nullptr;
            D3D12_RANGE rr = { 0, R.upSize };
            if (FAILED(R.upload[i]->Map(0, &rr, &p))) return;
            for (UINT y = 0; y < R.texH; ++y)
                memcpy((uint8_t*)p + (size_t)y * R.upPitch, (const uint8_t*)ui->pixels + (size_t)y * R.texW * 4, (size_t)R.texW * 4);
            D3D12_RANGE wr = { 0, R.upSize };
            R.upload[i]->Unmap(0, &wr);

            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = R.tex.Get();
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            if (!R.texFirst) cl->ResourceBarrier(1, &b);

            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource = R.tex.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource = R.upload[i].Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            src.PlacedFootprint.Footprint.Width = R.texW;
            src.PlacedFootprint.Footprint.Height = R.texH;
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = R.upPitch;
            cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

            D3D12_RESOURCE_BARRIER b2 = b;
            b2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b2.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            cl->ResourceBarrier(1, &b2);
            R.texFirst = false;
            R.upIdx ^= 1;
        }

        void DrawD12(IDXGISwapChain* sc, Ui* ui) {
            (void)sc;
            if (!R.pso || !R.dev) return;
            UINT idx = 0;
            if (R.sc3) idx = R.sc3->GetCurrentBackBufferIndex();
            else { idx = R.bbIndex; R.bbIndex = (R.bbIndex + 1) % (R.bbCount ? R.bbCount : 1); }
            if (idx >= R.bbCount) idx = 0;
            auto& alloc = R.alloc[idx % 4];
            WaitFence(R.allocFence[idx % 4]);

            alloc->Reset();
            R.list->Reset(alloc.Get(), nullptr);

            int usedUp = -1;
            if (ui->dirty && EnsureUiTexture12(ui)) {
                usedUp = R.upIdx;
                UploadUI_D12(ui, R.list.Get());
                ui->dirty = false;
            }
            if (!R.tex) {                       // 纹理尚未成功创建：本帧不绘制
                R.list->Close();
                return;
            }

            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = R.bb[idx].Get();
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            R.list->ResourceBarrier(1, &b);

            D3D12_CPU_DESCRIPTOR_HANDLE rtv = R.rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtv.ptr += (SIZE_T)idx * R.rtvInc;
            R.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            R.list->SetGraphicsRootSignature(R.rootSig.Get());
            R.list->SetPipelineState(R.pso.Get());
            ID3D12DescriptorHeap* heaps[] = { R.srvHeap.Get() };
            R.list->SetDescriptorHeaps(1, heaps);
            R.list->SetGraphicsRootDescriptorTable(0, R.srvHeap->GetGPUDescriptorHandleForHeapStart());
            D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)R.width, (float)R.height, 0.0f, 1.0f };
            R.list->RSSetViewports(1, &vp);
            D3D12_RECT rc = { 0, 0, (LONG)R.width, (LONG)R.height };
            R.list->RSSetScissorRects(1, &rc);
            R.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            R.list->DrawInstanced(3, 1, 0, 0);

            b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            R.list->ResourceBarrier(1, &b);
            R.list->Close();

            ID3D12CommandList* lists[] = { R.list.Get() };
            R.queue->ExecuteCommandLists(1, lists);
            R.fenceVal++;
            R.queue->Signal(R.fence.Get(), R.fenceVal);
            R.allocFence[idx % 4] = R.fenceVal;
            if (usedUp >= 0) R.uploadFence[usedUp] = R.fenceVal;
            if (R.ownQueue) WaitFence(R.fenceVal);   // 自建队列：等覆盖层画完再 Present，降低与游戏指令的重叠
        }

        void DrawD11(IDXGISwapChain* sc, Ui* ui) {
            if (!R.d11ps || !R.d11dev) return;
            static int dbg11 = 0;
            if (dbg11 < 2) { dbg11++; Log("DrawD11 入口 rtv=%p res=%p tex=%p srv=%p ctx=%p viewport=%ux%u",
                (void*)R.d11rtv.Get(), (void*)R.d11rtvRes.Get(), (void*)R.d11tex.Get(), (void*)R.d11srv.Get(),
                (void*)R.d11ctx.Get(), R.width, R.height); }
            // 翻转模型的 GetBuffer(0) 每帧可能返回不同纹理，必须逐帧比对并重建 RTV
            {
                ComPtr<ID3D11Resource> res;
                if (FAILED(sc->GetBuffer(0, IID_PPV_ARGS(res.GetAddressOf()))) || !res) return;
                if (res.Get() != R.d11rtvRes.Get()) {
                    ComPtr<ID3D11Texture2D> tex2d;
                    if (FAILED(res.As(&tex2d))) return;
                    if (FAILED(R.d11dev->CreateRenderTargetView(tex2d.Get(), nullptr, R.d11rtv.ReleaseAndGetAddressOf()))) return;
                    R.d11rtvRes = res;
                }
            }
            if (ui->dirty && EnsureUiTexture11(ui)) {
                D3D11_MAPPED_SUBRESOURCE ms = {};
                if (SUCCEEDED(R.d11ctx->Map(R.d11tex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                    for (UINT y = 0; y < R.texH; ++y)
                        memcpy((uint8_t*)ms.pData + (size_t)y * ms.RowPitch, (const uint8_t*)ui->pixels + (size_t)y * R.texW * 4, (size_t)R.texW * 4);
                    R.d11ctx->Unmap(R.d11tex.Get(), 0);
                }
                ui->dirty = false;
            }
            if (!R.d11tex || !R.d11srv || !R.d11rtv) return;   // 纹理尚未成功创建
            float bl[4] = { 1, 1, 1, 1 };
            D3D11_VIEWPORT vp = { 0, 0, (float)R.width, (float)R.height, 0, 1 };
            R.d11ctx->OMSetRenderTargets(1, R.d11rtv.GetAddressOf(), nullptr);
            R.d11ctx->OMSetBlendState(R.d11blend.Get(), bl, 0xFFFFFFFF);
            R.d11ctx->RSSetState(R.d11rast.Get());
            R.d11ctx->RSSetViewports(1, &vp);
            R.d11ctx->IASetInputLayout(nullptr);
            R.d11ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            R.d11ctx->VSSetShader(R.d11vs.Get(), nullptr, 0);
            R.d11ctx->PSSetShader(R.d11ps.Get(), nullptr, 0);
            R.d11ctx->PSSetSamplers(0, 1, R.d11smp.GetAddressOf());
            R.d11ctx->PSSetShaderResources(0, 1, R.d11srv.GetAddressOf());
            R.d11ctx->Draw(3, 0);
            ID3D11ShaderResourceView* nullSRV = nullptr;
            R.d11ctx->PSSetShaderResources(0, 1, &nullSRV);

            // 诊断：FSDTRAINER_DIAG=1 时立即回读当前后备缓冲，确认覆盖层确实写进了画面
            static int diag = -1;
            if (diag < 0) {
                wchar_t e[8] = L"";
                diag = (GetEnvironmentVariableW(L"FSDTRAINER_DIAG", e, 8) > 0) ? 0 : 1;
            }
            if (diag == 0) {
                diag = 1;
                ComPtr<ID3D11Resource> cur;
                if (SUCCEEDED(sc->GetBuffer(0, IID_PPV_ARGS(cur.GetAddressOf()))) && cur) {
                    ComPtr<ID3D11Texture2D> t2;
                    if (SUCCEEDED(cur.As(&t2))) {
                        D3D11_TEXTURE2D_DESC dd = {};
                        t2->GetDesc(&dd);
                        dd.Usage = D3D11_USAGE_STAGING;
                        dd.BindFlags = 0;
                        dd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                        dd.MiscFlags = 0;
                        ComPtr<ID3D11Texture2D> st;
                        if (SUCCEEDED(R.d11dev->CreateTexture2D(&dd, nullptr, st.GetAddressOf()))) {
                            R.d11ctx->CopyResource(st.Get(), cur.Get());
                            D3D11_MAPPED_SUBRESOURCE ms = {};
                            if (SUCCEEDED(R.d11ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &ms))) {
                                int diff = 0, total = 0;
                                for (UINT y = 0; y < dd.Height; ++y) {
                                    const uint8_t* row = (const uint8_t*)ms.pData + (size_t)y * ms.RowPitch;
                                    for (UINT x = 0; x < dd.Width; ++x) {
                                        const uint8_t* px = row + (size_t)x * 4;
                                        int db = px[0] - 26, dg = px[1] - 31, dr = px[2] - 41;
                                        if (db < 0) db = -db; if (dg < 0) dg = -dg; if (dr < 0) dr = -dr;
                                        if (db + dg + dr > 30) diff++;
                                        total++;
                                    }
                                }
                                Log("D3D11 诊断回读：非清屏色像素 %d/%d (%.1f%%)", diff, total, total ? diff * 100.0 / total : 0.0);
                                R.d11ctx->Unmap(st.Get(), 0);
                            }
                        }
                    }
                }
            }
        }

        // ---------- 每帧处理 ----------
        Ui* EnsureUi() {
            if (g_overlay.GetUi()) return g_overlay.GetUi();
            Ui* ui = new Ui();
            if (!ui->Init((int)R.width, (int)R.height, g_deps.uiScale, g_deps.sc, g_deps.fz, g_deps.pe)) {
                delete ui;
                return nullptr;
            }
            if (g_deps.menuOpen) ui->Toggle();
            g_overlay.SetUi(ui);
            return ui;
        }

        ULONGLONG g_fpsTick = 0;
        int       g_fpsCount = 0;

        void ProcessFrame(IDXGISwapChain* sc) {
            static int retry = 0;
            if (g_unloading.load()) return;      // 即将卸载：不再挂钩处理

            // 已用自建队列时，一旦捕获到游戏队列就切换过去（保证覆盖层绘制排在游戏本帧渲染之后）
            if (R.ownQueue && g_gameQueue.load()) {
                R.queue = g_gameQueue.load();
                R.ownQueue = false;
                Log("已捕获游戏命令队列，切换为游戏队列（覆盖层顺序正确）");
            }

            if (R.api == 0 && retry < 600) {
                retry++;
                // 尚未捕获到游戏的 D3D12 命令队列：先等一会儿（DX11 模式下约 1.5 秒后回退）
                if (!g_gameQueue.load() && retry < 90) {
                    if (retry % 30 == 0) Log("等待捕获 D3D12 命令队列… (%d) ECL钩子调用次数=%d", retry, g_eclCount.load());
                    return;
                }
                if (!InitD3D12(sc)) {
                    Log("D3D12 后端不可用，尝试 D3D11");
                    if (!InitD3D11(sc)) { R.api = -1; retry = 600; Log("覆盖层初始化失败（非 DX12/DX11 交换链）"); }
                }
            }
            if (R.api <= 0) return;

            if (!g_hwnd) InstallWndProc(SwapchainHwnd(sc));
            g_ovl.frame++;
            g_fpsCount++;
            ULONGLONG now = GetTickCount64();
            if (now - g_fpsTick >= 1000) { g_ovl.fps = g_fpsCount; g_fpsCount = 0; g_fpsTick = now; }

            Ui* ui = EnsureUi();
            if (!ui) return;
            if ((UINT)ui->width != R.width || (UINT)ui->height != R.height) {
                ui->Resize((int)R.width, (int)R.height);
                R.texW = 0; R.texH = 0;   // 触发纹理重建
                ui->dirty = true;
            }

            FrameInput in;
            {
                std::lock_guard<std::mutex> lk(g_inMtx);
                in = g_in;
            }
            // 菜单显示时：以真实光标位置 + 左键状态为准（游戏可能用 Raw Input 吞掉 WM_MOUSEMOVE）
            static bool prevDown = false;
            static int  pendingClicks = 0;
            RECT crc = {};
            bool haveClient = g_hwnd && GetClientRect(g_hwnd, &crc) && crc.right > 0 && crc.bottom > 0;
            if (haveClient) {
                ui->viewW = crc.right; ui->viewH = crc.bottom;   // ESP 坐标换算
                // 把视口尺寸写给游戏内 Lua（它算投影时需要宽高比，而 UE4SS 拿 GetViewportSize 不稳）
                static int lastVW = 0, lastVH = 0;
                if (crc.right != lastVW || crc.bottom != lastVH) {
                    lastVW = crc.right; lastVH = crc.bottom;
                    std::wstring lp(LogPath());
                    size_t sp2 = lp.find_last_of(L"\\/");
                    std::wstring dir = (sp2 == std::wstring::npos) ? std::wstring() : lp.substr(0, sp2 + 1);
                    std::wstring vpf = dir + L"FSDTrainer_vp.txt";
                    FILE* vf = nullptr;
                    if (_wfopen_s(&vf, vpf.c_str(), L"wb") == 0 && vf) {
                        fprintf(vf, "%d %d\n", (int)crc.right, (int)crc.bottom);
                        fclose(vf);
                    }
                }
            }
            if (haveClient && ui->visible) {
                POINT pt = {};
                if (GetCursorPos(&pt) && ScreenToClient(g_hwnd, &pt)) { in.mx = pt.x; in.my = pt.y; }
                bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (down && !prevDown) pendingClicks++;
                prevDown = down;
                in.ldown = down;
                if (pendingClicks > 0) in.lclick = true;
                in.mx = MulDiv(in.mx, ui->width, crc.right);
                in.my = MulDiv(in.my, ui->height, crc.bottom);
            } else if (haveClient && !ui->visible) {
                prevDown = false;
            }
            // 菜单没显示时：只在隐藏后的第一帧走一次绘制流程，之后就跳过"软件绘制 + 全屏纹理上传"，
            // 这是进太空站/进任务时卡顿的主因（每帧两次 9MB 上传）。输入队列照常清空。
            static bool drawnHidden = false;
            if (!ui->visible && drawnHidden && !ui->EspWanted()) {
                std::lock_guard<std::mutex> lk(g_inMtx);
                g_in.keys.clear();
                g_in.chars.clear();
                g_in.wheel = 0;
                g_in.lclick = false;
                pendingClicks = 0;
                g_inputBlock = false;
                return;                 // 不画任何东西：游戏自己那帧就是干净的
            }
            bool consumed = ui->Build(in);
            if (consumed) {
                std::lock_guard<std::mutex> lk(g_inMtx);
                g_in.keys.clear();
                g_in.chars.clear();
                g_in.wheel = 0;
                g_in.lclick = false;
                pendingClicks = 0;
            }
            g_inputBlock = ui->visible;

            // 菜单显示时保证光标可见、不被游戏钳制
            if (ui->visible) {
                CURSORINFO ci = { sizeof(ci) };
                if (GetCursorInfo(&ci) && !(ci.flags & CURSOR_SHOWING)) ShowCursor(TRUE);
                static ULONGLONG lastClip = 0;
                ULONGLONG n2 = GetTickCount64();
                if (n2 - lastClip > 500) { lastClip = n2; ClipCursor(nullptr); }
            }

            if (R.api == 1) DrawD12(sc, ui);
            else if (R.api == 2) DrawD11(sc, ui);
            drawnHidden = !ui->visible && !ui->EspWanted();   // ESP 开着时不能走"隐藏后跳过绘制"的快速路径
        }

        HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* sc, UINT sync, UINT flags) {
            __try { ProcessFrame(sc); }
            __except (EXCEPTION_EXECUTE_HANDLER) { Log("ProcessFrame 异常"); }
            return oPresent ? oPresent(sc, sync, flags) : E_FAIL;
        }

        HRESULT STDMETHODCALLTYPE Present1Hook(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
            __try { ProcessFrame(sc); }
            __except (EXCEPTION_EXECUTE_HANDLER) { Log("ProcessFrame 异常"); }
            return oPresent1 ? oPresent1(sc, sync, flags, p) : E_FAIL;
        }

        HRESULT STDMETHODCALLTYPE ResizeHook(IDXGISwapChain* sc, UINT bufferCount, UINT bw, UINT bh, DXGI_FORMAT fmt, UINT flags) {
            // 只在分辨率/格式真的变化时记录，避免刷日志（曾出现 5 万条重复记录）
            static UINT lc = 0, lw = 0, lh = 0; static DXGI_FORMAT lf = DXGI_FORMAT_UNKNOWN;
            if (bufferCount != lc || bw != lw || bh != lh || fmt != lf) {
                lc = bufferCount; lw = bw; lh = bh; lf = fmt;
                Log("交换链 ResizeBuffers count=%u %ux%u fmt=%d", bufferCount, bw, bh, (int)fmt);
            }
            ReleaseBackbuffers();
            R.texW = 0; R.texH = 0;
            HRESULT hr = oResize ? oResize(sc, bufferCount, bw, bh, fmt, flags) : E_FAIL;
            if (SUCCEEDED(hr)) {
                DXGI_SWAP_CHAIN_DESC d = {};
                sc->GetDesc(&d);
                R.width = d.BufferDesc.Width ? d.BufferDesc.Width : bw;
                R.height = d.BufferDesc.Height ? d.BufferDesc.Height : bh;
                R.fmt = d.BufferDesc.Format;
                R.bbCount = d.BufferCount > 8 ? 8 : (d.BufferCount ? d.BufferCount : 2);
                g_ovl.bbW = (int)R.width; g_ovl.bbH = (int)R.height;
                RecreateBackbuffers(sc);
            }
            return hr;
        }
    }  // namespace

    // ---------------- Overlay ----------------
    bool Overlay::Install() {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DummyProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"FSDTrainerDummyWnd";
        RegisterClassExW(&wc);
        g_dummyWnd = CreateWindowExW(0, wc.lpszClassName, L"FSDTrainer", WS_OVERLAPPEDWINDOW,
                                     0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        if (!g_dummyWnd) { Log("哑窗口创建失败"); return false; }

        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(g_dummyDev.GetAddressOf())))) {
            ComPtr<IDXGIFactory4> f4;
            if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(f4.GetAddressOf())))) {
                ComPtr<IDXGIAdapter> warp;
                if (SUCCEEDED(f4->EnumWarpAdapter(IID_PPV_ARGS(warp.GetAddressOf()))))
                    D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(g_dummyDev.GetAddressOf()));
            }
        }
        if (!g_dummyDev) { Log("哑 D3D12 设备创建失败"); return false; }

        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(g_dummyDev->CreateCommandQueue(&qd, IID_PPV_ARGS(g_dummyQueue.GetAddressOf())))) return false;

        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(g_dummyFactory.GetAddressOf())))) return false;

        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = 64; sd.Height = 64;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(g_dummyFactory->CreateSwapChainForHwnd(g_dummyQueue.Get(), g_dummyWnd, &sd, nullptr, nullptr, g_dummySwap.GetAddressOf()))) {
            Log("哑交换链创建失败"); return false;
        }

        bool ok = HookVtable(g_dummySwap.Get(), 8, (void*)PresentHook, (void**)&oPresent);
        HookVtable(g_dummySwap.Get(), 13, (void*)ResizeHook, (void**)&oResize);
        // 命令队列钩子：用于拿到游戏的 ID3D12CommandQueue（DXGI GetDevice 在部分驱动/系统上返回 E_NOINTERFACE）
        // 注意：ID3D12CommandQueue 虚表中 ExecuteCommandLists 位于 10 号槽（11=SetMarker, 12=BeginEvent）
        HookVtable(g_dummyQueue.Get(), 10, (void*)ExecuteCommandListsHook, (void**)&oExecute);
        ComPtr<IDXGISwapChain1> sc1;
        if (SUCCEEDED(g_dummySwap.As(&sc1)))
            HookVtable(sc1.Get(), 22, (void*)Present1Hook, (void**)&oPresent1);
        g_ovl.hooked = ok;
        g_ovl.inited = true;
        Log("Present 挂钩: %s (Present=%p Present1=%p Resize=%p)", ok ? "成功" : "失败", (void*)oPresent, (void*)oPresent1, (void*)oResize);

        // 提前建立 UI（不依赖渲染后端），保证热键呼出与状态日志始终可用
        if (!GetUi() && g_deps.sc) {
            Ui* u = new Ui();
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            if (sw <= 0) sw = 1920;
            if (sh <= 0) sh = 1080;
            if (u->Init(sw, sh, g_deps.uiScale, g_deps.sc, g_deps.fz, g_deps.pe)) {
                if (g_deps.menuOpen) u->Toggle();
                SetUi(u);
                Log("UI 预建立 %dx%d（等待渲染后端就绪后自动对齐分辨率）", u->width, u->height);
            } else {
                delete u;
            }
        }
        return ok;
    }

    void Overlay::Shutdown() {
        if (g_dummySwap && oPresent) UnhookVtable(g_dummySwap.Get(), 8, (void*)oPresent);
        if (g_hwnd && oWndProc) SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        Log("覆盖层已卸载");
    }

    // 解除全部挂钩并恢复窗口过程（END 键“杀死修改器”时调用）
    void Overlay::UninstallHooks() {
        if (g_inputBlock.load()) { g_inputBlock = false; }
        if (g_dummySwap && oPresent)  UnhookVtable(g_dummySwap.Get(), 8, (void*)oPresent);
        if (g_dummySwap && oResize)   UnhookVtable(g_dummySwap.Get(), 13, (void*)oResize);
        if (g_dummySwap && oPresent1) {
            ComPtr<IDXGISwapChain1> sc1;
            if (SUCCEEDED(g_dummySwap.As(&sc1))) UnhookVtable(sc1.Get(), 22, (void*)oPresent1);
        }
        if (g_dummyQueue && oExecute) UnhookVtable(g_dummyQueue.Get(), 10, (void*)oExecute);
        if (g_hwnd && oWndProc) {
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
            oWndProc = nullptr;
            Log("已恢复游戏窗口过程");
        }
        g_ovl.hooked = false;
        Log("全部挂钩已解除");
    }
}
